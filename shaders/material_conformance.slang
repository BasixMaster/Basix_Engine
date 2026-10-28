// SPDX-License-Identifier: MIT
// Float GPU port of the bounded CPU reference; not an independent physics oracle.
#include <NoGraphicsAPI/shader.slang>
struct MaterialCase { float4 baseRough; float4 woMetal; float3 wi; uint model; };
struct MaterialResult { float3 f; float density; };
struct Root { MaterialCase* cases; MaterialResult* results; uint count; uint3 padding; };
GPU_ROOT(Root,root);
float3 schlick(float3 f0,float c) {
 float t=1-clamp(c,0.f,1.f);float t5=t*t*t*t*t;return f0+(1-f0)*t5;
}
[numthreads(64,1,1)]
void computeMain(uint3 id:SV_DispatchThreadID) {
 if(id.x>=root.count)return;
 MaterialCase c=root.cases[id.x];MaterialResult r;
 const float pi=3.14159265358979323846;
 float3 wo=c.woMetal.xyz,wi=c.wi;float3 base=c.baseRough.xyz;
 if(c.model==0) {r.f=base/pi;r.density=wi.z/pi;}
 else {
  float roughness=c.baseRough.w,metal=c.woMetal.w;
  float alpha=roughness*roughness,a2=alpha*alpha;
  float3 h=normalize(wo+wi);
  float denominator=h.x*h.x+h.y*h.y+a2*h.z*h.z;
  float D=a2/(pi*denominator*denominator);
  float smithO=wo.z+sqrt(a2+(1-a2)*wo.z*wo.z);
  float smithI=wi.z+sqrt(a2+(1-a2)*wi.z*wi.z);
  float3 f0=.04*(1-metal)+base*metal;
  float3 spec=schlick(f0,dot(wo,h))*(D/(smithO*smithI));
  float3 diffuse=base*((1-metal)/pi)*(1-schlick(float3(.04),wo.z))*(1-schlick(float3(.04),wi.z));
  r.f=diffuse+spec;
  float p=.5+.5*metal;
  r.density=p*(D*h.z/(4*dot(wo,h)))+(1-p)*wi.z/pi;
 }
 root.results[id.x]=r;
}
