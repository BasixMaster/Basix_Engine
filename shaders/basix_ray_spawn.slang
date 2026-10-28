// SPDX-License-Identifier: MIT
#ifndef BASIX_RAY_SPAWN_SLANG
#define BASIX_RAY_SPAWN_SLANG
// Original error-box projection implementation. Its explicit traversalError
// input is a caller policy, not a claimed universal Vulkan hardware bound.
struct BasixRayPoint {float3 position;float3 error;float3 normal;uint valid;};
struct BasixRaySpawn {float3 origin;uint valid;};
float basixRayNext(float value,bool positive){
 if(value==0)return asfloat(positive?1u:0x80000001u);
 uint bits=asuint(value);bool increment=(value>0)==positive;
 return asfloat(increment?bits+1u:bits-1u);
}
float basixRayUp(float value){return basixRayNext(value,true);}
float basixRayEnvelope(float a,float b,float c,float u,float v,float extra){
 const float eps=0.000000059604644775390625f;
 const float gamma7=(7*eps)/(1-7*eps);
 float e1=basixRayUp(abs(b)+abs(a)),e2=basixRayUp(abs(c)+abs(a));
 float magnitude=basixRayUp(basixRayUp(abs(a)+basixRayUp(abs(u)*e1))+basixRayUp(abs(v)*e2));
 return basixRayUp(basixRayUp(basixRayUp(gamma7)*magnitude)+extra);
}
BasixRayPoint basixTrianglePoint(float3 a,float3 b,float3 c,float u,float v,float3 traversalError){
 BasixRayPoint result=(BasixRayPoint)0;
 if(!all(isfinite(a))||!all(isfinite(b))||!all(isfinite(c))||!all(isfinite(traversalError))||
    any(traversalError<0)||!isfinite(u)||!isfinite(v)||u<0||v<0||u>1||v>1||u+v>1)return result;
 float3 e1=b-a,e2=c-a;float scale=max(max(max(abs(e1.x),abs(e1.y)),abs(e1.z)),max(max(abs(e2.x),abs(e2.y)),abs(e2.z)));
 if(!all(isfinite(e1))||!all(isfinite(e2))||!(scale>0)||!isfinite(scale))return result;
 float3 n=cross(e1/scale,e2/scale);float ns=max(max(abs(n.x),abs(n.y)),abs(n.z));
 if(!all(isfinite(n))||!(ns>0))return result;
 n/=ns;n/=sqrt(dot(n,n));
 precise float3 first=a+u*e1;
 precise float3 p=first+v*e2;
 result.position=p;result.normal=n;
 result.error=float3(basixRayEnvelope(a.x,b.x,c.x,u,v,traversalError.x),basixRayEnvelope(a.y,b.y,c.y,u,v,traversalError.y),basixRayEnvelope(a.z,b.z,c.z,u,v,traversalError.z));
 result.valid=all(isfinite(p))&&all(isfinite(result.error))&&all(isfinite(n))&&dot(n,n)>0?1:0;
 if(result.valid==0)return (BasixRayPoint)0;return result;
}
float basixRayMove(float p,float normal,float distance){
 if(normal==0)return p;
 precise float value=p+normal*distance;
 return basixRayNext(value,normal>0);
}
BasixRaySpawn basixSpawnOrigin(BasixRayPoint point,float3 outgoing){
 BasixRaySpawn result=(BasixRaySpawn)0;
 if(point.valid==0||!all(isfinite(point.position))||!all(isfinite(point.error))||!all(isfinite(point.normal))||
    !all(isfinite(outgoing))||any(point.error<0))return result;
 float outgoing2=dot(outgoing,outgoing);
 if(!(outgoing2>0)||!isfinite(outgoing2))return result;
 float3 n=point.normal;float n2=dot(n,n);if(!(n2>0)||!isfinite(n2))return result;
 if(dot(n,outgoing)<0)n=-n;
 float projected=basixRayUp(basixRayUp(basixRayUp(abs(n.x)*point.error.x)+basixRayUp(abs(n.y)*point.error.y))+basixRayUp(abs(n.z)*point.error.z));
 float distance=basixRayUp(projected/n2);
 result.origin=float3(basixRayMove(point.position.x,n.x,distance),basixRayMove(point.position.y,n.y,distance),basixRayMove(point.position.z,n.z,distance));
 result.valid=all(isfinite(result.origin))?1:0;
 if(result.valid==0)return (BasixRaySpawn)0;return result;
}
#endif
