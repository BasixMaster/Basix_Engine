// SPDX-License-Identifier: MIT
#include "basix/transform_uncertainty.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace basix;
using render::Vec3;
using transform_uncertainty::analyze;
using transform_uncertainty::Domain;
int checks=0,failures=0;
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<name<<'\n';}}
template<class F>void rejects(F f,const char* name){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,name);}
reference::Scene triangle(){
 reference::Scene s;s.materials={{10,{.5,.5,.5},.5,0,{},render::MaterialModel::lambert}};
 s.triangles={{7,10,{0,0,0},{2,0,0},{0,2,0}}};return s;
}
instances::Batch bake(const reference::Scene& s,instances::Matrix m={1,0,0,0,0,1,0,0,0,0,1,0}){
 const instances::InputInstance i{101,1000,0,m};return instances::bake_instances(s,std::span(&i,1),1);
}
double maximum(Vec3 v){return std::max({v.x,v.y,v.z});}
bool finite_nonnegative(Vec3 v){return render::finite(v)&&v.x>=0&&v.y>=0&&v.z>=0;}
bool fp_values(Vec3 v){return double(float(v.x))==v.x&&double(float(v.y))==v.y&&double(float(v.z))==v.z;}
void anchors(){
 const auto s=triangle();const auto b=bake(s);const auto budget=analyze(s,b);const auto& q=budget.at(0);
 const double g=(7*0x1p-24)/(1-7*0x1p-24);
 check(budget.size()==1,"one budget per mapped world triangle");
 // Independent analytic identity anchors; outward interval noise must remain
 // insignificant, while no rounded bound may be below the exact formula.
 check(q.forward_policy.x>=2*g&&q.forward_policy.x<=2*g*(1+2e-7),"identity forward analytic bound");
 check(q.object_policy.x>=4*g&&q.object_policy.x<=4*g*(1+2e-7),"identity per-axis extent analytic bound");
 check(q.inverse_policy.x>=160*g&&q.inverse_policy.x<=160*g*(1+2e-7),"identity inverse ray-domain analytic bound");
 check(maximum(q.representation)<1e-14,"exact identity representation interval is near zero");
 check(q.total.x>=q.representation.x+q.forward_policy.x+q.object_policy.x+q.inverse_policy.x,"total upper bounds rounded component sum");
 check(q.total.z>0&&q.object_policy.z==0&&q.forward_policy.z==0,"flat triangle retains inverse z policy");
 check(fp_values(q.representation)&&fp_values(q.forward_policy)&&fp_values(q.object_policy)&&fp_values(q.inverse_policy)&&fp_values(q.total),"all stored components are exactly FP32");
 check(finite_nonnegative(q.representation)&&finite_nonnegative(q.forward_policy)&&finite_nonnegative(q.object_policy)&&finite_nonnegative(q.inverse_policy)&&finite_nonnegative(q.total),"all budget terms finite nonnegative");
 auto domain=analyze(s,b,Domain{64,256});check(domain[0].inverse_policy.x>q.inverse_policy.x,"inverse term grows with actual bounded ray domain");
 auto moved=s;for(auto& t:moved.triangles){t.a.x+=4096;t.b.x+=4096;t.c.x+=4096;}
 const auto cancel=analyze(moved,bake(moved,{1,0,0,-4096,0,1,0,0,0,0,1,0}));
 check(cancel[0].total.x>q.total.x*50,"large-object small-world cancellation grows uncertainty");
 check(cancel[0].forward_policy.x>=8194*g,"cancellation bound retains source and translation magnitudes");
 for(int exponent=-4;exponent<=3;++exponent){
  const double sx=std::ldexp(1.,exponent);const auto v=analyze(s,bake(s,{float(sx),0,0,.25f,0,1,0,0,0,0,1,0}))[0];
  // Exact powers of two make both inverse and cancellation residual zero.
  // These formulas are independent scalar evaluations, not matrix helpers.
  const double f=g*(2*sx+.25),o=g*4*sx,i=g*(160+.25);
  check(v.forward_policy.x>=f&&v.forward_policy.x<=f*(1+2e-7),"eight diagonal forward analytic anchors");
  check(v.object_policy.x>=o&&v.object_policy.x<=o*(1+2e-7),"eight diagonal extent analytic anchors");
  check(v.inverse_policy.x>=i&&v.inverse_policy.x<=i*(1+2e-7),"eight inverse round-trip analytic anchors");
 }
 // The largest singular stretch is in the unused normal axis, retaining
 // triangle resolution while independently anchoring the condition policy.
 check(analyze(s,bake(s,{1,0,0,0,0,1,0,0,0,0,1023,0})).size()==1,"condition1023 accepted below cutoff");
 // Binary fractions give exact object upload and exact affine coordinates.
 for(const auto m:std::array<instances::Matrix,4>{
   instances::Matrix{-1,.25f,0,1.25f,0,1,0,-.25f,.25f,0,1,0},
   instances::Matrix{0,0,1,0,0,1,0,0,-1,0,0,0},
   instances::Matrix{2,0,0,0,0,.5f,0,0,0,0,1,0},
   instances::Matrix{1,.5f,.25f,0,0,1,.25f,0,0,0,1,0}}){
   auto x=analyze(s,bake(s,m));check(x.size()==1&&finite_nonnegative(x[0].total),"rotation/reflection/scale/shear accepted");
 }
 auto room=reference::cornell_scene();room.spheres.clear();for(auto& t:room.triangles)for(auto* v:{&t.a,&t.b,&t.c})*v={float(v->x),float(v->y),float(v->z)};
 const std::array<instances::InputInstance,2> pair{{{10000123,1000,0,{1,0,0,-2,0,1,0,0,0,0,1,0}},{90000007,2000,0,{-1,.25f,0,2,0,1,0,0,.25f,0,1,0}}}};
 auto rb=instances::bake_instances(room,pair,1);auto budgets=analyze(room,rb);
 check(budgets.size()==room.triangles.size()*2,"default shared-room transforms accepted");
 bool good=true;for(const auto& v:budgets)good=good&&finite_nonnegative(v.total)&&fp_values(v.total);check(good,"every shared-room triangle has finite FP32 total");
 auto residual=s;residual.triangles[0].a.x=.1f;residual.triangles[0].b.x=2.1f;
 const auto rb2=bake(residual,{1.1f,.2f,0,.3f,0,1,0,0,0,0,1,0});auto res=analyze(residual,rb2);
 // Long-double independent residual lower anchor (on MSVC it aliases double;
 // this still validates nonzero representation, not exact certification).
 const long double exact=static_cast<long double>(1.1f)*static_cast<long double>(2.1f)+.3f;
 check(res[0].representation.x>=std::abs(static_cast<long double>(rb2.shader_scene.triangles[0].b.x)-exact),"representation encloses independent cancellation residual anchor");
}
void refusals(){
 const auto s=triangle();const auto good=bake(s);
 for(auto domain:std::array<Domain,8>{Domain{0,128},Domain{-1,128},Domain{32,64},Domain{32,0},Domain{32,std::numeric_limits<double>::infinity()},Domain{std::numeric_limits<double>::quiet_NaN(),128},Domain{0x1p21,0x1p22},Domain{32,0x1p21}})
  rejects([&]{analyze(s,good,domain);},"invalid or clipping-prone ray domain rejected");
 rejects([&]{analyze(s,good,Domain{std::nextafter(32.,0.),128});},"non-FP32 origin domain rejected");
 rejects([&]{analyze(s,good,Domain{32,std::nextafter(128.,256.)});},"non-FP32 distance domain rejected");
 auto bad=good;bad.matrices[0][0]=0;rejects([&]{analyze(s,bad);},"singular transform rejected");
 bad=good;bad.matrices[0][0]=1025;rejects([&]{analyze(s,bad);},"condition above1024 rejected");
 rejects([&]{analyze(s,bake(s,{1,0,0,0,0,1,0,0,0,0,1024,0}));},"exact condition boundary conservatively refused");
 bad=good;bad.matrices[0][0]=std::numeric_limits<float>::quiet_NaN();rejects([&]{analyze(s,bad);},"nonfinite coefficient rejected");
 bad=good;bad.matrices[0][3]=std::numeric_limits<float>::denorm_min();rejects([&]{analyze(s,bad);},"subnormal matrix input rejected");
 bad=good;bad.matrices[0][0]=0x1p21f;rejects([&]{analyze(s,bad);},"coefficient magnitude above policy rejected");
 bad=good;bad.matrices[0][0]=0x1p-21f;bad.matrices[0][5]=0x1p-21f;bad.matrices[0][10]=0x1p-21f;rejects([&]{analyze(s,bad);},"inverse coefficient above policy rejected");
 bad=good;bad.matrices[0][0]=0x1p-20f;bad.matrices[0][5]=0x1p-20f;bad.matrices[0][10]=0x1p-20f;bad.matrices[0][3]=2;rejects([&]{analyze(s,bad);},"inverse translation above policy rejected");
 auto mesh=s;mesh.triangles[0].a.z=std::numeric_limits<float>::denorm_min();rejects([&]{analyze(mesh,good);},"subnormal object input rejected");
 mesh=s;mesh.triangles[0].a.x=0x1p21;rejects([&]{analyze(mesh,good);},"object magnitude above policy rejected");
 mesh=s;mesh.triangles[0].a.x=1.+0x1p-30;rejects([&]{analyze(mesh,good);},"nonuploaded double mesh rejected");
 mesh=s;mesh.triangles[0].c=mesh.triangles[0].a;rejects([&]{analyze(mesh,good);},"degenerate source triangle rejected");
 bad=good;bad.shader_scene.triangles[0].b.x=3;rejects([&]{analyze(s,bad);},"forged baked geometry rejected");
 bad=good;bad.metadata[0].first_triangle=1;rejects([&]{analyze(s,bad);},"invalid dense range rejected");
 bad=good;bad.metadata[0].triangle_count=2;rejects([&]{analyze(s,bad);},"invalid dense count rejected");
 bad=good;bad.metadata[0].stable_id=0;rejects([&]{analyze(s,bad);},"zero stable metadata rejected");
 bad=good;bad.metadata[0].epoch=0;rejects([&]{analyze(s,bad);},"zero epoch metadata rejected");
 bad=good;bad.shader_scene.triangles[0].id=0;rejects([&]{analyze(s,bad);},"forged primitive metadata rejected");
 bad=good;bad.orientation_per_triangle[0]=0;rejects([&]{analyze(s,bad);},"forged orientation metadata rejected");
 bad=good;bad.reference_scene.triangles[0].b.x=3;rejects([&]{analyze(s,bad);},"forged reference geometry rejected");
 bad=good;bad.shader_scene.triangles[0].a.z=std::numeric_limits<double>::infinity();rejects([&]{analyze(s,bad);},"nonfinite world coordinate rejected");
 rejects([&]{analyze(s,bake(s,{1,0,0,30,0,1,0,0,0,0,1,0}));},"world spawn-margin violation rejected");
 mesh=s;for(auto& t:mesh.triangles){t.a.x+=0x1p19;t.b.x+=0x1p19;t.c.x+=0x1p19;}
 rejects([&]{analyze(mesh,bake(mesh,{1,0,0,-0x1p19f,0,1,0,0,0,0,1,0}));},"severe cancellation rejected by resolution policy");
 mesh=s;mesh.triangles[0].b.x=0x1p-16f;mesh.triangles[0].c.y=0x1p-16f;
 rejects([&]{analyze(mesh,bake(mesh));},"tiny triangle rejected by altitude budget policy");
}
}
int main(){try{anchors();refusals();}catch(const std::exception& e){std::cerr<<"Unexpected: "<<e.what()<<'\n';return 1;}std::cout<<"Transform uncertainty: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
