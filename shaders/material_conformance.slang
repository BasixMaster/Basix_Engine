// SPDX-License-Identifier: MIT
// Float GPU port of the bounded CPU reference; not an independent physics oracle.
#include <NoGraphicsAPI/shader.slang>
#include "basix_material.slang"
struct MaterialCase { float4 baseRough; float4 woMetal; float3 wi; uint model; };
struct MaterialResult { float3 f; float density; };
struct Root { MaterialCase* cases; MaterialResult* results; uint count; uint3 padding; };
GPU_ROOT(Root,root);
[numthreads(64,1,1)]
void computeMain(uint3 id:SV_DispatchThreadID) {
 if(id.x>=root.count)return;
 MaterialCase c=root.cases[id.x];MaterialResult r;
 BasixMaterial material=(BasixMaterial)0;material.base=c.baseRough.xyz;
 material.roughness=c.baseRough.w;material.metallic=c.woMetal.w;material.model=c.model;
 r.f=basixEvaluate(material,c.woMetal.xyz,c.wi);
 r.density=basixPdf(material,c.woMetal.xyz,c.wi);
 root.results[id.x]=r;
}
