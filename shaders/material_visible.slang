// SPDX-License-Identifier: MIT
#include <NoGraphicsAPI/shader.slang>
#include "basix_material_vndf.slang"
struct VisibleCase {BasixMaterial material;float3 wo;float uLobe;float u1;float u2;uint2 padding;};
struct Root {VisibleCase* cases;BasixVisibleSample* results;uint count;uint3 padding;};
GPU_ROOT(Root,root);
[numthreads(64,1,1)]
void computeMain(uint3 id:SV_DispatchThreadID){
 if(id.x>=root.count)return;
 VisibleCase c=root.cases[id.x];root.results[id.x]=basixSampleVisible(c.material,c.wo,c.uLobe,c.u1,c.u2);
}
