// SPDX-License-Identifier: MIT
#include "basix/transform_uncertainty.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace basix::transform_uncertainty {
namespace {
using V=std::array<double,3>;
using M=std::array<V,3>;
constexpr double inf=std::numeric_limits<double>::infinity();
constexpr double u=0x1p-24;
void require(bool ok,const char* why){if(!ok)throw std::invalid_argument(why);}
double up(double x){return std::nextafter(x,inf);}
double down(double x){return std::nextafter(x,-inf);}
double add(double a,double b){return a==0?b:b==0?a:up(a+b);}
double mul(double a,double b){return a==0||b==0?0:up(a*b);}
double fp_up(double x){
    require(std::isfinite(x)&&x>=0&&x<=std::numeric_limits<float>::max(),"Unrepresentable uncertainty");
    float f=float(x);if(double(f)<x)f=std::nextafter(f,std::numeric_limits<float>::infinity());
    return f;
}
// Outward double intervals enclose exact-real expressions on FP32 inputs,
// including cancellation. This code requires ordinary IEEE round-to-nearest
// double arithmetic, with no unsafe reassociation/fast-math compilation.
struct Interval{double lo,hi;};
Interval exact(double x){return {x,x};}
Interval plus(Interval a,Interval b){
    if(a.lo==0&&a.hi==0)return b;
    if(b.lo==0&&b.hi==0)return a;
    return {down(a.lo+b.lo),up(a.hi+b.hi)};
}
Interval times(double a,Interval b){
    if(a==0||(b.lo==0&&b.hi==0))return exact(0);
    return a>0?Interval{down(a*b.lo),up(a*b.hi)}:Interval{down(a*b.hi),up(a*b.lo)};
}
double magnitude(Interval x){return std::max(std::abs(x.lo),std::abs(x.hi));}
V vec(render::Vec3 v){return {v.x,v.y,v.z};}
render::Vec3 vec(V v){return {v[0],v[1],v[2]};}
void input(double x){
    require(std::isfinite(x)&&std::abs(x)<=maximum_magnitude,"Value exceeds transform policy range");
    require(double(float(x))==x,"Mesh/batch must contain uploaded FP32 values");
    require(x==0||std::fpclassify(float(x))!=FP_SUBNORMAL,"Subnormal transform input");
}
V checked(render::Vec3 v){auto p=vec(v);for(auto x:p)input(x);return p;}
V abs_product(const M& a,const V& v){
    V out{};for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)out[r]=add(out[r],mul(std::abs(a[r][c]),v[c]));
    return out;
}
std::array<Interval,3> apply(const M& a,const V& t,const std::array<Interval,3>& p){
    std::array<Interval,3> out;
    for(unsigned r=0;r<3;++r){out[r]=exact(t[r]);for(unsigned c=0;c<3;++c)out[r]=plus(out[r],times(a[r][c],p[c]));}
    return out;
}
double altitude(const reference::Triangle& t){
    const auto ab=t.b-t.a,ac=t.c-t.a,bc=t.c-t.b,n=render::cross(ab,ac);
    const double area2=std::hypot(n.x,n.y,n.z);
    const double edge=std::max({std::hypot(ab.x,ab.y,ab.z),std::hypot(ac.x,ac.y,ac.z),std::hypot(bc.x,bc.y,bc.z)});
    require(std::isfinite(area2)&&area2>0&&std::isnormal(float(area2*.5)),"Invalid FP32 triangle area");
    const double scale=std::max({std::abs(ab.x),std::abs(ab.y),std::abs(ab.z),std::abs(ac.x),std::abs(ac.y),std::abs(ac.z)});
    const float ax=float(ab.x)/float(scale),ay=float(ab.y)/float(scale),az=float(ab.z)/float(scale);
    const float bx=float(ac.x)/float(scale),by=float(ac.y)/float(scale),bz=float(ac.z)/float(scale);
    const float nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx;
    require(std::isfinite(nx)&&std::isfinite(ny)&&std::isfinite(nz)&&(nx!=0||ny!=0||nz!=0),"Invalid FP32 triangle normal");
    return down(area2/edge);
}
struct Transform{M l{},a{};V t{},b{};};
Transform transform(const instances::Matrix& matrix){
    Transform out;double rows[3][6]{};double norm=0;
    for(unsigned r=0;r<3;++r){double sum=0;for(unsigned c=0;c<3;++c){input(matrix[r*4+c]);out.l[r][c]=rows[r][c]=matrix[r*4+c];sum=add(sum,std::abs(rows[r][c]));}norm=std::max(norm,sum);input(matrix[r*4+3]);out.t[r]=matrix[r*4+3];rows[r][r+3]=1;}
    for(unsigned c=0;c<3;++c){unsigned p=c;for(unsigned r=c+1;r<3;++r)if(std::abs(rows[r][c])>std::abs(rows[p][c]))p=r;
        require(std::isfinite(rows[p][c])&&rows[p][c]!=0,"Singular transform");
        if(p!=c)for(unsigned j=0;j<6;++j)std::swap(rows[p][j],rows[c][j]);
        const double pivot=rows[c][c];for(unsigned j=0;j<6;++j)rows[c][j]/=pivot;
        for(unsigned r=0;r<3;++r)if(r!=c){const double f=rows[r][c];for(unsigned j=0;j<6;++j)rows[r][j]-=f*rows[c][j];}}
    for(unsigned r=0;r<3;++r){double b=0;for(unsigned c=0;c<3;++c){const double v=rows[r][c+3];require(std::isfinite(v)&&std::abs(v)<=maximum_magnitude,"Inverse coefficient outside policy");out.a[r][c]=float(v);input(out.a[r][c]);b-=v*out.t[c];}
        require(std::isfinite(b)&&std::abs(b)<=maximum_magnitude,"Inverse translation outside policy");out.b[r]=float(b);input(out.b[r]);}
    // Certify ||L^-1|| using the uploaded approximate inverse A:
    // E=I-A*L, ||E||<1 => ||L^-1|| <= ||A||/(1-||E||).
    // Outward intervals make this a conservative refusal near the cutoff,
    // including possible refusal of an exact condition-number boundary.
    double defect=0,uploaded_norm=0;
    for(unsigned r=0;r<3;++r){double error_sum=0,a_sum=0;
        for(unsigned c=0;c<3;++c){Interval product=exact(0);for(unsigned k=0;k<3;++k)product=plus(product,times(out.a[r][k],exact(out.l[k][c])));
            const auto e=plus(exact(r==c?1.:0.),{-product.hi,-product.lo});error_sum=add(error_sum,magnitude(e));a_sum=add(a_sum,std::abs(out.a[r][c]));}
        defect=std::max(defect,error_sum);uploaded_norm=std::max(uploaded_norm,a_sum);
    }
    require(defect<1,"Inverse residual cannot certify transform condition");
    const double certified_inverse_norm=up(uploaded_norm/down(1-defect));
    require(mul(norm,certified_inverse_norm)<=maximum_condition_number,"Transform condition exceeds policy");
    return out;
}
}
std::vector<Budget> analyze(const reference::Scene& mesh,const instances::Batch& batch,Domain domain){
    require(std::isfinite(domain.origin_limit)&&std::isfinite(domain.max_distance)&&domain.origin_limit>1&&domain.max_distance>=4*domain.origin_limit&&domain.origin_limit<=maximum_magnitude&&domain.max_distance<=maximum_magnitude,"Invalid transform ray domain");
    require(double(float(domain.origin_limit))==domain.origin_limit&&double(float(domain.max_distance))==domain.max_distance,"Ray domain must match uploaded FP32 values");
    require(!mesh.triangles.empty()&&mesh.spheres.empty()&&!batch.metadata.empty()&&batch.matrices.size()==batch.metadata.size(),"Invalid transform budget layout");
    const auto count=mesh.triangles.size();require(count<=100000&&batch.metadata.size()<=100000/count,"Transform budget triangle range exceeded");
    require(batch.shader_scene.triangles.size()==count*batch.metadata.size()&&batch.reference_scene.triangles.size()==batch.shader_scene.triangles.size()&&batch.orientation_per_triangle.size()==batch.shader_scene.triangles.size()&&batch.condition_numbers.size()==batch.metadata.size(),"Invalid world triangle count");
    for(const auto& t:mesh.triangles){checked(t.a);checked(t.b);checked(t.c);altitude(t);}
    std::vector<Budget> result;result.reserve(batch.shader_scene.triangles.size());
    const double gamma7=up((7*u)/(1-7*u));
    std::set<std::uint32_t> stable_ids,primitive_ids;
    for(std::size_t slot=0;slot<batch.metadata.size();++slot){
        const auto& meta=batch.metadata[slot];require(meta.first_triangle==slot*count&&meta.triangle_count==count,"Invalid dense triangle span");
        require(meta.stable_id!=0&&stable_ids.insert(meta.stable_id).second&&meta.epoch!=0&&meta.epoch==batch.metadata[0].epoch,"Invalid stable instance metadata");
        require(std::isfinite(batch.condition_numbers[slot])&&batch.condition_numbers[slot]>=1,"Invalid stored condition metadata");
        const auto tr=transform(batch.matrices[slot]);
        V inverse_arithmetic{};const auto ray=add(domain.origin_limit,domain.max_distance);
        auto av=abs_product(tr.a,{ray,ray,ray});for(unsigned r=0;r<3;++r)inverse_arithmetic[r]=mul(gamma7,add(av[r],std::abs(tr.b[r])));
        for(std::size_t primitive=0;primitive<count;++primitive){
            const auto& object=mesh.triangles[primitive];const auto& world=batch.shader_scene.triangles[meta.first_triangle+primitive];
            const auto& ref=batch.reference_scene.triangles[meta.first_triangle+primitive];const auto orientation=batch.orientation_per_triangle[meta.first_triangle+primitive];
            require(world.id>=object.id&&primitive_ids.insert(world.id).second&&world.id-object.id==batch.shader_scene.triangles[meta.first_triangle].id-mesh.triangles[0].id,"Invalid mapped primitive metadata");
            require(!world.reverse_orientation&&(orientation==1||orientation==-1)&&ref.reverse_orientation==(orientation==-1)&&world.id==ref.id&&world.material_id==ref.material_id,"Invalid mapped orientation metadata");
            require(world.a.x==ref.a.x&&world.a.y==ref.a.y&&world.a.z==ref.a.z&&world.b.x==ref.b.x&&world.b.y==ref.b.y&&world.b.z==ref.b.z&&world.c.x==ref.c.x&&world.c.y==ref.c.y&&world.c.z==ref.c.z,"Reference/world geometry mismatch");
            const std::array<V,3> ov{checked(object.a),checked(object.b),checked(object.c)},wv{checked(world.a),checked(world.b),checked(world.c)};
            V rep{},forward{},inverse_residual{},extent{};
            for(unsigned k=0;k<3;++k)for(unsigned r=0;r<3;++r){require(std::abs(wv[k][r])<=domain.origin_limit-1,"World coordinate lacks spawn margin");}
            for(unsigned k=0;k<3;++k){
                std::array<Interval,3> p{exact(ov[k][0]),exact(ov[k][1]),exact(ov[k][2])};const auto q=apply(tr.l,tr.t,p),back=apply(tr.a,tr.b,q);
                V absv{std::abs(ov[k][0]),std::abs(ov[k][1]),std::abs(ov[k][2])};const auto mag=abs_product(tr.l,absv);
                for(unsigned r=0;r<3;++r){
                    // World must be the correctly rounded double-evaluated
                    // FP32 bake. Exact intervals additionally enclose the
                    // rounding difference from that bake's real transform.
                    const double qhat=((tr.l[r][0]*ov[k][0]+tr.l[r][1]*ov[k][1])+tr.l[r][2]*ov[k][2])+tr.t[r];
                    require(double(float(qhat))==wv[k][r],"World/source transform mismatch");
                    rep[r]=std::max(rep[r],mul(magnitude(plus(exact(wv[k][r]),{-q[r].hi,-q[r].lo})),1+2*u));
                    forward[r]=std::max(forward[r],mul(gamma7,add(mag[r],std::abs(tr.t[r]))));
                    inverse_residual[r]=std::max(inverse_residual[r],magnitude(plus(back[r],exact(-ov[k][r]))));
                }
            }
            for(unsigned r=0;r<3;++r){const auto e1=magnitude(plus(exact(ov[1][r]),exact(-ov[0][r]))),e2=magnitude(plus(exact(ov[2][r]),exact(-ov[0][r])));extent[r]=mul(gamma7,mul(2,std::max(e1,e2)));inverse_residual[r]=add(inverse_residual[r],inverse_arithmetic[r]);}
            const auto object_policy=abs_product(tr.l,extent),inverse_policy=abs_product(tr.l,inverse_residual);
            V total{};for(unsigned r=0;r<3;++r){rep[r]=fp_up(rep[r]);forward[r]=fp_up(forward[r]);total[r]=fp_up(add(add(rep[r],forward[r]),add(fp_up(object_policy[r]),fp_up(inverse_policy[r]))));}
            require(std::max({total[0],total[1],total[2]})<=altitude(world)/64,"Transform uncertainty exceeds triangle resolution policy");
            V op{},ip{};for(unsigned r=0;r<3;++r){op[r]=fp_up(object_policy[r]);ip[r]=fp_up(inverse_policy[r]);}
            result.push_back({vec(rep),vec(forward),vec(op),vec(ip),vec(total)});
        }
    }
    return result;
}
} // namespace basix::transform_uncertainty
