// SPDX-License-Identifier: MIT
#include <NoGraphicsAPI/shader.slang>
struct Root { float4* output; uint textureIndex; uint width; uint height; uint mode; uint2 padding; };
GPU_ROOT(Root,root);
[numthreads(8,8,1)]
void computeMain(uint3 id:SV_DispatchThreadID) {
 if(id.x>=root.width||id.y>=root.height)return;
 RWTexture2D<float4> target=gpu_texture<RWTexture2D<float4>>(root.textureIndex);
 if(root.mode==0) target[id.xy]=float4(.25,.5,.75,1);
 else root.output[id.y*root.width+id.x]=target[id.xy];
}
