// SPDX-License-Identifier: MIT
#pragma once
#include "basix/reference.hpp"
#include <array>

namespace basix::reconstruction_guides {
struct Pixel2 { double x{},y{}; };
struct Extent { std::uint32_t width{},height{}; };
struct Basis { render::Vec3 origin{},right{},up{},forward{}; double tangent_half_fov{},aspect{}; };
struct Projection {
    Pixel2 pixel{};
    double linear_depth{},perspective_depth{};
    bool projectable=false,in_frustum=false;
};
// Bounded CPU pinhole convention, independent of any GPU/SDK ABI:
// origin is top-left, +x right,+y down; pixel centers are (x+.5,y+.5).
// +forward view depth; noninverted perspective near=0,far=1.
// Jitter denotes projected image displacement in these same pixel axes.
// Consequently primary rays sample pixel center MINUS projection jitter.
// Domain: each extent1..16384; world/camera positions abs<=2^20; near>=2^-20,
// far<=2^20 and far>near; jitter components[-.5,.5]; reference camera FOV1..175.
// Projectable points outside near/far retain UNCLAMPED diagnostic depth;
// only in_frustum projections have the promised[0,1] depth range.
// projection_aspect=0 selects input extent ratio. An explicit positive aspect
// in[1/16384,16384] preserves display geometry across rounded input resizes.
class View {
public:
    View(reference::Camera camera,Extent extent,double near_plane=.1,double far_plane=128,
         Pixel2 projection_jitter_pixels={},double projection_aspect=0);
    [[nodiscard]] Projection project(render::Vec3 world) const;
    [[nodiscard]] Projection project_jittered(render::Vec3 world) const;
    [[nodiscard]] reference::Ray primary_ray(std::uint32_t x,std::uint32_t y) const;
    [[nodiscard]] const Basis& basis() const noexcept {return basis_;}
    [[nodiscard]] Extent extent() const noexcept {return extent_;}
    [[nodiscard]] Pixel2 projection_jitter() const noexcept {return jitter_;}
    [[nodiscard]] double near_plane() const noexcept {return near_;}
    [[nodiscard]] double far_plane() const noexcept {return far_;}
private:
    Basis basis_;Extent extent_;double near_,far_;Pixel2 jitter_;
};
enum class HistoryMask {
    valid,disabled,missing_previous,camera_cut,resized,clip_changed,out_of_frustum,miss
};
struct History {
    const View* previous=nullptr;
    bool valid=false,camera_cut=false;
    // Default uses the same world point (static geometry). For object motion
    // caller must supply the previous location of this SAME surface point.
    std::optional<render::Vec3> previous_world_position;
    // Default invalidates resize history. Explicit dynamic-input mode permits
    // reprojection across input extents, measuring both projections in CURRENT
    // pixels via previous normalized UV. Output-size resets remain caller policy.
    bool allow_input_resize=false;
    // Explicit SR adapter mode: preserve finite previous projection outside
    // viewport/near/far bounds, yielding natural offscreen displacement. The
    // previous point must still be in front of its camera (+Z projectable).
    // This is not a special invalid-MV sentinel; nonprojectable foreground
    // history still needs caller reset/fallback where no pixel mask is consumed.
    bool allow_offscreen_projection=false;
};
struct Motion { Pixel2 previous_minus_current{};bool valid=false;HistoryMask mask=HistoryMask::disabled; };
// Excludes both frames' jitter. Render-pixel units, current->previous sign;
// an SDK normalized adapter can use scale=(1/width,1/height). A valid mask
// establishes bounded projection only, not previous visibility/disocclusion.
Motion motion_for_point(const View& current,render::Vec3 world,const History&);
using Matrix4=std::array<double,16>;
struct ProjectionMatrices { Matrix4 camera_to_clip{},clip_to_camera{}; };
struct ReprojectionMatrices {
    Matrix4 clip_to_previous_clip{},previous_clip_to_clip{};
    bool valid=false;HistoryMask mask=HistoryMask::disabled;
};
// Row-major storage AND row-vector multiplication: p_clip=p_camera*P.
// Clip +X right,+Y up,view +Z forward,depth[0,1]; no raster viewport Y flip.
// Matrices are unjittered. Temporal matrices carry camera-only transport;
// explicit previous-world positions are still needed for object-motion MVs.
ProjectionMatrices projection_matrices(const View&);
ReprojectionMatrices reprojection_matrices(const View& current,const History&);
struct Guide {
    bool hit_valid=false,front_face=false;
    render::Vec3 world_position{},world_normal{},albedo{};
    double linear_depth{},perspective_depth{},roughness{},metallic{};
    std::uint32_t stable_primitive_id{},material_id{};
    Motion motion{};
};
// Single immutable-World primary hit, no radiance sampling or mutable history.
// world_normal is the authored geometric normal, not the ray-facing normal.
// Albedo is linear material base color. Material channels are optional future
// reconstruction diagnostics, not a claim that DLSS SR requires RR buffers.
// Misses have hit_valid=false,motion.valid=false,mask=miss. Zero fields on any
// invalid result are placeholders and MUST NOT be interpreted without masks.
// CPU miss depth0 is not an SDK-ready depth clear. The adapter must explicitly
// encode its noninverted background far value1, independently of this mask.
Guide primary_guide(const reference::World&,const View&,std::uint32_t x,std::uint32_t y,
                    const History& history={});
} // namespace basix::reconstruction_guides
