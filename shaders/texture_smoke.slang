#include <NoGraphicsAPI/shader.slang>
struct Root { uint* output; uint textureIndex; uint width; uint height; uint mode; uint2 padding; };
GPU_ROOT(Root,root);
[numthreads(8,8,1)]
void computeMain(uint3 id:SV_DispatchThreadID) {
 if(id.x>=root.width||id.y>=root.height)return;
 RWTexture2D<float4> target=gpu_texture<RWTexture2D<float4>>(root.textureIndex);
 if(root.mode==0) {
  uint r=id.x*255/max(1u,root.width-1),g=id.y*255/max(1u,root.height-1);
  target[id.xy]=float4(float(r)/255,float(g)/255,64.0/255.0,1);
 } else {
  uint4 c=uint4(round(saturate(target[id.xy])*255));
  root.output[id.y*root.width+id.x]=c.x|(c.y<<8)|(c.z<<16)|(c.w<<24);
 }
}
