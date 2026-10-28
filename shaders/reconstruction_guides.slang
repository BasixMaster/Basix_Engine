// SPDX-License-Identifier: MIT
#define BASIX_TRANSPORT_ONLY
#include "progressive_pt.slang"
struct GuidePixel {float4 worldDepth;float4 normalRoughness;float4 albedoMetallic;float4 motionDepth;uint4 identity;float4 ray;};
struct GuideRoot {Root* current;GuidePixel* records;float4* previousVertices;uint historyValid;uint mode;float4 jitterNearFar;float4 previousOrigin;float4 previousForward;float4 previousRight;float4 previousUp;uint spp;uint previousWidth;uint previousHeight;uint pad;};
struct GuidePush {GuideRoot* data;};
[[vk::push_constant]] ConstantBuffer<GuidePush> guidePush;
float2 guideUv(float3 world,float4 origin,float4 forward,float4 right,float4 up){float3 delta=world-origin.xyz;float z=dot(delta,forward.xyz);return float2(.5f+.5f*dot(delta,right.xyz)/(z*right.w*forward.w),.5f-.5f*dot(delta,up.xyz)/(z*forward.w));}
[numthreads(8,8,1)]
void guideMain(uint3 id:SV_DispatchThreadID){
 GuideRoot* g=guidePush.data;if(g==nullptr||g.current==nullptr)return;Root* root=g.current;if(id.x>=root.width||id.y>=root.height)return;uint64_t pixel=uint64_t(id.y)*root.width+id.x;
 if(g.mode==1){
  float3 sum=0;for(uint sample=0;sample<g.spp;++sample){uint64_t key=ptHash(root.seed^ptHash(pixel)^ptHash(uint64_t(sample)+0x4b1d3f857ef01a29ull));float2 uv=float2(.5f,.5f)-g.jitterNearFar.xy;float3 direction=ptCamera(root,id.xy,uv.x,uv.y);float cosForward=dot(direction,root.cameraForward.xyz);sum+=ptRadiance(root,root.cameraOrigin.xyz,direction,ptHash(key+2),g.jitterNearFar.z/cosForward,g.jitterNearFar.w/cosForward);}root.accumulation[pixel]=float4(sum,float(g.spp));return;
 }
 GuidePixel result=(GuidePixel)0;result.motionDepth.z=1;float3 ray=ptCamera(root,id.xy,.5f-g.jitterNearFar.x,.5f-g.jitterNearFar.y);result.ray=float4(ray,1);float cosForward=dot(ray,root.cameraForward.xyz);PtHit hit=ptTrace(root,root.cameraOrigin.xyz,ray,g.jitterNearFar.w/cosForward,g.jitterNearFar.z/cosForward);
 if(hit.error!=0){result.identity.w=2;g.records[pixel]=result;return;}if(hit.found==0){g.records[pixel]=result;return;}
 float3 p=hit.position;float z=dot(p-root.cameraOrigin.xyz,root.cameraForward.xyz);float n=g.jitterNearFar.z,f=g.jitterNearFar.w;float depth=f/(f-n)-(f*n)/((f-n)*z);BasixMaterial material=root.materials[hit.material];
 result.worldDepth=float4(p,z);result.normalRoughness=float4(hit.outward,material.roughness);result.albedoMetallic=float4(material.base,material.metallic);result.motionDepth.z=depth;result.identity=uint4(root.triangles[hit.index].id,hit.instanceId,hit.material,1);
 if(g.historyValid!=0&&g.previousVertices!=nullptr){float3 a=g.previousVertices[hit.index*3].xyz,b=g.previousVertices[hit.index*3+1].xyz,c=g.previousVertices[hit.index*3+2].xyz;float3 previousPoint=a+hit.bary.x*(b-a)+hit.bary.y*(c-a);if(g.pad==4)previousPoint=p;float previousZ=dot(previousPoint-g.previousOrigin.xyz,g.previousForward.xyz);float2 currentUv=guideUv(p,root.cameraOrigin,root.cameraForward,root.cameraRight,root.cameraUp),previousUv=guideUv(previousPoint,g.previousOrigin,g.previousForward,g.previousRight,g.previousUp);bool valid=z>=n&&z<=f&&previousZ>0&&all(isfinite(previousUv))&&all(currentUv>=0)&&all(currentUv<1);if(valid){result.motionDepth.xy=(previousUv-currentUv)*float2(root.width,root.height);result.motionDepth.w=1;}}
 if(g.pad==1)result.motionDepth.xy=-result.motionDepth.xy;else if(g.pad==2)result.motionDepth.xy*=2;else if(g.pad==3)result.motionDepth.z=.5f;
 g.records[pixel]=result;
}
