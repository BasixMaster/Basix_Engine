// SPDX-License-Identifier: MIT
#pragma once
#include "basix/progressive_gpu_abi.hpp"
namespace basix::gpu_guides {
using gpu_reference::Float4;
struct UInt4 {std::uint32_t x,y,z,w;};
struct alignas(16) Pixel {Float4 world_depth,normal_roughness,albedo_metallic,motion_depth;UInt4 identity;Float4 ray;};
struct alignas(16) Root {
 std::uint64_t scene_root,records,previous_vertices;std::uint32_t history_valid,mode;
 Float4 jitter_near_far,previous_origin,previous_forward,previous_right,previous_up;
 std::uint32_t spp,previous_width,previous_height,pad;
};
static_assert(sizeof(Pixel)==96&&offsetof(Pixel,motion_depth)==48&&offsetof(Pixel,identity)==64&&offsetof(Pixel,ray)==80);
static_assert(sizeof(Root)==128&&offsetof(Root,jitter_near_far)==32&&offsetof(Root,spp)==112);
}
