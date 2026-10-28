// SPDX-License-Identifier: MIT
#include "basix/reference.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <stdexcept>
#include <utility>

namespace basix::reference {
using namespace render;
namespace {
constexpr double epsilon=std::numeric_limits<double>::epsilon();
bool nonnegative(Vec3 v) { return finite(v)&&v.x>=0&&v.y>=0&&v.z>=0; }
bool uniform(double u) { return std::isfinite(u)&&u>=0&&u<1; }
double length(Vec3 v) { return std::hypot(v.x,v.y,v.z); }
bool valid_ray(const Ray& r) {
    const double squared=dot(r.direction,r.direction);
    return finite(r.origin)&&finite(r.direction)&&squared>0&&std::isfinite(squared)&&
        std::isfinite(r.t_min)&&r.t_min>=0&&!std::isnan(r.t_max)&&r.t_max>=r.t_min;
}
std::uint64_t hash(std::uint64_t x) {
    x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);
}
struct Rng { std::uint64_t key,counter=0;double next() { return double(hash(key+counter++*0x9e3779b97f4a7c15ULL)>>11)*0x1.0p-53; } };
struct Frame {
    Vec3 x,y,z;
    explicit Frame(Vec3 n):z(n) { x=normalize(cross(std::abs(n.z)<.999?Vec3{0,0,1}:Vec3{0,1,0},n));y=cross(z,x); }
    Vec3 local(Vec3 p)const { return {dot(p,x),dot(p,y),dot(p,z)}; }
    Vec3 world(Vec3 p)const { return x*p.x+y*p.y+z*p.z; }
};
Vec3 offset(Vec3 p,Vec3 n,Vec3 direction) {
    const double sign=dot(n,direction)>=0?1.0:-1.0;
    const double scale=std::max({1.0,std::abs(p.x),std::abs(p.y),std::abs(p.z)});
    const Vec3 delta=n*(sign*64*epsilon*scale);p=p+delta;
    const double inf=std::numeric_limits<double>::infinity();
    if(delta.x)p.x=std::nextafter(p.x,delta.x>0?inf:-inf);
    if(delta.y)p.y=std::nextafter(p.y,delta.y>0?inf:-inf);
    if(delta.z)p.z=std::nextafter(p.z,delta.z>0?inf:-inf);
    return p;
}
double triangle_area(const Triangle& t) { return .5*length(cross(t.b-t.a,t.c-t.a)); }
}

