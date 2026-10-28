// SPDX-License-Identifier: MIT
// Independent integral/analytic checks for the distinct visible-normal candidate.
// Legacy NDF tests and frozen GPU controls are intentionally untouched.
#include "basix/material_vndf.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>

using namespace basix::render;
namespace {
constexpr double pi=std::numbers::pi;
int checks=0,failures=0;
void check(bool b,const char* name){++checks;if(!b){++failures;std::cerr<<"FAIL: "<<name<<'\n';}}
bool near(double a,double b,double e=1e-10){return std::abs(a-b)<=e;}
bool near(Vec3 a,Vec3 b,double e=1e-10){return near(a.x,b.x,e)&&near(a.y,b.y,e)&&near(a.z,b.z,e);}
Vec3 hemi(double z,double phi){double r=std::sqrt(1-z*z);return {r*std::cos(phi),r*std::sin(phi),z};}
struct Random{std::mt19937_64 engine{0x7e27b931};double next(){return double(engine()>>11)*0x1.0p-53;}};
Material conductor(double rough=1){return {1,{1,1,1},rough,1,{},MaterialModel::metallic_roughness};}
bool zero(const VisibleSample& s){return !s.valid&&near(s.wi,{})&&near(s.f,{})&&s.pdf==0&&near(s.half,{})&&s.projection==0;}
// Midpoint solid-angle integration, dOmega=dz*dPhi. Neither sampler nor its
// change-of-variables implementation supplies the integration distribution.
template<class F> double integral(F f,int nz=256,int np=512){
 double sum=0;for(int z=0;z<nz;++z)for(int p=0;p<np;++p)sum+=f(hemi((z+.5)/nz,2*pi*(p+.5)/np));
 return sum*(2*pi/(double(nz)*np));
}
struct Estimate{double mass=0;Vec3 energy{};std::array<unsigned,32> bins{};};
Estimate estimate(const Material& m,Vec3 wo,unsigned count=262144){
 Random random;Estimate result;
 for(unsigned i=0;i<count;++i){const double ul=random.next(),u1=random.next(),u2=random.next();const auto s=sample_visible(m,wo,ul,u1,u2);
  if(s.valid){result.mass+=1;result.energy=result.energy+s.f*(s.wi.z/s.pdf);
   double phi=std::atan2(s.wi.y,s.wi.x);if(phi<0)phi+=2*pi;
   const auto z=std::min(3u,unsigned(s.wi.z*4)),p=std::min(7u,unsigned(phi*8/(2*pi)));++result.bins[z*8+p];}}
 result.mass/=count;result.energy=result.energy/count;return result;
}
void analytic_endpoints(){
 Material lambert{1,{.2,.4,.8},.3,0,{},MaterialModel::lambert};
 check(near(integral([&](Vec3 wi){return pdf_visible(lambert,{0,0,1},wi);}),1,1e-12),"Lambert PDF integral is one");
 Random random;bool exact=true;
 for(int i=0;i<4096;++i){const double ul=random.next(),u1=random.next(),u2=random.next();auto s=sample_visible(lambert,hemi(.00001,.8),ul,u1,u2);
  exact&=s.valid&&near(s.f*(s.wi.z/s.pdf),lambert.base_color,1e-14)&&near(s.half,{})&&s.projection==0;}
 check(exact,"Lambert samples retain exact albedo weight and no microfacet state");
 // For alpha=1 and F=1: D=1/pi, specPDF=1/[2*pi*(1+wo.z)].
 // Hemisphere mass=1/(1+wo.z), energy=2*(1-ln2)/(1+wo.z).
 // These closed forms include null probability; nulls are never resampled.
 for(double z:{1.,.5,.01,.00001}){
  const auto wo=hemi(z,.7);const auto m=conductor();const double mass=1/(1+z),energy=2*(1-std::log(2.))/(1+z);
  const auto measured=estimate(m,wo);
  check(near(measured.mass,mass,.005),"alpha1 conductor empirical non-null mass matches independent exact formula");
  check(near(measured.energy,{energy,energy,energy},.005),"alpha1 conductor empirical energy matches independent exact formula");
  // The analytic alpha1 PDF is constant over the positive hemisphere, even
  // for oblique wo. Equal-solid-angle bins therefore have equal populations.
  double count=0;for(auto n:measured.bins)count+=n;const double expected=count/32;double chi=0;
  for(auto n:measured.bins)chi+=(n-expected)*(n-expected)/expected;
  check(chi<100,"alpha1 accepted directions match independent uniform-hemisphere binned distribution");
  check(near(integral([&](Vec3 wi){return pdf_visible(m,wo,wi);},512,16),mass,1e-12),"alpha1 PDF hemisphere integral matches exact mass");
  check(near(integral([&](Vec3 wi){return evaluate_visible(m,wo,wi).x*wi.z;},1024,16),energy,3e-7),"alpha1 BRDF integral matches exact energy");
 }
}
void independent_integrals(){
 for(double metallic:{0.,.4,1.})for(double z:{1.,.2}){
  Material m{1,{.7,.4,.2},.6,metallic,{},MaterialModel::metallic_roughness};const auto wo=hemi(z,.3);
  const double mass=integral([&](Vec3 wi){return pdf_visible(m,wo,wi);});
  Vec3 energy{};for(unsigned c=0;c<3;++c){double value=integral([&](Vec3 wi){const auto f=evaluate_visible(m,wo,wi);return (c==0?f.x:c==1?f.y:f.z)*wi.z;});if(c==0)energy.x=value;else if(c==1)energy.y=value;else energy.z=value;}
  const double finer=integral([&](Vec3 wi){return evaluate_visible(m,wo,wi).x*wi.z;},512,1024);
  check(near(energy.x,finer,2e-4),"independent hemisphere energy quadrature converges before Monte Carlo comparison");
  check(mass>0&&mass<=1.0001&&max_component(energy)<=1.0001,"visible proposal mass and white-environment energy stay within physical bounds");
  const auto measured=estimate(m,wo);
  check(near(measured.mass,mass,.005),"sampled null mass agrees with independent hemisphere PDF integral");
  check(near(measured.energy,energy,.008),"sampled energy agrees with independent hemisphere BRDF integral");
 }
}
void state_and_reciprocity(){
 Random random;bool state=true,bound=true,tuple=true,reciprocal=true,unit_semantics=true;
 for(int i=0;i<20000;++i){
  const double rough=.05+.95*random.next(),metal=random.next();Material m{1,{.7,.4,.2},rough,metal,{},MaterialModel::metallic_roughness};
  const auto wo=hemi(.05+.95*random.next(),2*pi*random.next()),wi=hemi(.05+.95*random.next(),2*pi*random.next());
  reciprocal&=near(evaluate_visible(m,wo,wi),evaluate_visible(m,wi,wo),1e-8);
  const double ul=random.next(),u1=random.next(),u2=random.next();auto s=sample_visible(m,wo,ul,u1,u2);
  if(s.valid){
   state&=near(dot(s.half,s.half),1,1e-12)&&s.half.z>=0&&near(s.projection,dot(wo,s.half),1e-12)&&s.projection>0&&near(normalize(s.half*(2*s.projection)-wo),s.wi,1e-11);
   // Ordinary double tuples only. Rounded float tuples are reported separately.
   tuple&=near(s.f,evaluate_visible(m,wo,s.wi),1e-7*std::max(1.,max_component(s.f)))&&near(s.pdf,pdf_visible(m,wo,s.wi),1e-8*std::max(1.,s.pdf));
  }
  const auto scaled=wo*(1+1e-8);const auto canonical=normalize(scaled);auto a=sample_visible(m,scaled,ul,u1,u2),b=sample_visible(m,canonical,ul,u1,u2);
  unit_semantics&=a.valid==b.valid&&(!a.valid||(near(a.wi,b.wi,1e-11)&&near(a.f,b.f,1e-7*std::max(1.,max_component(b.f)))&&near(a.pdf,b.pdf,1e-8*std::max(1.,b.pdf))));
  auto pure=conductor(rough);pure.base_color={.8,.5,.2};const auto grazing=hemi(i%2?1e-5:.1,2*pi*random.next());
  auto p=sample_visible(pure,grazing,0,u1,u2);if(p.valid){
   const double alpha=rough*rough,z=p.wi.z;
   const double g1=2*z/(z+std::sqrt(alpha*alpha+(1-alpha*alpha)*z*z));
   const double t=1-p.projection,fifth=t*t*t*t*t;const auto fresnel=pure.base_color+(Vec3{1,1,1}-pure.base_color)*fifth;
   const auto weight=p.f*(z/p.pdf);
   bound&=near(weight,fresnel*g1,1e-10)&&weight.x>=0&&weight.y>=0&&weight.z>=0&&max_component(weight)<=1+1e-12;
  }
 }
 check(state,"sampled half state reconstructs reflection and retains projection");
 check(tuple,"ordinary double sample f/PDF agrees tightly with standalone tuple evaluation");
 check(reciprocal,"visible candidate preserves physical BRDF reciprocity");
 check(unit_semantics,"accepted near-unit input is interpreted through canonical normalization");
 check(bound,"pure-conductor sampled weight independently equals F times G1(wi) and is bounded by one");
}
void boundaries_and_diagnostic(){
 const auto m=conductor(.05);const double nan=std::numeric_limits<double>::quiet_NaN();
 for(Vec3 wo:std::array<Vec3,5>{{{0,0,0},{0,0,-1},{0,0,2},{1,0,0},{nan,0,1}}}){
  check(zero(sample_visible(m,wo,.1,.2,.3)),"invalid/horizon view direction yields a zero null sample");
  check(pdf_visible(m,wo,{0,0,1})==0&&near(evaluate_visible(m,wo,{0,0,1}),{}),"invalid/horizon view direction has zero standalone response");
 }
 for(double u:{-.01,1.,nan,std::numeric_limits<double>::infinity()}){
  check(zero(sample_visible(m,{0,0,1},u,.2,.3))&&zero(sample_visible(m,{0,0,1},.2,u,.3))&&zero(sample_visible(m,{0,0,1},.2,.3,u)),"invalid uniform variates are rejected without resampling");
 }
 auto invalid=m;invalid.roughness=.049;check(zero(sample_visible(invalid,{0,0,1},0,.2,.3)),"roughness domain unchanged");
 auto horizon=sample_visible(conductor(),{0,0,1},0,.5,0);
 check(!horizon.valid||horizon.wi.z<1e-12,"exact reflection horizon remains a boundary classification, not resampled");
 // Diagnostic, not a pass/fail substitute for sampled-state or legacy tests.
 double worst=0;unsigned valid=0;Random random;
 for(int i=0;i<4096;++i){const double phi=2*pi*random.next(),u1=random.next(),u2=random.next();auto s=sample_visible(m,hemi(1e-5,phi),0,u1,u2);if(!s.valid)continue;
  const Vec3 q{float(s.wi.x),float(s.wi.y),float(s.wi.z)};const auto wo=normalize(s.half*(2*s.projection)-s.wi);const double density=pdf_visible(m,wo,q);
  if(density>0){worst=std::max(worst,std::abs(density/s.pdf-1));++valid;}}
 std::cout<<"DIAGNOSTIC only: rounded-float standalone versus sampled-state PDF max relative="<<worst<<" across "<<valid<<" non-null cases; not legacy NDF acceptance\n";
}
}
int main(){analytic_endpoints();independent_integrals();state_and_reciprocity();boundaries_and_diagnostic();std::cout<<"CPU visible-normal candidate: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
