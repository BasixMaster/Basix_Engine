// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstddef>
namespace basix::gpu_reference {
struct Float4 {float x,y,z,w;};
struct alignas(16) Material {
 float r,g,b,roughness;
 float metallic,emission_r,emission_g,emission_b;
 std::uint32_t model,pad0,pad1,pad2;
};
struct alignas(16) Triangle {
 float ax,ay,az;std::uint32_t material;
 float bx,by,bz;std::uint32_t id;
 float cx,cy,cz,area;
 float nx,ny,nz,pad;
};
struct alignas(16) Instance {std::uint32_t first_triangle,triangle_count,stable_id,epoch;};
static_assert(sizeof(Instance)==16);
// Pointed-to records use scalar C layout; push constants hold only a 64-bit
// root address. Conventional AS descriptors belong to the native RT adapter.
struct alignas(16) Root {
 std::uint64_t materials,triangles,light_indices,accumulation,debug_hits;
 std::uint32_t material_count,triangle_count;
 std::uint32_t light_count,width,height,sample_start;
 std::uint32_t sample_count,max_depth,rr_start,flags;
 std::uint64_t seed;float total_light_area;std::uint32_t mode;
 Float4 camera_origin,camera_forward,camera_right,camera_up,environment;
 std::uint64_t instances;std::uint32_t instance_count,scene_epoch;
};
static_assert(sizeof(Material)==48&&offsetof(Material,metallic)==16&&offsetof(Material,model)==32);
static_assert(sizeof(Triangle)==64&&offsetof(Triangle,material)==12&&offsetof(Triangle,id)==28&&offsetof(Triangle,nx)==48);
static_assert(sizeof(Root)==192&&offsetof(Root,material_count)==40&&offsetof(Root,light_count)==48);
static_assert(offsetof(Root,sample_count)==64&&offsetof(Root,seed)==80&&offsetof(Root,camera_origin)==96);
static_assert(offsetof(Root,environment)==160&&offsetof(Root,instances)==176&&offsetof(Root,instance_count)==184&&sizeof(Float4)==16);
}
