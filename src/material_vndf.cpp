// SPDX-License-Identifier: MIT
#include "basix/material_vndf.hpp"
#include <numbers>

// Original implementation of projected-disk visible-GGX sampling mathematics:
// Heitz 2018, https://jcgt.org/published/0007/04/01/
// and microfacet change of variables described in PBRT 4, section 9.6.
// No source implementation is incorporated. Separable Smith matches Basix's
// existing physical BRDF; VNDF changes the proposal, not that masking model.
namespace basix::render {
namespace {
constexpr double pi=std::numbers::pi;
constexpr Vec3 one{1,1,1};
bool direction(Vec3 v){return finite(v)&&v.z>0&&std::abs(dot(v,v)-1)<1e-6;}
bool uniform(double u){return std::isfinite(u)&&u>=0&&u<1;}
Vec3 schlick(Vec3 f0,double c){double t=1-std::clamp(c,0.,1.);return f0+(one-f0)*(t*t*t*t*t);}
double smith(double a,double z){return z+std::sqrt(a*a+(1-a*a)*z*z);}
double distribution(double a,Vec3 h){double t=h.x*h.x+h.y*h.y+a*a*h.z*h.z;return a*a/(pi*t*t);}
Vec3 diffuse(const Material&m,Vec3 wo,Vec3 wi){
 return m.base_color*((1-m.metallic)/pi)*(one-schlick({.04,.04,.04},wo.z))*(one-schlick({.04,.04,.04},wi.z));
}
Vec3 state_f(const Material&m,Vec3 wo,Vec3 wi,Vec3 h,double d){
 double a=m.roughness*m.roughness;
 Vec3 f0=Vec3{.04,.04,.04}*(1-m.metallic)+m.base_color*m.metallic;
 return diffuse(m,wo,wi)+schlick(f0,dot(wo,h))*(d/(smith(a,wo.z)*smith(a,wi.z)));
}
double state_pdf(const Material&m,Vec3 wo,Vec3 wi,double d){
 double p=.5+.5*m.metallic,a=m.roughness*m.roughness;
 // D*G1(wo)/(4*wo.z) with cosines cancelled before arithmetic.
 return p*d/(2*smith(a,wo.z))+(1-p)*wi.z/pi;
}
}
Vec3 evaluate_visible(const Material&m,Vec3 wo,Vec3 wi) noexcept {
 if(!validate(m)||!direction(wo)||!direction(wi))return {};
 return evaluate(m,normalize(wo),normalize(wi));
}
double pdf_visible(const Material&m,Vec3 wo,Vec3 wi) noexcept {
 if(!validate(m)||!direction(wo)||!direction(wi))return 0;
 wo=normalize(wo);wi=normalize(wi);if(m.model==MaterialModel::lambert)return wi.z/pi;
 Vec3 h=normalize(wo+wi);if(dot(wo,h)<=0)return 0;
 return state_pdf(m,wo,wi,distribution(m.roughness*m.roughness,h));
}
VisibleSample sample_visible(const Material&m,Vec3 wo,double ul,double u1,double u2) noexcept {
 if(!validate(m)||!direction(wo)||!uniform(ul)||!uniform(u1)||!uniform(u2))return {};
 wo=normalize(wo);Vec3 wi,h{};double phi=2*pi*u2;
 if(m.model==MaterialModel::lambert||ul>=.5+.5*m.metallic){
  double r=std::sqrt(u1);wi={r*std::cos(phi),r*std::sin(phi),std::sqrt(1-u1)};
  if(m.model!=MaterialModel::lambert)h=normalize(wo+wi);
 }else{
  double a=m.roughness*m.roughness;
  Vec3 stretched=normalize({a*wo.x,a*wo.y,wo.z});
  double xy=stretched.x*stretched.x+stretched.y*stretched.y;
  Vec3 tangent=xy>0?Vec3{-stretched.y,stretched.x,0}/std::sqrt(xy):Vec3{1,0,0};
  Vec3 bitangent=cross(stretched,tangent);
  double r=std::sqrt(u1),x=r*std::cos(phi),y=r*std::sin(phi);
  double blend=.5*(1+stretched.z);
  // The original disk satisfies x*x+disk_y*disk_y=u1. Compute its
  // orthogonal radius without subtracting nearly equal numbers at the rim.
  // Factor 1-x*x-y*y=(c-y)*(c+y), and rationalize the small disk factor.
  // This is the same exact-real mapping, with no cutoff or variate changes.
  double disk_y=y,c=std::sqrt((1-u1)+disk_y*disk_y);
  double minus=disk_y>=0?(1-u1)/(c+disk_y):c-disk_y;
  double plus=disk_y<0?(1-u1)/(c-disk_y):c+disk_y;
  y=(1-blend)*c+blend*disk_y;
  double z=std::sqrt(std::max(0.,blend*minus*(2*(1-blend)*c+blend*plus)));
  Vec3 hemisphere=tangent*x+bitangent*y+stretched*z;
  h=normalize({a*hemisphere.x,a*hemisphere.y,std::max(0.,hemisphere.z)});
  double projection=dot(wo,h);if(projection<=0)return {};
  wi=h*(2*projection)-wo;if(wi.z<=0)return {};
  wi=normalize(wi);
 }
 if(m.model==MaterialModel::lambert)return {wi,m.base_color/pi,wi.z/pi,true,{},0};
 double d=distribution(m.roughness*m.roughness,h);
 Vec3 f=state_f(m,wo,wi,h,d);double p=state_pdf(m,wo,wi,d);
 if(!finite(wi)||!finite(f)||!std::isfinite(p)||p<=0)return {};
 return {wi,f,p,true,h,dot(wo,h)};
}
} // namespace basix::render
