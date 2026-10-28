// SPDX-License-Identifier: MIT
#include "basix/reconstruction_guides.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace basix::reconstruction_guides {
namespace {
using namespace render;
void require(bool ok,const char* reason){if(!ok)throw std::invalid_argument(reason);}
void position(Vec3 p){require(finite(p)&&std::abs(p.x)<=0x1p20&&std::abs(p.y)<=0x1p20&&std::abs(p.z)<=0x1p20,"Guide position outside finite bounded domain");}
bool in_bounds(Pixel2 p,Extent size){return p.x>=0&&p.y>=0&&p.x<size.width&&p.y<size.height;}
HistoryMask history_mask(const View& current,const History& history){
    if(!history.valid)return HistoryMask::disabled;
    if(!history.previous)return HistoryMask::missing_previous;
    if(history.camera_cut)return HistoryMask::camera_cut;
    const auto& previous=*history.previous;const auto a=current.extent(),b=previous.extent();
    if((a.width!=b.width||a.height!=b.height)&&!history.allow_input_resize)return HistoryMask::resized;
    if(current.near_plane()!=previous.near_plane()||current.far_plane()!=previous.far_plane())return HistoryMask::clip_changed;
    return HistoryMask::valid;
}
Matrix4 multiply(const Matrix4& a,const Matrix4& b){
    Matrix4 c{};for(unsigned r=0;r<4;++r)for(unsigned k=0;k<4;++k)for(unsigned j=0;j<4;++j)c[r*4+j]+=a[r*4+k]*b[k*4+j];return c;
}
Matrix4 camera_to_world(const View& view){
    const auto& b=view.basis();return {b.right.x,b.right.y,b.right.z,0,b.up.x,b.up.y,b.up.z,0,b.forward.x,b.forward.y,b.forward.z,0,b.origin.x,b.origin.y,b.origin.z,1};
}
Matrix4 world_to_camera(const View& view){
    const auto& b=view.basis();return {b.right.x,b.up.x,b.forward.x,0,b.right.y,b.up.y,b.forward.y,0,b.right.z,b.up.z,b.forward.z,0,-dot(b.origin,b.right),-dot(b.origin,b.up),-dot(b.origin,b.forward),1};
}
}
View::View(reference::Camera camera,Extent extent,double near_plane,double far_plane,Pixel2 jitter,double projection_aspect)
    :extent_(extent),near_(near_plane),far_(far_plane),jitter_(jitter){
    require(reference::validate(camera),"Invalid guide pinhole camera");
    position(camera.origin);position(camera.target);
    require(extent.width>=1&&extent.height>=1&&extent.width<=16384&&extent.height<=16384,"Guide dimensions outside bounded domain");
    require(std::isfinite(near_)&&std::isfinite(far_)&&near_>=0x1p-20&&far_>near_&&far_<=0x1p20,"Invalid guide clip range");
    require(std::isfinite(jitter.x)&&std::isfinite(jitter.y)&&std::abs(jitter.x)<=.5&&std::abs(jitter.y)<=.5,"Invalid projection jitter");
    require(std::isfinite(projection_aspect)&&(projection_aspect==0||(projection_aspect>=1./16384&&projection_aspect<=16384)),"Invalid projection aspect override");
    basis_.origin=camera.origin;basis_.forward=normalize(camera.target-camera.origin);
    basis_.right=normalize(cross(basis_.forward,camera.up));basis_.up=cross(basis_.right,basis_.forward);
    basis_.tangent_half_fov=std::tan(camera.vertical_fov_degrees*std::numbers::pi/360);
    basis_.aspect=projection_aspect==0?double(extent.width)/extent.height:projection_aspect;
    require(finite(basis_.right)&&finite(basis_.up)&&dot(basis_.right,basis_.right)>.999999,"Singular guide camera basis");
}
Projection View::project(Vec3 world)const{
    position(world);const auto p=world-basis_.origin;Projection result;
    const double z=dot(p,basis_.forward);result.linear_depth=z;
    if(!(z>0))return result;
    const double nx=dot(p,basis_.right)/(z*basis_.tangent_half_fov*basis_.aspect);
    const double ny=dot(p,basis_.up)/(z*basis_.tangent_half_fov);
    result.pixel={(nx+1)*extent_.width*.5,(1-ny)*extent_.height*.5};
    result.perspective_depth=far_*(z-near_)/(z*(far_-near_));
    result.projectable=std::isfinite(result.pixel.x)&&std::isfinite(result.pixel.y)&&std::isfinite(result.perspective_depth);
    result.in_frustum=result.projectable&&z>=near_&&z<=far_&&in_bounds(result.pixel,extent_);
    return result;
}
Projection View::project_jittered(Vec3 world)const{
    auto result=project(world);if(!result.projectable)return result;
    result.pixel.x+=jitter_.x;result.pixel.y+=jitter_.y;
    result.in_frustum=result.linear_depth>=near_&&result.linear_depth<=far_&&in_bounds(result.pixel,extent_);
    return result;
}
reference::Ray View::primary_ray(std::uint32_t x,std::uint32_t y)const{
    require(x<extent_.width&&y<extent_.height,"Primary guide pixel out of bounds");
    const double px=(2*(double(x)+.5-jitter_.x)/extent_.width-1)*basis_.aspect*basis_.tangent_half_fov;
    const double py=(1-2*(double(y)+.5-jitter_.y)/extent_.height)*basis_.tangent_half_fov;
    const auto direction=normalize(basis_.forward+basis_.right*px+basis_.up*py);
    const double forward=dot(direction,basis_.forward);
    require(forward>0&&std::isfinite(forward),"Invalid guide ray direction");
    return {basis_.origin,direction,near_/forward,far_/forward};
}
Motion motion_for_point(const View& current,Vec3 world,const History& history){
    position(world);if(history.previous_world_position)position(*history.previous_world_position);
    const auto mask=history_mask(current,history);if(mask!=HistoryMask::valid)return {{},false,mask};
    const auto& previous=*history.previous;
    const auto now=current.project(world),before=previous.project(history.previous_world_position.value_or(world));
    if(!now.in_frustum||!(history.allow_offscreen_projection?before.projectable:before.in_frustum))return {{},false,HistoryMask::out_of_frustum};
    const auto size=current.extent(),old_size=previous.extent();
    const Pixel2 displacement{(before.pixel.x/old_size.width-now.pixel.x/size.width)*size.width,
                              (before.pixel.y/old_size.height-now.pixel.y/size.height)*size.height};
    if(!std::isfinite(displacement.x)||!std::isfinite(displacement.y))return {{},false,HistoryMask::out_of_frustum};
    return {displacement,true,HistoryMask::valid};
}
ProjectionMatrices projection_matrices(const View& view){
    const auto& b=view.basis();const double sx=1/(b.tangent_half_fov*b.aspect),sy=1/b.tangent_half_fov;
    const double a=view.far_plane()/(view.far_plane()-view.near_plane()),z=-view.near_plane()*a;
    return {{sx,0,0,0,0,sy,0,0,0,0,a,1,0,0,z,0},
            {1/sx,0,0,0,0,1/sy,0,0,0,0,0,1/z,0,0,1,-a/z}};
}
ReprojectionMatrices reprojection_matrices(const View& current,const History& history){
    ReprojectionMatrices result;result.mask=history_mask(current,history);if(result.mask!=HistoryMask::valid)return result;
    const auto& previous=*history.previous;const auto a=projection_matrices(current),b=projection_matrices(previous);
    result.clip_to_previous_clip=multiply(multiply(multiply(a.clip_to_camera,camera_to_world(current)),world_to_camera(previous)),b.camera_to_clip);
    result.previous_clip_to_clip=multiply(multiply(multiply(b.clip_to_camera,camera_to_world(previous)),world_to_camera(current)),a.camera_to_clip);
    result.valid=true;return result;
}
Guide primary_guide(const reference::World& world,const View& view,std::uint32_t x,std::uint32_t y,const History& history){
    Guide result;const auto hit=world.intersect(view.primary_ray(x,y));
    if(!hit){result.motion.mask=HistoryMask::miss;return result;}
    const auto projection=view.project(hit->position);require(projection.projectable,"Nonprojectable primary hit");
    const auto& material=world.material(hit->material_id);
    result.hit_valid=true;result.front_face=hit->front_face;result.world_position=hit->position;result.world_normal=hit->geometric_normal;
    result.linear_depth=projection.linear_depth;result.perspective_depth=projection.perspective_depth;
    result.albedo=material.base_color;result.roughness=material.roughness;result.metallic=material.metallic;
    result.stable_primitive_id=hit->primitive_id;result.material_id=hit->material_id;
    result.motion=motion_for_point(view,hit->position,history);return result;
}
} // namespace basix::reconstruction_guides
