// SPDX-License-Identifier: MIT
#include "basix/reference.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>

using namespace basix::reference;
using namespace basix::render;
namespace {
int failures=0,checks=0;
void check(bool condition,const char* name){++checks;if(!condition){++failures;std::cerr<<"FAIL: "<<name<<'\n';}}
bool near(double a,double b,double tolerance=1e-10){return std::abs(a-b)<=tolerance*std::max({1.,std::abs(a),std::abs(b)});}
bool near(Vec3 a,Vec3 b,double tolerance=1e-10){return near(a.x,b.x,tolerance)&&near(a.y,b.y,tolerance)&&near(a.z,b.z,tolerance);}
template<class Function> void rejects(Function f,const char* label){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,label);}
Scene area_scene(){
    Scene s;s.materials={Material{1,{.7,.5,.3},.5,0,{},MaterialModel::lambert},Material{2,{0,0,0},.5,0,{3,2,1},MaterialModel::lambert}};
    s.triangles={{1,1,{-10,-10,0},{10,-10,0},{0,10,0}},
                 {10,2,{-1,-1,2},{1,1,2},{1,-1,2}},
                 {11,2,{-1,-1,2},{-1,1,2},{1,1,2}}};
    return s;
}
Vec3 analytic_direct(){
    // Differential-plane / centered rectangle form factor, half-width=half-height=1,
    // separation=2. This closed-form irradiance oracle does not use light sampling.
    const double a=1,b=1,h=2;
    const double integral=2*(a/std::sqrt(a*a+h*h)*std::atan(b/std::sqrt(a*a+h*h))+
                             b/std::sqrt(b*b+h*h)*std::atan(a/std::sqrt(b*b+h*h)));
    return Vec3{.7,.5,.3}*Vec3{3,2,1}*(integral/std::numbers::pi);
}
Hit receiver(){return {1,{0,0,0},{0,0,1},{0,0,1},1,1,true};}
double uniform(std::mt19937_64& rng){return std::generate_canonical<double,53>(rng);}
Vec3 direct_average(const World& w,unsigned n,std::uint64_t seed){
    Vec3 sum{};std::mt19937_64 rng(seed);
    for(unsigned i=0;i<n;++i){const double a=uniform(rng),b=uniform(rng),c=uniform(rng);sum=sum+direct_lighting(w,receiver(),{0,0,1},w.sample_light({0,0,0},a,b,c),false);}
    return sum/double(n);
}
void geometry(){
    Scene s;s.materials={Material{1}};s.spheres={{3,1,{0,0,0},1}};World world(s);
    auto hit=world.intersect({{0,0,-3},{0,0,2}});check(hit&&near(hit->t,1)&&near(hit->position,{0,0,-1})&&hit->front_face,"sphere nonunit ray root t=1");
    hit=world.intersect({{0,0,0},{0,0,2}});check(hit&&near(hit->t,.5)&&!hit->front_face&&near(hit->normal,{0,0,-1}),"sphere interior exit root and oriented normal");
    check(!world.intersect({{0,0,-3},{0,0,2},1.1,1.9}),"sphere finite interval excludes both roots");
    hit=world.intersect({{0,0,-3},{0,0,2},1.1,3});check(hit&&near(hit->t,2),"sphere interval selects second root");
    hit=world.intersect({{1,0,-3},{0,0,1}});check(hit&&near(hit->t,3),"sphere analytic tangent");
    check(!world.intersect({{0,0,-3},{0,0,0}}),"zero direction rejected");
    s.spheres.clear();s.triangles={{4,1,{-1,-1,0},{1,-1,0},{0,1,0}}};World triangle(s);
    hit=triangle.intersect({{0,0,2},{0,0,-2}});check(hit&&near(hit->t,1)&&hit->primitive_id==4,"triangle analytic plane root");
    check(!triangle.intersect({{2,0,2},{0,0,-1}}),"triangle outside barycentric region");
    check(!triangle.intersect({{0,0,2},{1,0,0}}),"parallel triangle ray");
    check(!triangle.intersect({{0,0,2},{0,0,-1},0,1.99}),"triangle finite tmax");
}
void lights(){
    World w(area_scene());check(near(w.light_area(),4),"rectangle emitter area");
    const auto sample=w.sample_light({0,0,0},.2,.36,.4);check(sample.valid&&near(sample.pdf_area,.25),"area-uniform light selection density");
    const double cosine=2/sample.distance;
    check(near(sample.pdf_solid_angle,sample.distance*sample.distance/(4*cosine)),"area-to-solid-angle Jacobian");
    Hit light{sample.distance,sample.position,sample.normal,sample.normal,sample.primitive_id,2,true};
    check(near(w.light_pdf({0,0,0},light),sample.pdf_solid_angle),"BSDF-hit complementary light PDF");
    check(!w.sample_light({0,0,3},.2,.36,.4).valid,"one-sided emitter rejects backside sample");
    check(w.light_pdf({0,0,3},light)==0,"backside hit light PDF is zero");
    check(!w.sample_light({0,0,0},1,.3,.4).valid,"light sampling rejects u=1");
    check(!w.sample_light({-1,-1,2},0,0,0).valid,"coincident light sample rejected");
    auto broken=sample;broken.pdf_solid_angle=0;check(near(direct_lighting(w,receiver(),{0,0,1},broken,false),{}),"invalid zero light density cannot divide by zero");
    check(near(power_heuristic(2,3)+power_heuristic(3,2),1),"MIS complementary weights");
    check(near(power_heuristic(1e300,1e300),.5)&&near(power_heuristic(0,2),0)&&near(power_heuristic(2,0),1),"MIS stable extreme and zero densities");
    auto blocked=area_scene();blocked.triangles.push_back({20,1,{-10,-10,1},{10,-10,1},{0,10,1}});World blocker(blocked);
    check(near(direct_lighting(blocker,receiver(),{0,0,1},blocker.sample_light({0,0,0},.2,.36,.4),false),{}),"opaque shadow blocker removes NEE contribution");
    const auto estimate=direct_average(w,65536,42),exact=analytic_direct();
    check(near(estimate,exact,.004),"Lambert direct light matches independent rectangular irradiance integral");
    double low_error=0,high_error=0;Vec3 grand{};
    for(std::uint64_t seed=1;seed<=24;++seed){
        const auto low=direct_average(w,16,seed),high=direct_average(w,512,seed);
        low_error+=std::pow(low.x-exact.x,2);high_error+=std::pow(high.x-exact.x,2);grand=grand+high;
    }
    check(high_error<low_error*.2,"greater samples reduce error across independent seeds");
    check(near(grand/24,exact,.008),"multi-seed direct estimate converges to irradiance reference");
}
void transport(){
    World w(area_scene());const Vec3 expected=analytic_direct();const Ray ray{{0,0,.5},{0,0,-1}};
    for(unsigned mode=0;mode<4;++mode){
        IntegratorSettings settings{1,1,mode!=0,mode!=2,mode==3};Vec3 sum{};
        for(std::uint64_t i=0;i<32768;++i)sum=sum+radiance(w,ray,settings,17+i*0x9e3779b97f4a7c15ULL);
        check(near(sum/32768,expected,.012),"pure PT / NEE-MIS / NEE-only / RR agree with independent direct-light reference");
    }
    Scene s;s.materials={Material{1,{.3,.6,.9},.5,0,{},MaterialModel::lambert}};
    s.triangles={{1,1,{-100,-100,0},{100,-100,0},{0,100,0}}};s.environment={2,1,.5};World environment(s);
    IntegratorSettings settings{1,3,true,true,false};
    for(std::uint64_t key=0;key<16;++key)check(near(radiance(environment,ray,settings,key),Vec3{.6,.6,.45}),"Lambert constant environment conserves rho times radiance");
    check(near(radiance(environment,{{0,0,.5},{0,0,1}},settings,1),s.environment),"primary miss sees environment without BSDF factor");
    check(near(radiance(environment,{{0,0,.5},{0,0,-2},0,.2},settings,1),s.environment),"normalized path preserves nonunit initial clipped ray interval");
}
void progressive_and_validation(){
    RenderConfig config;config.width=12;config.height=8;config.seed=7;config.integrator.max_depth=4;
    ProgressiveRenderer whole(cornell_scene(),{},config),chunked(cornell_scene(),{},config);
    whole.add_samples(8);chunked.add_samples(1);chunked.add_samples(3);chunked.add_samples(4);
    const auto a=whole.linear_image(),b=chunked.linear_image();bool same=true,valid=true,lit=false;
    for(size_t i=0;i<a.size();++i){same=same&&a[i].x==b[i].x&&a[i].y==b[i].y&&a[i].z==b[i].z;valid=valid&&finite(a[i])&&a[i].x>=0&&a[i].y>=0&&a[i].z>=0;lit=lit||max_component(a[i])>0;}
    check(same&&whole.samples_per_pixel()==8&&chunked.samples_per_pixel()==8,"chunked accumulation is bit-identical to one batch");
    check(valid&&lit,"Cornell-like scene has finite nonnegative nonblack radiance");
    auto float_config=config;float_config.integrator.random_bits=24;
    ProgressiveRenderer float_whole(cornell_scene(),{},float_config),float_chunked(cornell_scene(),{},float_config);
    float_whole.add_samples(8);float_chunked.add_samples(1);float_chunked.add_samples(3);float_chunked.add_samples(4);
    const auto fa=float_whole.linear_image(),fb=float_chunked.linear_image();bool float_same=true,float_valid=true,changed=false;
    for(size_t i=0;i<fa.size();++i){float_same=float_same&&near(fa[i],fb[i],0);float_valid=float_valid&&finite(fa[i])&&fa[i].x>=0&&fa[i].y>=0&&fa[i].z>=0;changed=changed||!near(fa[i],a[i],0);}
    check(float_same&&float_whole.samples_per_pixel()==8&&float_chunked.samples_per_pixel()==8,"24-bit camera/path streams preserve bit-identical chunked accumulation");
    check(float_valid&&changed,"24-bit stream is finite and distinct from default 53-bit stream");
    check(config.integrator.random_bits==53,"default random precision remains 53 bits");
    check(config.integrator.sampling_proposal==SamplingProposal::ndf_legacy,"CPU default remains legacy NDF");
    auto visible_config=float_config;visible_config.integrator.sampling_proposal=SamplingProposal::visible_v1;
    ProgressiveRenderer vw(cornell_scene(),{},visible_config),vc(cornell_scene(),{},visible_config);
    vw.add_samples(8);vc.add_samples(3);vc.add_samples(5);const auto va=vw.linear_image(),vb=vc.linear_image();bool vi=true,vf=true;
    for(size_t i=0;i<va.size();++i){vi=vi&&near(va[i],vb[i],0);vf=vf&&finite(va[i])&&va[i].x>=0&&va[i].y>=0&&va[i].z>=0;}
    check(vi&&vf,"VNDF integration preserves finite nonnegative batch identity");
    auto bad_proposal=config;bad_proposal.integrator.sampling_proposal=static_cast<SamplingProposal>(42);
    rejects([&]{ProgressiveRenderer x(cornell_scene(),{},bad_proposal);},"unknown sampling proposal rejected");
    auto reordered=cornell_scene();std::reverse(reordered.materials.begin(),reordered.materials.end());std::reverse(reordered.triangles.begin(),reordered.triangles.end());std::reverse(reordered.spheres.begin(),reordered.spheres.end());
    ProgressiveRenderer stable(reordered,{},config);stable.add_samples(8);const auto c=stable.linear_image();bool identical=true;
    for(size_t i=0;i<a.size();++i)identical=identical&&near(a[i],c[i],0);
    check(identical,"stable IDs preserve result after scene vector reordering");
    auto invalid=cornell_scene();invalid.spheres[0].material_id=999;rejects([&]{World x(invalid);},"unknown scene material ID rejected");
    invalid=cornell_scene();invalid.triangles[0].id=invalid.spheres[0].id;rejects([&]{World x(invalid);},"duplicate primitive ID rejected");
    invalid=cornell_scene();invalid.spheres[0].radius=std::numeric_limits<double>::quiet_NaN();rejects([&]{World x(invalid);},"NaN geometry rejected");
    invalid=cornell_scene();invalid.triangles[0].c=invalid.triangles[0].a;rejects([&]{World x(invalid);},"degenerate triangle rejected");
    invalid=cornell_scene();invalid.materials.push_back(invalid.materials[0]);rejects([&]{World x(invalid);},"duplicate material ID rejected");
    auto bad_config=config;bad_config.width=0;rejects([&]{ProgressiveRenderer x(cornell_scene(),{},bad_config);},"zero image dimension rejected");
    Camera bad_camera;bad_camera.vertical_fov_degrees=std::numeric_limits<double>::quiet_NaN();rejects([&]{ProgressiveRenderer x(cornell_scene(),bad_camera,config);},"NaN FOV rejected");
    bad_camera={};bad_camera.up=bad_camera.target-bad_camera.origin;rejects([&]{ProgressiveRenderer x(cornell_scene(),bad_camera,config);},"degenerate camera basis rejected");
    rejects([&]{whole.add_samples(0);},"zero sample batch rejected");
    rejects([&]{whole.add_samples(1000001);},"unbounded sample count rejected");
    IntegratorSettings bad_settings;bad_settings.max_depth=0;rejects([&]{radiance(World(area_scene()),{{0,0,.5},{0,0,-1}},bad_settings,0);},"invalid path depth rejected");
    for(const auto bits:{0u,23u,25u,52u,54u,64u}){
        auto invalid_bits=config;invalid_bits.integrator.random_bits=bits;
        rejects([&]{ProgressiveRenderer x(cornell_scene(),{},invalid_bits);},"unsupported camera random precision rejected");
        rejects([&]{radiance(World(area_scene()),{{0,0,.5},{0,0,-1}},invalid_bits.integrator,0);},"unsupported path random precision rejected");
    }
}
}
int main(){geometry();lights();transport();progressive_and_validation();std::cout<<"CPU reference PT: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
