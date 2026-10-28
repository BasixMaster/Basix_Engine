// SPDX-License-Identifier: MIT
#include "basix/reconstruction_guides.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace basix;
using namespace reconstruction_guides;
using render::Vec3;
int checks=0,failures=0;
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
bool near(double x,double y,double e=2e-12){return std::abs(x-y)<=e;}
bool near(Vec3 a,Vec3 b,double e=2e-12){return near(a.x,b.x,e)&&near(a.y,b.y,e)&&near(a.z,b.z,e);}
template<class F>void rejects(F f,const char* label){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,label);}
reference::Camera camera(Vec3 origin={}){return {origin,origin+Vec3{0,0,-1},{0,1,0},90};}
std::array<double,4> apply(std::array<double,4> a,const Matrix4& m){std::array<double,4> out{};for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)out[c]+=a[r]*m[r*4+c];return out;}
reference::Scene scene(){
 reference::Scene s;s.materials={{77,{.2,.4,.8},.3,.8,{},render::MaterialModel::metallic_roughness}};
 s.triangles={{90000007,77,{-10,-10,-4},{10,-10,-4},{0,10,-4}}};return s;
}
void projections(){
 const View view(camera(),{8,4},1,9);const auto& b=view.basis();
 check(near(b.right,{1,0,0})&&near(b.up,{0,1,0})&&near(b.forward,{0,0,-1}),"reference right/up/forward basis anchor");
 const auto c=view.project({0,0,-4});check(c.projectable&&c.in_frustum&&near(c.pixel.x,4)&&near(c.pixel.y,2),"optical axis projects to image center");
 check(near(c.linear_depth,4)&&near(c.perspective_depth,27./32),"linear4 and perspective27/32 independent anchor");
 const auto p=view.project({2,1,-4});check(near(p.pixel.x,5)&&near(p.pixel.y,1.5),"aspect2 horizontal and top-left Y projection");
 check(near(view.project({0,0,-1}).perspective_depth,0)&&near(view.project({0,0,-9}).perspective_depth,1),"noninverted near0/far1 endpoints");
 check(!view.project({0,0,1}).projectable&&!view.project({0,0,0}).projectable,"behind and camera-plane projections masked");
 check(!view.project({0,0,-.5}).in_frustum&&!view.project({0,0,-10}).in_frustum,"near/far frustum refusal");
 check(!view.project({9,0,-1}).in_frustum,"offscreen point masked");
 const View jittered(camera(),{8,4},1,9,{.25,-.25});const auto ray=jittered.primary_ray(3,1);
 check(near(render::dot(ray.direction,ray.direction),1),"primary ray normalized");
 const auto q=ray.origin+ray.direction*(4/render::dot(ray.direction,b.forward));
 const auto unj=jittered.project(q),j=jittered.project_jittered(q);
 check(near(unj.pixel.x,3.25)&&near(unj.pixel.y,1.75),"sample center minus SDK projection jitter sign");
 check(near(j.pixel.x,3.5)&&near(j.pixel.y,1.5),"jittered projection returns exact sampled pixel center");
 check(near(render::dot(ray.origin+ray.direction*ray.t_min,b.forward),1)&&near(render::dot(ray.origin+ray.direction*ray.t_max,b.forward),9),"ray t clip planes use view depth rather than radial distance");
 for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x){
  const auto r=view.primary_ray(x,y);const auto a=view.project(r.origin+r.direction*4);
  check(near(a.pixel.x,x+.5)&&near(a.pixel.y,y+.5),"32 independent pixel-center ray/project round trips");
 }
 const auto m=projection_matrices(view);const auto clip=apply({2,1,4,1},m.camera_to_clip);
 check(near(clip[0],1)&&near(clip[1],1)&&near(clip[2],27./8)&&near(clip[3],4),"row-vector projection matrix analytic anchor");
 const auto back=apply(clip,m.clip_to_camera);check(near(back[0],2)&&near(back[1],1)&&near(back[2],4)&&near(back[3],1),"projection analytic inverse homogeneous round trip");
 const auto jm=projection_matrices(jittered);check(jm.camera_to_clip==m.camera_to_clip&&jm.clip_to_camera==m.clip_to_camera,"camera matrices exclude projection jitter");
 const View explicit_aspect(camera(),{8,4},1,9,{},1);const auto ap=explicit_aspect.project({2,1,-4});
 check(near(ap.pixel.x,6)&&near(ap.pixel.y,1.5)&&near(explicit_aspect.basis().aspect,1),"explicit display aspect overrides input ratio analytically");
 check(near(projection_matrices(explicit_aspect).camera_to_clip[0],1),"projection matrix uses explicit display aspect");
}
void temporal(){
 const View previous(camera(),{8,4},1,9),current(camera({1,0,0}),{8,4},1,9);
 History h{&previous,true,false,{}};auto motion=motion_for_point(current,{0,0,-4},h);
 check(motion.valid&&near(motion.previous_minus_current.x,.5)&&near(motion.previous_minus_current.y,0),"camera right motion previous-minus-current positive half pixel");
 const View up(camera({0,1,0}),{8,4},1,9);motion=motion_for_point(up,{0,0,-4},h);
 check(motion.valid&&near(motion.previous_minus_current.x,0)&&near(motion.previous_minus_current.y,-.5),"camera up motion negative screen Y half pixel");
 h.previous_world_position=Vec3{1,0,-4};motion=motion_for_point(previous,{0,0,-4},h);
 check(motion.valid&&near(motion.previous_minus_current.x,.5)&&near(motion.previous_minus_current.y,0),"explicit object previous-world point motion");
 h.previous_world_position.reset();const View jitter(camera(),{8,4},1,9,{.5,-.5});motion=motion_for_point(jitter,{0,0,-4},h);
 check(motion.valid&&near(motion.previous_minus_current.x,0)&&near(motion.previous_minus_current.y,0),"jitter-only motion stays exactly zero");
 h.valid=false;check(motion_for_point(current,{0,0,-4},h).mask==HistoryMask::disabled,"disabled history masked");
 h={nullptr,true,false,{}};check(motion_for_point(current,{0,0,-4},h).mask==HistoryMask::missing_previous,"missing previous view masked");
 h={&previous,true,true,{}};check(motion_for_point(current,{0,0,-4},h).mask==HistoryMask::camera_cut,"explicit camera cut resets motion");
 h.camera_cut=false;const View resized(camera(),{16,8},1,9);check(motion_for_point(resized,{0,0,-4},h).mask==HistoryMask::resized,"resize resets motion");
 h.allow_input_resize=true;const auto dynamic=motion_for_point(resized,{1,1,-4},h);
 check(dynamic.valid&&near(dynamic.previous_minus_current.x,0)&&near(dynamic.previous_minus_current.y,0),"same-aspect dynamic input resize uses normalized UV with zero static motion");
 const View dynamic_moved(camera({1,0,0}),{16,8},1,9);const auto moved=motion_for_point(dynamic_moved,{0,0,-4},h);
 check(moved.valid&&near(moved.previous_minus_current.x,1)&&near(moved.previous_minus_current.y,0),"dynamic input motion measured in current render pixels");
 check(reprojection_matrices(resized,h).valid,"explicit dynamic input mode permits temporal matrices");
 const View aspect_changed(camera(),{4,4},1,9);const auto aspect_motion=motion_for_point(aspect_changed,{1,0,-4},h);
 check(aspect_motion.valid&&near(aspect_motion.previous_minus_current.x,-.25),"aspect-changing resize retains actual normalized projection motion");
 const View old_rounded(camera(),{427,240},1,9,{},16./9),new_rounded(camera(),{320,180},1,9,{},16./9);
 History fixed_display{&old_rounded,true,false,{},true};const auto rounded=motion_for_point(new_rounded,{1,1,-4},fixed_display);
 check(rounded.valid&&near(rounded.previous_minus_current.x,0)&&near(rounded.previous_minus_current.y,0),"rounded input ratio changes preserve zero motion at fixed display aspect");
 const auto rounded_matrices=reprojection_matrices(new_rounded,fixed_display);const auto round_clip=apply({.2,.3,.8,1},rounded_matrices.clip_to_previous_clip);
 check(rounded_matrices.valid&&near(round_clip[0],.2)&&near(round_clip[1],.3)&&near(round_clip[2],.8)&&near(round_clip[3],1),"fixed-display temporal matrix remains identity across rounded input sizes");
 h.allow_input_resize=false;
 const View clip_change(camera(),{8,4},.5,9);check(motion_for_point(clip_change,{0,0,-4},h).mask==HistoryMask::clip_changed,"clip change resets motion");
 check(motion_for_point(current,{0,0,1},h).mask==HistoryMask::out_of_frustum,"behind-current history masked");
 h.previous_world_position=Vec3{100,0,-4};check(motion_for_point(current,{0,0,-4},h).mask==HistoryMask::out_of_frustum,"previous object position outside frustum masked");
 h.allow_offscreen_projection=true;const auto outside=motion_for_point(previous,{0,0,-4},h);
 check(outside.valid&&near(outside.previous_minus_current.x,50)&&near(outside.previous_minus_current.y,0),"explicit offscreen previous projection retains analytic finite50pixel motion");
 h.previous_world_position=Vec3{0,0,-.5};const auto before_near=motion_for_point(previous,{0,0,-4},h);
 check(before_near.valid&&near(before_near.previous_minus_current.x,0)&&near(before_near.previous_minus_current.y,0),"explicit previous projection permits positive depth before near plane");
 h.previous_world_position=Vec3{0,0,-10};check(motion_for_point(previous,{0,0,-4},h).valid,"explicit previous projection permits depth beyond far plane");
 h.previous_world_position=Vec3{0,0,1};check(!motion_for_point(previous,{0,0,-4},h).valid,"offscreen mode still masks behind-camera previous point");
 h.previous_world_position=Vec3{0,0,-4};check(!motion_for_point(previous,{100,0,-4},h).valid,"offscreen previous mode retains current-frustum requirement");
 const View tiny_input(camera(),{1,1},1,9),huge_input(camera(),{16384,16384},1,9);
 History huge_motion{&tiny_input,true,false,Vec3{1,0,-1e-308},true,true};
 check(tiny_input.project(*huge_motion.previous_world_position).projectable,"extreme positive-Z previous projection is finite before resize scaling");
 check(!motion_for_point(huge_input,{0,0,-4},huge_motion).valid,"nonfinite final motion after resize scaling is masked");
 h.allow_offscreen_projection=false;
 h.previous_world_position.reset();const auto m=reprojection_matrices(current,h);check(m.valid,"camera temporal matrices valid for bounded history");
 const View rotated(reference::Camera{{},{-1,0,0},{0,1,0},90},{8,4},1,9);
 const auto rotation_motion=motion_for_point(rotated,{-4,0,-4},h);check(rotation_motion.valid&&near(rotation_motion.previous_minus_current.x,-4)&&near(rotation_motion.previous_minus_current.y,0),"quarter-turn camera motion independent four-pixel anchor");
 const auto rotation_matrix=reprojection_matrices(rotated,h);const auto rotation_clip=apply({2,0,27./8,4},rotation_matrix.clip_to_previous_clip);
 check(near(rotation_clip[0],-2)&&near(rotation_clip[1],0)&&near(rotation_clip[2],27./8)&&near(rotation_clip[3],4),"quarter-turn clip matrix independent anchor");
 // Current world(0,0,-4) has current-view(-1,0,4), previous-view(0,0,4).
 const auto cur=apply({-1,0,4,1},projection_matrices(current).camera_to_clip);
 const auto prev=apply(cur,m.clip_to_previous_clip);check(near(prev[0],0)&&near(prev[1],0)&&near(prev[2],27./8)&&near(prev[3],4),"clip-to-previous camera translation analytic anchor");
 const auto restore=apply(prev,m.previous_clip_to_clip);bool inverse=true;for(unsigned i=0;i<4;++i)inverse=inverse&&near(restore[i],cur[i]);check(inverse,"bidirectional temporal matrices preserve homogeneous coordinate");
 const auto identity=reprojection_matrices(previous,h);bool same=true;const auto c=apply({.2,-.3,.8,1},identity.clip_to_previous_clip);for(unsigned i=0;i<4;++i)same=same&&near(c[i],std::array<double,4>{.2,-.3,.8,1}[i]);check(same,"camera identity temporal matrix anchor");
 h.camera_cut=true;check(!reprojection_matrices(current,h).valid&&reprojection_matrices(current,h).mask==HistoryMask::camera_cut,"cut temporal matrices masked rather than valid identity");
}
void guides(){
 const reference::World world(scene());const View view(camera(),{1,1},1,9);const History h{&view,true,false,{}};
 const auto g=primary_guide(world,view,0,0,h);
 check(g.hit_valid&&g.front_face&&g.stable_primitive_id==90000007&&g.material_id==77,"primary hit full stable integer IDs and facing");
 check(near(g.world_position,{0,0,-4})&&near(g.world_normal,{0,0,1}),"primary exact plane position and authored world normal");
 check(near(g.linear_depth,4)&&near(g.perspective_depth,27./32),"primary depth convention independent plane anchor");
 check(near(g.albedo,{.2,.4,.8})&&near(g.roughness,.3)&&near(g.metallic,.8),"optional material diagnostics preserve linear material channels");
 check(g.motion.valid&&near(g.motion.previous_minus_current.x,0)&&near(g.motion.previous_minus_current.y,0),"identity guide carries valid zero motion");
 auto source=scene();source.triangles[0].reverse_orientation=true;const reference::World reversed(source);const auto r=primary_guide(reversed,view,0,0);
 check(r.hit_valid&&!r.front_face&&near(r.world_normal,{0,0,-1}),"authored normal retained independently from ray-facing normal");
 source.triangles.clear();const reference::World empty(source);const auto miss=primary_guide(empty,view,0,0,h);
 check(!miss.hit_valid&&!miss.motion.valid&&miss.motion.mask==HistoryMask::miss&&miss.stable_primitive_id==0,"miss has explicit invalid guides/history masks");
 const View far_clip(camera(),{1,1},1,3);check(!primary_guide(world,far_clip,0,0).hit_valid,"primary guide respects far view clip");
 const View near_clip(camera(),{1,1},5,9);check(!primary_guide(world,near_clip,0,0).hit_valid,"primary guide respects near view clip");
}
void invalid(){
 auto bad=camera();bad.target=bad.origin;rejects([&]{View v(bad,{8,4});},"zero forward camera rejected");
 bad=camera();bad.up={0,0,1};rejects([&]{View v(bad,{8,4});},"parallel up camera rejected");
 bad=camera();bad.origin.x=std::numeric_limits<double>::quiet_NaN();rejects([&]{View v(bad,{8,4});},"nonfinite camera rejected");
 bad=camera();bad.origin.x=0x1p21;bad.target.x=bad.origin.x;rejects([&]{View v(bad,{8,4});},"bounded camera position enforced");
 rejects([&]{View v(camera(),{0,4});},"zero dimensions rejected");
 rejects([&]{View v(camera(),{16385,4});},"oversized dimensions rejected");
 rejects([&]{View v(camera(),{8,4},0,9);},"zero near plane rejected");
 rejects([&]{View v(camera(),{8,4},9,9);},"equal clip planes rejected");
 rejects([&]{View v(camera(),{8,4},1,std::numeric_limits<double>::infinity());},"nonfinite far plane rejected");
 rejects([&]{View v(camera(),{8,4},1,9,{.51,0});},"jitter outside half pixel rejected");
 rejects([&]{View v(camera(),{8,4},1,9,{0,std::numeric_limits<double>::quiet_NaN()});},"nonfinite jitter rejected");
 for(double aspect:{-1.,1./32768,32768.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
  rejects([&]{View v(camera(),{8,4},1,9,{},aspect);},"invalid projection aspect override rejected");
 const View v(camera(),{8,4});rejects([&]{(void)v.primary_ray(8,0);},"pixel X out of bounds rejected");
 rejects([&]{(void)v.primary_ray(0,4);},"pixel Y out of bounds rejected");
 rejects([&]{(void)v.project({std::numeric_limits<double>::infinity(),0,-4});},"nonfinite projection input rejected");
 rejects([&]{(void)v.project({0x1p21,0,-4});},"bounded projection input enforced");
 const History invalid_history{&v,true,false,Vec3{0,std::numeric_limits<double>::quiet_NaN(),-4}};
 rejects([&]{motion_for_point(v,{0,0,-4},invalid_history);},"nonfinite explicit previous position rejected");
}
}
int main(){try{projections();temporal();guides();invalid();}catch(const std::exception& e){std::cerr<<"Unexpected: "<<e.what()<<'\n';return 1;}std::cout<<"Reconstruction guides: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
