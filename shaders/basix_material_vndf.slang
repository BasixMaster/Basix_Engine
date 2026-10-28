// SPDX-License-Identifier: MIT
#ifndef BASIX_MATERIAL_VNDF_SLANG
#define BASIX_MATERIAL_VNDF_SLANG
#include "basix_material.slang"
// Original projected-disk visible-GGX implementation. See Heitz 2018,
// https://jcgt.org/published/0007/04/01/ . This is a separate proposal identity.
// C layout: 48 bytes. Generated half-vector state is retained for transport;
// reconstructed evaluation of a rounded wi is a separate diagnostic.
struct BasixVisibleSample {
 float3 wi;float pdf;float3 f;uint valid;float3 half;float projection;
};
float3 basixVisibleDiffuse(BasixMaterial m,float3 wo,float3 wi) {
 return m.base*((1-m.metallic)/BASIX_PI)*(1-basixSchlick(float3(.04),wo.z))*(1-basixSchlick(float3(.04),wi.z));
}
float3 basixEvaluateVisible(BasixMaterial m,float3 wo,float3 wi) {
 if(!basixMaterialValid(m)||!basixDirectionValid(wo)||!basixDirectionValid(wi))return float3(0);
 return basixEvaluate(m,normalize(wo),normalize(wi));
}
float basixVisibleStatePdf(BasixMaterial m,float3 wo,float3 wi,float d) {
 float p=.5+.5*m.metallic,a=m.roughness*m.roughness;
 return p*d/(2*basixSmithDenominator(a,wo.z))+(1-p)*wi.z/BASIX_PI;
}
float basixPdfVisible(BasixMaterial m,float3 wo,float3 wi) {
 if(!basixMaterialValid(m)||!basixDirectionValid(wo)||!basixDirectionValid(wi))return 0;
 wo=normalize(wo);wi=normalize(wi);if(m.model==0)return wi.z/BASIX_PI;
 float3 h=normalize(wo+wi);if(dot(wo,h)<=0)return 0;
 return basixVisibleStatePdf(m,wo,wi,basixDistribution(m.roughness*m.roughness,h));
}
BasixVisibleSample basixSampleVisible(BasixMaterial m,float3 wo,float uLobe,float u1,float u2) {
 BasixVisibleSample result=(BasixVisibleSample)0;
 if(!basixMaterialValid(m)||!basixDirectionValid(wo)||!basixUniformValid(uLobe)||!basixUniformValid(u1)||!basixUniformValid(u2))return result;
 wo=normalize(wo);float3 wi,h=float3(0);float phi=2*BASIX_PI*u2;
 if(m.model==0||uLobe>=.5+.5*m.metallic) {
  float r=sqrt(u1);wi=float3(r*cos(phi),r*sin(phi),sqrt(1-u1));
  if(m.model!=0)h=normalize(wo+wi);
 } else {
  float a=m.roughness*m.roughness;
  float3 stretched=normalize(float3(a*wo.x,a*wo.y,wo.z));
  float xy=stretched.x*stretched.x+stretched.y*stretched.y;
  float3 tangent=xy>0?float3(-stretched.y,stretched.x,0)/sqrt(xy):float3(1,0,0);
  float3 bitangent=cross(stretched,tangent);
  float r=sqrt(u1),x=r*cos(phi),y=r*sin(phi),blend=.5*(1+stretched.z);
  // The disk radius squared is u1. Factor the hemisphere height and
  // rationalize its small disk factor instead of subtracting at the rim.
  // Exact-real mapping and all variates are unchanged; no cutoff is added.
  float diskY=y,c=sqrt((1-u1)+diskY*diskY);
  float minus=diskY>=0?(1-u1)/(c+diskY):c-diskY;
  float plus=diskY<0?(1-u1)/(c-diskY):c+diskY;
  y=(1-blend)*c+blend*diskY;
  float z=sqrt(max(0.f,blend*minus*(2*(1-blend)*c+blend*plus)));
  float3 hemisphere=tangent*x+bitangent*y+stretched*z;
  h=normalize(float3(a*hemisphere.x,a*hemisphere.y,max(0.f,hemisphere.z)));
  float projection=dot(wo,h);if(projection<=0)return result;
  wi=2*projection*h-wo;if(wi.z<=0)return result;
  wi=normalize(wi);
 }
 if(m.model==0) {
  result.wi=wi;result.f=m.base/BASIX_PI;result.pdf=wi.z/BASIX_PI;result.valid=1;return result;
 }
 float a=m.roughness*m.roughness,d=basixDistribution(a,h);
 float3 f0=.04*(1-m.metallic)+m.base*m.metallic;
 result.f=basixVisibleDiffuse(m,wo,wi)+basixSchlick(f0,dot(wo,h))*(d/(basixSmithDenominator(a,wo.z)*basixSmithDenominator(a,wi.z)));
 result.pdf=basixVisibleStatePdf(m,wo,wi,d);result.wi=wi;result.half=h;result.projection=dot(wo,h);
 result.valid=result.pdf>0&&isfinite(result.pdf)&&all(isfinite(result.f))&&all(isfinite(wi))?1:0;
 if(result.valid==0)return (BasixVisibleSample)0;
 return result;
}
#endif
