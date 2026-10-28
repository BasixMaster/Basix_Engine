// SPDX-License-Identifier: MIT
#include <NoGraphicsAPI/shader.slang>
#include "basix_material.slang"
struct SamplingCase {BasixMaterial material;float3 wo;float uLobe;float u1;float u2;uint2 padding;};
struct Root {SamplingCase* cases;BasixBsdfSample* results;uint count;uint3 padding;};
GPU_ROOT(Root,root);
[numthreads(64,1,1)]
void computeMain(uint3 id:SV_DispatchThreadID) {
 if(id.x>=root.count)return;
 SamplingCase c=root.cases[id.x];root.results[id.x]=basixSample(c.material,c.wo,c.uLobe,c.u1,c.u2);
}
