// SPDX-License-Identifier: MIT
#include "basix/ray_spawn.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>

using namespace basix::ray_spawn;
namespace {
int checks=0,failures=0;
void check(bool value,const char* name){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<name<<'\n';}}
struct D3 {long double x,y,z;};
D3 d(Float3 v){return {v.x,v.y,v.z};}
D3 operator-(D3 a,D3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
D3 operator+(D3 a,D3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
D3 operator*(D3 a,long double s){return {a.x*s,a.y*s,a.z*s};}
D3 cross(D3 a,D3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
long double dot(D3 a,D3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool enclosed(const Point& p,Float3 a,Float3 b,Float3 c,float u,float v){
 D3 exact=d(a)+(d(b)-d(a))*u+(d(c)-d(a))*v;
 return p.valid&&std::abs(exact.x-p.position.x)<=p.error.x&&std::abs(exact.y-p.position.y)<=p.error.y&&std::abs(exact.z-p.position.z)<=p.error.z;
}
void planes(){
 for(float scale:{1e-6f,1e-3f,1.f,1e3f,1e6f}){
  for(float sign:{-1.f,1.f}){
   Float3 a{-.5f*scale,-.5f*scale,sign*.25f*scale};
   Float3 b{.5f*scale,-.5f*scale,sign*.25f*scale};
   Float3 c{0,.5f*scale,sign*.25f*scale};
   const float e=4.2e-7f*2*scale;auto p=triangle_point(a,b,c,.25f,.5f,{e,e,e});
   auto front=offset(p,{.3f,.2f,1}),back=offset(p,{.3f,.2f,-1});
   D3 n=cross(d(b)-d(a),d(c)-d(a));
   check(enclosed(p,a,b,c,.25f,.5f),"triangle reconstruction enclosed by independent high precision point");
   check(front.valid&&back.valid&&dot(d(front.origin)-d(a),n)>0&&dot(d(back.origin)-d(a),n)<0,"spawned origins on requested exact geometric plane sides");
   const float gap=2e-6f*scale;
   check(front.origin.z-a.z<gap&&a.z-back.origin.z<gap,"bounded origin stays before scoped thin adjacent plane");
   const double old=2e-5*std::max(1.,double(std::max({std::abs(p.position.x),std::abs(p.position.y),std::abs(p.position.z)})));
   check(old>gap,"legacy global floor crosses the thin-gap control");
  }
 }
 // General oblique planes and non-central barycentrics. Neither the test plane
 // nor exact reconstructed position uses the helper's normal/error calculation.
 std::mt19937 rng(0x2833a1u);std::uniform_real_distribution<float> coord(-4,4),uniform(0,1);
 bool envelope=true,oriented=true,nontrivial=true;
 for(int i=0;i<12000;++i){
  float scale=std::pow(10.f,float(i%13-6));
  Float3 a{coord(rng)*scale,coord(rng)*scale,coord(rng)*scale};
  Float3 b{coord(rng)*scale,coord(rng)*scale,coord(rng)*scale};
  Float3 c{coord(rng)*scale,coord(rng)*scale,coord(rng)*scale};
  float u=uniform(rng)*.9f,v=uniform(rng)*(1-u)*.9f;
  auto p=triangle_point(a,b,c,u,v);
  if(!p.valid){nontrivial=false;continue;}
  envelope=envelope&&enclosed(p,a,b,c,u,v);
  auto f=offset(p,p.normal),r=offset(p,{-p.normal.x,-p.normal.y,-p.normal.z});
  auto n=cross(d(b)-d(a),d(c)-d(a));
  oriented=oriented&&f.valid&&r.valid&&dot(d(f.origin)-d(a),n)>0&&dot(d(r.origin)-d(a),n)<0;
 }
 check(nontrivial,"all nondegenerate randomized fixtures constructed");
 check(envelope,"12000 randomized reconstructions enclosed by independent high precision calculation");
 check(oriented,"12000 random planes place front and back origins on exact requested side");
}
void local_coordinates(){
 bool preserved=true;
 for(double world:{-16777216.,-65536.,0.,65536.,16777216.}){
  // Source geometry exists as doubles; subtract the common origin before FP32
  // conversion. These exactly repeated local fixtures are intentional.
  auto local=[&](double x,double y,double z){return Float3{float((world+x)-world),float((world+y)-world),float((world+z)-world)};};
  auto a=local(-.5,-.5,0),b=local(.5,-.5,0),c=local(0,.5,0);
  auto p=triangle_point(a,b,c,.25f,.5f,{8.4e-7f,8.4e-7f,8.4e-7f});
  auto spawned=offset(p,{0,0,1});
  float gap=float((world+1e-5)-world);
  preserved=preserved&&spawned.valid&&spawned.origin.z>0&&spawned.origin.z<gap;
 }
 check(preserved,"double world rebase preserves representable thin separation through positive and negative2^24 translations");
 float w=16777216.f;auto world=triangle_point({w,w,w},{w+4,w,w},{w,w+4,w},.25f,.25f);
 auto spawned=offset(world,{0,0,1});
 check(spawned.valid&&spawned.origin.z-w>4,"unrebased error envelope explicitly cannot preserve arbitrary small world gaps");
}
void invalid_and_state(){
 Float3 a{0,0,0},b{1,0,0},c{0,1,0};auto p=triangle_point(a,b,c,.25f,.25f);
 auto z=offset(p,{0,0,1}),neg=offset(p,{0,0,-1});
 check(z.valid&&neg.valid&&z.origin.z>0&&neg.origin.z<0,"signed-zero surface receives outward representable displacement");
 check(z.origin.x==p.position.x&&z.origin.y==p.position.y,"zero normal components stay unchanged");
 check(!triangle_point(a,a,c,.25f,.25f).valid,"degenerate triangle rejected");
 check(!triangle_point(a,b,c,-.01f,.25f).valid,"negative barycentric rejected");
 check(!triangle_point(a,b,c,.8f,.8f).valid,"outside barycentrics rejected");
 check(!triangle_point(a,b,c,.2f,.2f,{-1,0,0}).valid,"negative traversal allowance rejected");
 float inf=std::numeric_limits<float>::infinity(),nan=std::numeric_limits<float>::quiet_NaN();
 check(!triangle_point({inf,0,0},b,c,.2f,.2f).valid,"infinite vertex rejected");
 check(!triangle_point(a,b,c,nan,.2f).valid,"NaN barycentric rejected");
 check(!triangle_point(a,b,c,.2f,.2f,{0,inf,0}).valid,"infinite traversal allowance rejected");
 check(!offset(p,{}).valid,"zero outgoing direction rejected");
 check(!offset(p,{nan,0,1}).valid,"NaN outgoing direction rejected");
 check(!offset(p,{std::numeric_limits<float>::max(),0,0}).valid,"overflowing outgoing squared length rejected");
 Point bad=p;bad.error.z=-1;check(!offset(bad,{0,0,1}).valid,"negative input error box rejected");
 bad=p;bad.normal={};check(!offset(bad,{0,0,1}).valid,"zero geometric normal rejected");
 bad=p;bad.position.z=std::numeric_limits<float>::max();bad.error.z=std::numeric_limits<float>::max();
 check(!offset(bad,{0,0,1}).valid,"overflowing spawn fails closed");
 auto extra=triangle_point(a,b,c,.25f,.25f,{0,0,.0001f});
 check(extra.valid&&extra.error.z>=.0001f&&offset(extra,{0,0,1}).origin.z>.0001f,"explicit traversal allowance preserved and projected");
 auto reversed=triangle_point(a,c,b,.25f,.25f);auto r=offset(reversed,{0,0,1});
 check(r.valid&&r.origin.z>0,"winding reversal still follows outgoing side");
}
}
int main(){planes();local_coordinates();invalid_and_state();std::cout<<"Ray spawn: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}

