// SPDX-License-Identifier: MIT
#include "basix/ray_spawn.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

// Original arithmetic from error-box projection principles described in
// https://pbr-book.org/4ed/Shapes/Managing_Rounding_Error . No implementation
// source is incorporated. This helper does not bound a hardware intersector.
namespace basix::ray_spawn {
namespace {
constexpr float infinity=std::numeric_limits<float>::infinity();
constexpr float unit_roundoff=0x1p-24f;
constexpr float gamma7=(7*unit_roundoff)/(1-7*unit_roundoff);
bool finite(Float3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
Float3 sub(Float3 a,Float3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Float3 mul(Float3 a,float s){return {a.x*s,a.y*s,a.z*s};}
float dot(Float3 a,Float3 b){return (a.x*b.x+a.y*b.y)+a.z*b.z;}
Float3 cross(Float3 a,Float3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float maximum(Float3 v){return std::max({std::abs(v.x),std::abs(v.y),std::abs(v.z)});}
float up(float x){return std::nextafter(x,infinity);}
float envelope(float a,float b,float c,float u,float v,float extra){
    // Absolute differences are bounded by |b|+|a| and |c|+|a|. Using those
    // input magnitudes also covers rounding in the two edge subtractions.
    float e1=up(std::abs(b)+std::abs(a)),e2=up(std::abs(c)+std::abs(a));
    float magnitude=up(up(std::abs(a)+up(std::abs(u)*e1))+up(std::abs(v)*e2));
    return up(up(up(gamma7)*magnitude)+extra);
}
float move(float p,float normal,float distance){
    if(normal==0)return p;
    float value=p+normal*distance;
    return std::nextafter(value,normal>0?infinity:-infinity);
}
}
Point triangle_point(Float3 a,Float3 b,Float3 c,float u,float v,Float3 traversal_error) noexcept {
    Point result;
    if(!finite(a)||!finite(b)||!finite(c)||!finite(traversal_error)||
       traversal_error.x<0||traversal_error.y<0||traversal_error.z<0||
       !std::isfinite(u)||!std::isfinite(v)||u<0||v<0||u>1||v>1||u+v>1)return result;
    Float3 e1=sub(b,a),e2=sub(c,a);float scale=std::max(maximum(e1),maximum(e2));
    if(!finite(e1)||!finite(e2)||!(scale>0)||!std::isfinite(scale))return result;
    Float3 n=cross(mul(e1,1/scale),mul(e2,1/scale));float ns=maximum(n);
    if(!finite(n)||!(ns>0))return result;
    n=mul(n,1/ns);float length=std::sqrt(dot(n,n));n=mul(n,1/length);
    result.position={(a.x+u*e1.x)+v*e2.x,(a.y+u*e1.y)+v*e2.y,(a.z+u*e1.z)+v*e2.z};
    result.error={envelope(a.x,b.x,c.x,u,v,traversal_error.x),envelope(a.y,b.y,c.y,u,v,traversal_error.y),envelope(a.z,b.z,c.z,u,v,traversal_error.z)};
    result.normal=n;
    result.valid=finite(result.position)&&finite(result.error)&&finite(n)&&dot(n,n)>0;
    return result.valid?result:Point{};
}
Spawn offset(const Point& point,Float3 outgoing) noexcept {
    if(!point.valid||!finite(point.position)||!finite(point.error)||!finite(point.normal)||!finite(outgoing)||
       point.error.x<0||point.error.y<0||point.error.z<0)return {};
    float outgoing2=dot(outgoing,outgoing);
    if(!(outgoing2>0)||!std::isfinite(outgoing2))return {};
    Float3 n=point.normal;float n2=dot(n,n);if(!(n2>0)||!std::isfinite(n2))return {};
    if(dot(n,outgoing)<0)n=mul(n,-1);
    float projected=up(up(up(std::abs(n.x)*point.error.x)+up(std::abs(n.y)*point.error.y))+up(std::abs(n.z)*point.error.z));
    float distance=up(projected/n2);
    Float3 origin{move(point.position.x,n.x,distance),move(point.position.y,n.y,distance),move(point.position.z,n.z,distance)};
    return finite(origin)?Spawn{origin,true}:Spawn{};
}
} // namespace basix::ray_spawn
