// SPDX-License-Identifier: MIT
#include "basix/cage.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace basix;
namespace {
int failures=0,checks=0;
void check(bool ok,const char* label) { ++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<label<<'\n';} }
bool near(double a,double b,double eps=1e-10) { return std::abs(a-b)<=eps*std::max({1.0,std::abs(a),std::abs(b)}); }
bool near(Vec3 a,Vec3 b,double eps=1e-10) { return near(a.x,b.x,eps)&&near(a.y,b.y,eps)&&near(a.z,b.z,eps); }
Vec3 affine_vector(Vec3 p) { return {2*p.x+.3*p.y+.1*p.z,1.2*p.y+.2*p.z,.8*p.z}; }
Vec3 affine_point(Vec3 p) { return affine_vector(p)+Vec3{3,-2,5}; }
Tetrahedron affine_tet(Tetrahedron t) { for(auto& p:t)p=affine_point(p);return t; }
const Tetrahedron unit{Vec3{0,0,0},Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
// Independent triangle oracle: standard double-precision Moller-Trumbore.
std::optional<double> triangle_hit(const Ray& ray,const std::array<Vec3,3>& tri) {
    const Vec3 e1=tri[1]-tri[0],e2=tri[2]-tri[0];
    const Vec3 p=cross(ray.direction,e2);
    const double det=dot(e1,p);
    if(std::abs(det)<1e-14)return std::nullopt;
    const Vec3 s=ray.origin-tri[0];const double u=dot(s,p)/det;
    const Vec3 q=cross(s,e1);const double v=dot(ray.direction,q)/det;
    const double t=dot(e2,q)/det;
    if(u<0||v<0||u+v>1||t<std::max(0.0,ray.t_min)||t>ray.t_max)return std::nullopt;
    return t;
}
void mappings() {
    CageError e=CageError::non_finite;
    auto m=CageMap::create(unit,affine_tet(unit),&e);check(m.has_value()&&e==CageError::none,"valid skew deformation");if(!m)return;
    for(unsigned i=0;i<4;++i) {
        check(near(m->to_deformed(unit[i]),affine_point(unit[i])),"vertex correspondence");
        const auto b=m->rest_barycentric(unit[i]);for(unsigned j=0;j<4;++j)check(near(b[j],i==j?1:0),"vertex barycentric basis");
    }
    const Vec3 p{.17,.22,.11};const Vec3 outside{-2,4,.7};
    check(near(m->to_deformed(p),affine_point(p)),"interior affine mapping");
    check(near(m->to_rest(affine_point(outside)),outside),"outside affine extension");
    const auto bary=m->deformed_barycentric(affine_point(p));
    check(near(bary[0],.5)&&near(bary[1],.17)&&near(bary[2],.22)&&near(bary[3],.11),"deformed barycentric values");
    const Ray world{affine_point({.2,.2,-1}),affine_vector({0,0,2}),.1,7};
    const auto rest=m->ray_to_rest(world);
    check(near(rest.origin,{.2,.2,-1})&&near(rest.direction,{0,0,2}),"ray inverse affine transform without normalization");
    check(rest.t_min==.1&&rest.t_max==7,"ray interval preserved");
    for(double t:{.1,.6,1.7})check(near(m->to_rest(world.origin+world.direction*t),rest.origin+rest.direction*t),"ray parameter invariant");
    const auto round=m->ray_to_deformed(rest);check(near(round.origin,world.origin)&&near(round.direction,world.direction),"ray roundtrip");
    const Vec3 tangent1{1,.3,.1},tangent2{.2,1,.4},n=cross(tangent1,tangent2);
    const auto wn=m->normal_to_deformed(n);
    check(near(dot(wn,affine_vector(tangent1)),0)&&near(dot(wn,affine_vector(tangent2)),0),"inverse transpose normal remains perpendicular");
    const auto box=m->deformed_bounds();
    for(auto v:affine_tet(unit))check(v.x>box.minimum.x&&v.x<box.maximum.x&&v.y>box.minimum.y&&v.y<box.maximum.y&&v.z>box.minimum.z&&v.z<box.maximum.z,"outward-rounded conservative bounds");
}
void clipping() {
    const auto m=CageMap::create(unit,unit);if(!m){check(false,"identity cage");return;}
    auto hit=m->clip_deformed({{-1,.2,.2},{2,0,0}});
    check(hit&&near(hit->enter,.5)&&near(hit->exit,.8),"clip analytic entry exit");
    hit=m->clip_deformed({{.1,.2,.2},{2,0,0}});
    check(hit&&hit->enter==0&&near(hit->exit,.25),"clip begins inside");
    check(!m->clip_deformed({{-1,.8,.8},{2,0,0}}),"miss outside two planes");
    check(!m->clip_deformed({{-1,.2,.2},{-2,0,0}}),"behind ray ignored");
    check(!m->clip_deformed({{-1,.2,.2},{2,0,0},0,.4}),"finite tmax rejects later entry");
    check(!m->clip_deformed({{-1,.2,.2},{2,0,0},.9,1}),"tmin beyond exit");
    hit=m->clip_deformed({{.1,.1,.1},{0,0,0},0,4});check(hit&&hit->enter==0&&hit->exit==4,"stationary interior point");
    check(!m->clip_deformed({{2,2,2},{0,0,0}}),"stationary outside point");
    check(!m->clip_deformed({{0,0,0},{1,0,0}},-1),"invalid clipping slack");
    const auto inf=std::numeric_limits<double>::infinity();
    check(!m->clip_deformed({{inf,0,0},{1,0,0}}),"nonfinite ray rejected");
    hit=m->clip_deformed({{-1,.2,.2},{1e-20,0,0}});check(hit&&near(hit->enter/1e20,1)&&near(hit->exit/1e20,1.6),"tiny direction does not become parallel");
}
void triangle_oracle() {
    const auto m=CageMap::create(unit,affine_tet(unit));if(!m)return;
    const std::array<Vec3,3> rest_tri{Vec3{.1,.1,.2},Vec3{.6,.1,.2},Vec3{.1,.6,.2}};
    auto world_tri=rest_tri;for(auto& v:world_tri)v=affine_point(v);
    const Ray world{affine_point({.2,.2,-1}),affine_vector({0,0,2})};
    auto a=triangle_hit(world,world_tri),b=triangle_hit(m->ray_to_rest(world),rest_tri);
    check(a&&b&&near(*a,.6)&&near(*b,*a),"rest mini-geometry hit matches explicitly deformed triangle t");
    auto clip=m->clip_deformed(world);check(clip&&a&&*a>=clip->enter&&*a<=clip->exit,"triangle hit lies inside cage interval");
    const Ray miss{affine_point({.9,.9,-1}),affine_vector({0,0,2})};
    check(!triangle_hit(miss,world_tri)&&!triangle_hit(m->ray_to_rest(miss),rest_tri),"transformed triangle miss invariant");
    // Adjacent cells include a shared face. This prototype intentionally requires
    // downstream hit deduplication; it does not promise watertight hardware traversal.
    const Tetrahedron left{Vec3{0,0,0},Vec3{0,1,0},Vec3{-1,0,0},Vec3{0,0,1}};
    const auto l=CageMap::create(left,affine_tet(left));if(!l){check(false,"adjacent left cell");return;}
    const std::array<Vec3,3> face{Vec3{0,.1,.1},Vec3{0,.6,.1},Vec3{0,.1,.6}};
    auto transformed=face;for(auto& v:transformed)v=affine_point(v);
    const Ray shared{affine_point({-1,.2,.2}),affine_vector({1,0,0})};
    auto oracle=triangle_hit(shared,transformed);
    auto rh=triangle_hit(m->ray_to_rest(shared),face),lh=triangle_hit(l->ray_to_rest(shared),face);
    auto ri=m->clip_deformed(shared),li=l->clip_deformed(shared);
    check(oracle&&rh&&lh&&near(*oracle,1)&&near(*oracle,*rh)&&near(*oracle,*lh),"shared-face triangle agrees for both cell maps");
    check(oracle&&ri&&li&&*oracle>=ri->enter&&*oracle<=ri->exit&&*oracle>=li->enter&&*oracle<=li->exit,"shared-face hit belongs to both conservative intervals");
}
void failures_and_scales() {
    CageError e{};auto bad=unit;bad[3]=bad[2];
    check(!CageMap::create(bad,unit,&e)&&e==CageError::degenerate_rest,"degenerate rest rejected");
    check(!CageMap::create(unit,bad,&e)&&e==CageError::degenerate_deformed,"degenerate deformation rejected");
    bad=unit;std::swap(bad[1],bad[2]);
    check(!CageMap::create(unit,bad,&e)&&e==CageError::inverted_deformation,"inverted deformation rejected");
    check(CageMap::create(bad,bad).has_value(),"negative rest orientation accepted when preserved");
    bad=unit;bad[3].z=1e-14;
    check(!CageMap::create(unit,bad,&e)&&e==CageError::degenerate_deformed,"near singular rejected relative to edge scale");
    bad=unit;bad[0].x=std::numeric_limits<double>::quiet_NaN();
    check(!CageMap::create(bad,unit,&e)&&e==CageError::non_finite,"NaN cage rejected");
    check(!CageMap::create(unit,unit,&e,0)&&e==CageError::invalid_tolerance,"invalid tolerance rejected");
    for(double scale:{1e-100,1e-20,1.0,1e20,1e100}) {
        Tetrahedron r=unit,d=unit;
        for(unsigned i=0;i<4;++i){r[i]=(unit[i]+Vec3{2,-3,4})*scale;d[i]=affine_vector(r[i])+Vec3{-3,7,2}*scale;}
        auto m=CageMap::create(r,d);check(m.has_value(),"cage independent of world-unit scale");if(!m)continue;
        const Vec3 rp=Vec3{2.17,-2.78,4.11}*scale;
        const Vec3 dp=affine_vector(rp)+Vec3{-3,7,2}*scale;
        check(near(m->to_rest(dp)/scale,rp/scale,2e-12),"small/large-scale inverse point");
        check(near(m->to_deformed(rp)/scale,dp/scale,2e-12),"small/large-scale forward point");
        const auto bc=m->deformed_barycentric(dp);check(near(bc[1],.17)&&near(bc[2],.22)&&near(bc[3],.11),"small/large-scale barycentric");
    }
}
}
int main() {
    mappings();clipping();triangle_oracle();failures_and_scales();
    std::cout<<"Cage CPU correctness: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
