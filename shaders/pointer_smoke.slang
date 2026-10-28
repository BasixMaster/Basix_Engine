#include <NoGraphicsAPI/shader.slang>
struct Root { uint* output; uint width; uint height; };
GPU_ROOT(Root, root);
[numthreads(8,8,1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
 if(id.x>=root.width || id.y>=root.height) return;
 uint r=id.x*255/(root.width-1),g=id.y*255/(root.height-1);
 root.output[id.y*root.width+id.x]=r|(g<<8)|(64u<<16)|(255u<<24);
}
