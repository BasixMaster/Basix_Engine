// SPDX-License-Identifier: MIT
#include "basix/cage.hpp"

#include <algorithm>
#include <cmath>

namespace basix {
namespace {
bool finite(Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
Vec3 multiply(const std::array<Vec3,3>& columns,Vec3 p) {
    return columns[0]*p.x+columns[1]*p.y+columns[2]*p.z;
}
Vec3 transpose_multiply(const std::array<Vec3,3>& columns,Vec3 p) {
    return {dot(columns[0],p),dot(columns[1],p),dot(columns[2],p)};
}
}

std::optional<CageMap::Frame> CageMap::frame(const Tetrahedron& t,double tol,double& determinant) {
    Frame f{}; f.anchor=t[0];
    for (unsigned i=0;i<3;++i) f.edges[i]=t[i+1]-t[0];
    double scale=0;
    for (auto e:f.edges) {
        if (!finite(e)) return std::nullopt;
        scale=std::max({scale,std::abs(e.x),std::abs(e.y),std::abs(e.z)});
    }
    if (!(scale>0)) return std::nullopt;
    f.scale=scale;
    const auto a=f.edges[0]/scale,b=f.edges[1]/scale,c=f.edges[2]/scale;
    determinant=dot(a,cross(b,c));
    if (!std::isfinite(determinant)||std::abs(determinant)<=tol) return std::nullopt;
    const auto r0=cross(b,c)/determinant;
    const auto r1=cross(c,a)/determinant;
    const auto r2=cross(a,b)/determinant;
    f.normalized_inverse={Vec3{r0.x,r1.x,r2.x},Vec3{r0.y,r1.y,r2.y},Vec3{r0.z,r1.z,r2.z}};
    for (auto e:f.normalized_inverse) if (!finite(e)) return std::nullopt;
    return f;
}

std::optional<CageMap> CageMap::create(const Tetrahedron& rest,const Tetrahedron& deformed,
                                     CageError* error,double tolerance) {
    const auto fail=[error](CageError e)->std::optional<CageMap> { if(error)*error=e;return std::nullopt; };
    if (!std::isfinite(tolerance)||tolerance<=0||tolerance>=1) return fail(CageError::invalid_tolerance);
    for (auto v:rest) if (!finite(v)) return fail(CageError::non_finite);
    for (auto v:deformed) if (!finite(v)) return fail(CageError::non_finite);
    double rd{},dd{};
    auto r=frame(rest,tolerance,rd);if(!r)return fail(CageError::degenerate_rest);
    auto d=frame(deformed,tolerance,dd);if(!d)return fail(CageError::degenerate_deformed);
    if (std::signbit(rd)!=std::signbit(dd)) return fail(CageError::inverted_deformation);
    CageMap result;result.rest_=*r;result.deformed_=*d;result.deformed_vertices_=deformed;
    if(error)*error=CageError::none;
    return result;
}

Barycentric CageMap::barycentric(const Frame& frame,Vec3 point) {
    const auto p=multiply(frame.normalized_inverse,(point-frame.anchor)/frame.scale);
    return {1-p.x-p.y-p.z,p.x,p.y,p.z};
}
Vec3 CageMap::from_barycentric(const Frame& frame,const Barycentric& b) {
    return frame.anchor+multiply(frame.edges,{b[1],b[2],b[3]});
}
Vec3 CageMap::direction_between(const Frame& from,const Frame& to,Vec3 direction) {
    return multiply(to.edges,multiply(from.normalized_inverse,direction/from.scale));
}
Barycentric CageMap::rest_barycentric(Vec3 point) const { return barycentric(rest_,point); }
Barycentric CageMap::deformed_barycentric(Vec3 point) const { return barycentric(deformed_,point); }
Vec3 CageMap::to_rest(Vec3 point) const { return from_barycentric(rest_,barycentric(deformed_,point)); }
Vec3 CageMap::to_deformed(Vec3 point) const { return from_barycentric(deformed_,barycentric(rest_,point)); }
Ray CageMap::ray_to_rest(const Ray& ray) const {
    return {to_rest(ray.origin),direction_between(deformed_,rest_,ray.direction),ray.t_min,ray.t_max};
}
Ray CageMap::ray_to_deformed(const Ray& ray) const {
    return {to_deformed(ray.origin),direction_between(rest_,deformed_,ray.direction),ray.t_min,ray.t_max};
}
Vec3 CageMap::normal_to_deformed(Vec3 normal) const {
    return transpose_multiply(deformed_.normalized_inverse,transpose_multiply(rest_.edges,normal))/deformed_.scale;
}
Aabb CageMap::deformed_bounds() const {
    Aabb box{deformed_vertices_[0],deformed_vertices_[0]};
    for (auto p:deformed_vertices_) {
        box.minimum={std::min(box.minimum.x,p.x),std::min(box.minimum.y,p.y),std::min(box.minimum.z,p.z)};
        box.maximum={std::max(box.maximum.x,p.x),std::max(box.maximum.y,p.y),std::max(box.maximum.z,p.z)};
    }
    const double infinity=std::numeric_limits<double>::infinity();
    box.minimum={std::nextafter(box.minimum.x,-infinity),std::nextafter(box.minimum.y,-infinity),std::nextafter(box.minimum.z,-infinity)};
    box.maximum={std::nextafter(box.maximum.x,infinity),std::nextafter(box.maximum.y,infinity),std::nextafter(box.maximum.z,infinity)};
    return box;
}
std::optional<RayInterval> CageMap::clip_deformed(const Ray& ray,double slack) const {
    if(!finite(ray.origin)||!finite(ray.direction)||!std::isfinite(ray.t_min)||
       std::isnan(ray.t_max)||ray.t_max<0||!std::isfinite(slack)||slack<0) return std::nullopt;
    double lo=std::max(0.0,ray.t_min),hi=ray.t_max;
    if(lo>hi)return std::nullopt;
    const auto origin=deformed_barycentric(ray.origin);
    const auto v=multiply(deformed_.normalized_inverse,ray.direction/deformed_.scale);
    const Barycentric slope{-v.x-v.y-v.z,v.x,v.y,v.z};
    for(unsigned i=0;i<4;++i) {
        if(!std::isfinite(origin[i])||!std::isfinite(slope[i]))return std::nullopt;
        if(slope[i]==0) { if(origin[i]<-slack)return std::nullopt;continue; }
        const double crossing=(-slack-origin[i])/slope[i];
        if(slope[i]>0)lo=std::max(lo,crossing);else hi=std::min(hi,crossing);
        if(lo>hi)return std::nullopt;
    }
    return RayInterval{lo,hi};
}
} // namespace basix
