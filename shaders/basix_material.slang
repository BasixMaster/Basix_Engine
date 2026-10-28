// SPDX-License-Identifier: MIT
#ifndef BASIX_MATERIAL_SLANG
#define BASIX_MATERIAL_SLANG
// Original float port of the CPU reference. All vectors use a local +Z normal.
// C-layout storage ABI: 48 bytes. Material IDs are scene metadata, not stored here.
struct BasixMaterial {
 float3 base;float roughness;float metallic;float3 emission;uint model;uint3 padding;
};
// C-layout storage ABI: 32 bytes; pdf describes continuous non-null probability.
struct BasixBsdfSample {float3 wi;float pdf;float3 f;uint valid;};
static const float BASIX_PI=3.14159265358979323846;
bool basixMaterialValid(BasixMaterial m) {
 return all(isfinite(m.base))&&all(m.base>=0)&&all(m.base<=1)&&isfinite(m.roughness)&&
  m.roughness>=.05f&&m.roughness<=1&&isfinite(m.metallic)&&m.metallic>=0&&m.metallic<=1&&
  all(isfinite(m.emission))&&all(m.emission>=0)&&m.model<=1;
}
bool basixDirectionValid(float3 v) {return all(isfinite(v))&&v.z>0&&abs(dot(v,v)-1)<1e-6f;}
bool basixUniformValid(float u) {return isfinite(u)&&u>=0&&u<1;}
float3 basixSchlick(float3 f0,float cosine) {
 float t=1-clamp(cosine,0.f,1.f);float fifth=t*t*t*t*t;return f0+(1-f0)*fifth;
}
float basixDistribution(float alpha,float3 h) {
 float a2=alpha*alpha,t=h.x*h.x+h.y*h.y+a2*h.z*h.z;
 return a2/(BASIX_PI*t*t);
}
float basixSmithDenominator(float alpha,float z) {return z+sqrt(alpha*alpha+(1-alpha*alpha)*z*z);}
float3 basixEvaluate(BasixMaterial m,float3 wo,float3 wi) {
 if(!basixMaterialValid(m)||!basixDirectionValid(wo)||!basixDirectionValid(wi))return float3(0);
 if(m.model==0)return m.base/BASIX_PI;
 float3 h=normalize(wo+wi);float alpha=m.roughness*m.roughness;
 float3 f0=.04*(1-m.metallic)+m.base*m.metallic;
 float3 spec=basixSchlick(f0,dot(wo,h))*(basixDistribution(alpha,h)/
  (basixSmithDenominator(alpha,wo.z)*basixSmithDenominator(alpha,wi.z)));
 float3 diffuse=m.base*((1-m.metallic)/BASIX_PI)*
  (1-basixSchlick(float3(.04),wo.z))*(1-basixSchlick(float3(.04),wi.z));
 return diffuse+spec;
}
float basixPdf(BasixMaterial m,float3 wo,float3 wi) {
 if(!basixMaterialValid(m)||!basixDirectionValid(wo)||!basixDirectionValid(wi))return 0;
 float diffuse=wi.z/BASIX_PI;if(m.model==0)return diffuse;
 float3 h=normalize(wo+wi);float projection=dot(wo,h);if(projection<=0)return 0;
 float spec=basixDistribution(m.roughness*m.roughness,h)*h.z/(4*projection);
 float p=.5+.5*m.metallic;return p*spec+(1-p)*diffuse;
}
BasixBsdfSample basixSample(BasixMaterial m,float3 wo,float uLobe,float u1,float u2) {
 BasixBsdfSample result=(BasixBsdfSample)0;
 if(!basixMaterialValid(m)||!basixDirectionValid(wo)||!basixUniformValid(uLobe)||
  !basixUniformValid(u1)||!basixUniformValid(u2))return result;
 float3 wi;float phi=2*BASIX_PI*u2;
 if(m.model==0||uLobe>=.5+.5*m.metallic) {
  float radius=sqrt(u1);wi=float3(radius*cos(phi),radius*sin(phi),sqrt(1-u1));
 } else {
  float alpha=m.roughness*m.roughness,tan2=alpha*alpha*u1/(1-u1);
  float z=1/sqrt(1+tan2),r=sqrt(tan2)*z;float3 h=float3(r*cos(phi),r*sin(phi),z);
  float projection=dot(wo,h);if(projection<=0)return result;
  wi=2*projection*h-wo;if(wi.z<=0)return result;wi=normalize(wi);
 }
 result.pdf=basixPdf(m,wo,wi);result.f=basixEvaluate(m,wo,wi);
 result.wi=wi;result.valid=result.pdf>0&&isfinite(result.pdf)&&all(isfinite(result.f))?1:0;
 return result;
}
#endif