bool validate(const Scene& scene,std::string* reason) {
    auto bad=[reason](const char* text){if(reason)*reason=text;return false;};
    if(scene.materials.empty()||scene.materials.size()>4096||scene.spheres.size()+scene.triangles.size()>100000)return bad("Invalid scene size");
    if(!nonnegative(scene.environment))return bad("Environment must be finite and nonnegative");
    std::set<std::uint32_t> materials,primitives;
    for(const auto& m:scene.materials)if(!render::validate(m)||!materials.insert(m.id).second)return bad("Invalid or duplicate material ID/value");
    auto material=[&](std::uint32_t id)->const Material*{for(const auto& m:scene.materials)if(m.id==id)return &m;return nullptr;};
    for(const auto& s:scene.spheres) {
        const auto* m=material(s.material_id);
        if(!s.id||!primitives.insert(s.id).second||!m||!finite(s.center)||!std::isfinite(s.radius)||s.radius<=0||s.radius>1e6)return bad("Invalid sphere geometry or ID");
        if(max_component(m->emission)>0)return bad("This reference samples only triangle emitters; emitting spheres are unsupported");
    }
    double light_area=0;
    for(const auto& t:scene.triangles) {
        const auto* m=material(t.material_id);const double area=triangle_area(t);
        if(!t.id||!primitives.insert(t.id).second||!m||!finite(t.a)||!finite(t.b)||!finite(t.c)||!std::isfinite(area)||area<=0)return bad("Invalid triangle geometry or ID");
        if(max_component(m->emission)>0)light_area+=area;
    }
    if(!std::isfinite(light_area))return bad("Nonfinite emitter area");
    return true;
}
bool validate(const Camera& c) {
    const Vec3 forward=c.target-c.origin;
    return finite(c.origin)&&finite(c.target)&&finite(c.up)&&length(forward)>0&&std::isfinite(length(forward))&&
        length(cross(normalize(forward),normalize(c.up)))>1e-8&&std::isfinite(c.vertical_fov_degrees)&&
        c.vertical_fov_degrees>=1&&c.vertical_fov_degrees<=175;
}
bool validate(const IntegratorSettings& s) { return s.max_depth>=1&&s.max_depth<=64&&s.rr_start>=1&&s.rr_start<=64; }
double power_heuristic(double a,double b) {
    if(!(a>0)||!std::isfinite(a))return 0;
    if(!(b>0)||!std::isfinite(b))return 1;
    if(a>=b){const double r=b/a;return 1/(1+r*r);}const double r=a/b;return r*r/(1+r*r);
}
World::World(Scene scene):scene_(std::move(scene)) {
    std::string reason;if(!validate(scene_,&reason))throw std::invalid_argument(reason);
    for(const auto& t:scene_.triangles)if(max_component(material(t.material_id).emission)>0)lights_.push_back({t,normalize(cross(t.b-t.a,t.c-t.a)),triangle_area(t)});
    std::sort(lights_.begin(),lights_.end(),[](const Light& a,const Light& b){return a.triangle.id<b.triangle.id;});
    for(const auto& l:lights_)total_light_area_+=l.area;
}
const Material& World::material(std::uint32_t id)const {
    for(const auto& m:scene_.materials)if(m.id==id)return m;
    throw std::invalid_argument("Unknown material ID");
}
std::optional<Hit> World::intersect(const Ray& ray)const {
    if(!valid_ray(ray))return std::nullopt;
    std::optional<Hit> closest;
    auto commit=[&](double t,Vec3 outward,std::uint32_t primitive,std::uint32_t material_id) {
        if(!std::isfinite(t)||t<ray.t_min||t>ray.t_max||(closest&&(t>closest->t||(t==closest->t&&primitive>=closest->primitive_id))))return;
        const bool front=dot(ray.direction,outward)<0;
        closest=Hit{t,ray.origin+ray.direction*t,front?outward:-outward,outward,primitive,material_id,front};
    };
    const double a=dot(ray.direction,ray.direction);
    for(const auto& s:scene_.spheres) {
        const Vec3 oc=ray.origin-s.center;const double b=dot(oc,ray.direction),c=dot(oc,oc)-s.radius*s.radius,disc=b*b-a*c;
        if(!std::isfinite(disc)||disc<0)continue;
        const double root=std::sqrt(disc),q=-b-std::copysign(root,b);
        double first,second;if(q==0)first=second=-b/a;else{first=q/a;second=c/q;if(first>second)std::swap(first,second);}
        const double t=first>=ray.t_min?first:second;
        commit(t,normalize(ray.origin+ray.direction*t-s.center),s.id,s.material_id);
    }
    for(const auto& t:scene_.triangles) {
        const Vec3 e1=t.b-t.a,e2=t.c-t.a,p=cross(ray.direction,e2);const double determinant=dot(e1,p);
        if(std::abs(determinant)<=16*epsilon*length(e1)*length(e2)*length(ray.direction))continue;
        const Vec3 s=ray.origin-t.a;const double u=dot(s,p)/determinant;if(u<0||u>1)continue;
        const Vec3 q=cross(s,e1);const double v=dot(ray.direction,q)/determinant;if(v<0||u+v>1)continue;
        commit(dot(e2,q)/determinant,normalize(cross(e1,e2)),t.id,t.material_id);
    }
    return closest;
}
LightSample World::sample_light(Vec3 reference,double select,double u1,double u2)const {
    if(!finite(reference)||!uniform(select)||!uniform(u1)||!uniform(u2)||lights_.empty())return {};
    const double target=select*total_light_area_;double sum=0;const Light* chosen=&lights_.back();
    for(const auto& light:lights_){sum+=light.area;if(target<sum){chosen=&light;break;}}
    const double root=std::sqrt(u1);const Vec3 p=chosen->triangle.a*(1-root)+chosen->triangle.b*(root*(1-u2))+chosen->triangle.c*(root*u2);
    const Vec3 delta=p-reference;const double distance=length(delta);if(!(distance>0)||!std::isfinite(distance))return {};
    const Vec3 wi=delta/distance;const double cosine=dot(chosen->normal,-wi);if(!(cosine>0))return {};
    const double area_pdf=1/total_light_area_,density=area_pdf*distance*distance/cosine;
    if(!(density>0)||!std::isfinite(density))return {};
    return {p,chosen->normal,material(chosen->triangle.material_id).emission,wi,distance,area_pdf,density,chosen->triangle.id,true};
}
double World::light_pdf(Vec3 reference,const Hit& hit)const {
    if(!finite(reference)||!finite(hit.position))return 0;
    for(const auto& l:lights_)if(l.triangle.id==hit.primitive_id) {
        const Vec3 delta=hit.position-reference;const double distance=length(delta);if(!(distance>0))return 0;
        const double cosine=dot(l.normal,-delta/distance);if(!(cosine>0))return 0;
        const double p=distance*distance/(cosine*total_light_area_);return std::isfinite(p)?p:0;
    }
    return 0;
}
Vec3 direct_lighting(const World& world,const Hit& hit,Vec3 wo,const LightSample& light,bool mis) {
    if(!light.valid||!finite(light.position)||!nonnegative(light.emission)||!finite(light.wi)||
       std::abs(dot(light.wi,light.wi)-1)>1e-6||!(light.pdf_solid_angle>0)||!std::isfinite(light.pdf_solid_angle))return {};
    const Frame frame(hit.normal);const Vec3 local_wi=frame.local(light.wi),local_wo=frame.local(wo);
    if(local_wi.z<=0||local_wo.z<=0)return {};
    const Vec3 start=offset(hit.position,hit.normal,light.wi),delta=light.position-start;const double distance=length(delta);
    if(!(distance>0))return {};
    if(world.intersect({start,delta/distance,0,distance*(1-128*epsilon)}))return {};
    const auto& m=world.material(hit.material_id);const Vec3 f=render::evaluate(m,local_wo,local_wi);
    const double weight=mis?power_heuristic(light.pdf_solid_angle,render::pdf(m,local_wo,local_wi)):1;
    return f*light.emission*(local_wi.z*weight/light.pdf_solid_angle);
}
Vec3 radiance(const World& world,const Ray& initial,const IntegratorSettings& settings,std::uint64_t key) {
    if(!validate(settings)||!valid_ray(initial))throw std::invalid_argument("Invalid path settings or ray");
    Rng rng{key};Ray ray=initial;const double initial_length=length(ray.direction);ray.direction=ray.direction/initial_length;
    ray.t_min*=initial_length;ray.t_max*=initial_length;
    if(!valid_ray(ray))throw std::invalid_argument("Initial ray interval exceeds representable distance");
    // Path rays are normalized internally. Scaling their initial t interval
    // preserves the caller's nonnormalized ray parameterization and clipped segment.
    Vec3 throughput{1,1,1},sum{};Vec3 previous{};double previous_pdf=0;
    for(std::uint32_t depth=0;depth<=settings.max_depth;++depth) {
        const auto hit=world.intersect(ray);
        if(!hit){sum=sum+throughput*world.scene().environment;break;}
        const auto& material=world.material(hit->material_id);
        if(hit->front_face&&max_component(material.emission)>0) {
            double weight=1;
            const double lp=depth?world.light_pdf(previous,*hit):0;
            if(depth&&settings.next_event_estimation&&lp>0)weight=settings.multiple_importance_sampling?power_heuristic(previous_pdf,lp):0;
            sum=sum+throughput*material.emission*weight;
        }
        if(depth==settings.max_depth)break;
        const Frame frame(hit->normal);const Vec3 wo=frame.local(-ray.direction);
        if(settings.next_event_estimation) {
            const double light_select=rng.next(),light_u1=rng.next(),light_u2=rng.next();
            sum=sum+throughput*direct_lighting(world,*hit,-ray.direction,world.sample_light(hit->position,light_select,light_u1,light_u2),settings.multiple_importance_sampling);
        }
        const double lobe=rng.next(),bsdf_u1=rng.next(),bsdf_u2=rng.next();
        const auto sample=render::sample(material,wo,lobe,bsdf_u1,bsdf_u2);if(!sample.valid)break;
        throughput=throughput*sample.f*(sample.wi.z/sample.pdf);
        if(!finite(throughput))throw std::runtime_error("Nonfinite path throughput");
        if(max_component(throughput)<=0)break;
        const Vec3 wi=normalize(frame.world(sample.wi));previous=hit->position;previous_pdf=sample.pdf;
        if(settings.russian_roulette&&depth+1>=settings.rr_start) {
            const double survive=std::clamp(max_component(throughput),.05,.95);
            if(rng.next()>=survive)break;
            throughput=throughput/survive;
        }
        ray={offset(hit->position,hit->normal,wi),wi};
    }
    if(!nonnegative(sum))throw std::runtime_error("Invalid path radiance");
    return sum;
}
Scene cornell_scene() {
    Scene s;s.materials={
        Material{1,{.75,.75,.75},.5,0,{},MaterialModel::lambert},Material{2,{.75,.12,.08},.5,0,{},MaterialModel::lambert},
        Material{3,{.08,.55,.15},.5,0,{},MaterialModel::lambert},Material{4,{.95,.65,.22},.28,1,{},MaterialModel::metallic_roughness},
        Material{5,{.25,.35,.75},.5,0,{},MaterialModel::lambert},Material{6,{0,0,0},.5,0,{12,12,12},MaterialModel::lambert}};
    std::uint32_t id=10;
    auto quad=[&](Vec3 a,Vec3 b,Vec3 c,Vec3 d,std::uint32_t m){s.triangles.push_back({id++,m,a,b,c});s.triangles.push_back({id++,m,a,c,d});};
    quad({-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1},1);
    quad({-1,1,1},{1,1,1},{1,1,-1},{-1,1,-1},1);
    quad({-1,-1,-1},{-1,1,-1},{1,1,-1},{1,-1,-1},1);
    quad({-1,-1,1},{-1,1,1},{-1,1,-1},{-1,-1,-1},2);
    quad({1,-1,-1},{1,1,-1},{1,1,1},{1,-1,1},3);
    quad({-.35,.98,-.25},{.35,.98,-.25},{.35,.98,.25},{-.35,.98,.25},6);
    s.spheres={{100,5,{-.4,-.65,.05},.35},{101,4,{.4,-.55,-.25},.45}};
    return s;
}
ProgressiveRenderer::ProgressiveRenderer(Scene scene,Camera camera,RenderConfig config):world_(std::move(scene)),camera_(camera),config_(config) {
    if(!validate(camera_)||!validate(config.integrator)||config.width<1||config.height<1||config.width>2048||config.height>2048)throw std::invalid_argument("Invalid camera/render configuration");
    sums_.resize(std::size_t(config.width)*config.height);
}
void ProgressiveRenderer::add_samples(std::uint32_t count) {
    if(!count||count>1000000||samples_+count>1000000||std::uint64_t(config_.width)*config_.height*(samples_+count)>100000000)throw std::invalid_argument("Requested reference workload exceeds bounded sample limits");
    auto accumulated=sums_; // Keep a failed batch from corrupting previous samples.
    const Vec3 forward=normalize(camera_.target-camera_.origin),right=normalize(cross(forward,camera_.up)),up=cross(right,forward);
    const double tangent=std::tan(camera_.vertical_fov_degrees*std::numbers::pi/360),aspect=double(config_.width)/config_.height;
    for(std::uint64_t sample_index=samples_;sample_index<samples_+count;++sample_index)for(std::uint32_t y=0;y<config_.height;++y)for(std::uint32_t x=0;x<config_.width;++x) {
        const std::uint64_t pixel=std::uint64_t(y)*config_.width+x,key=hash(config_.seed^hash(pixel)^hash(sample_index+0x4b1d3f857ef01a29ULL));Rng rng{key};
        const double camera_u=rng.next(),camera_v=rng.next();
        const double px=(2*(double(x)+camera_u)/config_.width-1)*aspect*tangent,py=(1-2*(double(y)+camera_v)/config_.height)*tangent;
        const Ray ray{camera_.origin,normalize(forward+right*px+up*py)};
        accumulated[static_cast<std::size_t>(pixel)]=accumulated[static_cast<std::size_t>(pixel)]+radiance(world_,ray,config_.integrator,hash(key+2));
    }
    sums_=std::move(accumulated);samples_+=count;
}
std::vector<Vec3> ProgressiveRenderer::linear_image()const {
    std::vector<Vec3> image=sums_;if(samples_)for(auto& value:image)value=value/double(samples_);return image;
}
} // namespace basix::reference
