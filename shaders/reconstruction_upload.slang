// SPDX-License-Identifier: MIT
#include <NoGraphicsAPI/shader.slang>
struct GuidePixel {float4 worldDepth;float4 normalRoughness;float4 albedoMetallic;float4 motionDepth;uint4 identity;float4 ray;};
struct UploadRoot {float4* accumulation;GuidePixel* guides;float4* readback;uint width;uint height;uint outputWidth;uint outputHeight;uint spp;uint mode;uint4 padding;};
GPU_ROOT(UploadRoot,root);
[numthreads(8,8,1)]
void computeMain(uint3 id:SV_DispatchThreadID){
 uint2 extent=root.mode==1||root.mode==2?uint2(root.outputWidth,root.outputHeight):uint2(root.width,root.height);if(any(id.xy>=extent))return;
 RWTexture2D<float4> color=gpu_texture<RWTexture2D<float4>>(0);RWTexture2D<float4> output=gpu_texture<RWTexture2D<float4>>(3);
 if(root.mode==0){uint pixel=id.y*root.width+id.x;RWTexture2D<float> depth=gpu_texture<RWTexture2D<float>>(1);RWTexture2D<float2> motion=gpu_texture<RWTexture2D<float2>>(2);color[id.xy]=float4(root.accumulation[pixel].xyz/float(root.spp),1);depth[id.xy]=root.guides[pixel].motionDepth.z;motion[id.xy]=root.guides[pixel].motionDepth.xy;return;}
 if(root.mode==1){root.readback[id.y*root.outputWidth+id.x]=output[id.xy];return;}
 if(root.mode==3){root.readback[id.y*root.width+id.x]=color[id.xy];return;}
 if(root.mode==2){float2 source=(float2(id.xy)+.5f)*float2(root.width,root.height)/float2(root.outputWidth,root.outputHeight)-.5f;int2 p=int2(floor(source));float2 f=source-float2(p);int2 end=int2(root.width-1,root.height-1);float4 a=color[clamp(p,int2(0),end)],b=color[clamp(p+int2(1,0),int2(0),end)],c=color[clamp(p+int2(0,1),int2(0),end)],d=color[clamp(p+int2(1,1),int2(0),end)];output[id.xy]=lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);}
}
