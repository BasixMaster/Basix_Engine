// SPDX-License-Identifier: MIT
// Original bounded triangle-only GPU transport experiment. Ordinary Vulkan ray
// queries run on the NoGraphicsAPI-owned device through an explicit native scope.
// One identity TLAS instance: committed primitive indices address triangles[].
#include "basix_material_vndf.slang"

struct Triangle {
 float3 a;uint material;float3 b;uint id;float3 c;float area;float3 normal;float pad;
};
struct Root {
 BasixMaterial* materials;
 Triangle* triangles;
 uint* lightIndices;
 float4* accumulation;
 float4* debugHits;
 uint materialCount;uint triangleCount;
 uint lightCount;uint width;uint height;uint sampleStart;
 uint sampleCount;uint maxDepth;uint rrStart;uint flags;
 uint64_t seed;float totalLightArea;uint mode;
 float4 cameraOrigin;float4 cameraForward;float4 cameraRight;float4 cameraUp;float4 environment;
};
struct Push {Root* data;};
// Storage layout is the host's C ABI contract; compare reflected SPIR-V offsets
// and native sizeof/offsetof rather than assuming a constant-buffer packing rule.
[[vk::push_constant]] ConstantBuffer<Push> push;
[[vk::binding(0,0)]] RaytracingAccelerationStructure scene;

static const uint PT_NEE=1,PT_MIS=2,PT_RR=4,PT_VISIBLE=8;
static const float PT_RAY_MAX=1e30f;


float3 ptEvaluate(Root* root,BasixMaterial m,float3 wo,float3 wi){return (root.flags&PT_VISIBLE)!=0?basixEvaluateVisible(m,wo,wi):basixEvaluate(m,wo,wi);}
float ptPdf(Root* root,BasixMaterial m,float3 wo,float3 wi){return (root.flags&PT_VISIBLE)!=0?basixPdfVisible(m,wo,wi):basixPdf(m,wo,wi);}
BasixBsdfSample ptSample(Root* root,BasixMaterial m,float3 wo,float ul,float u1,float u2){
 if((root.flags&PT_VISIBLE)==0)return basixSample(m,wo,ul,u1,u2);
 BasixVisibleSample visible=basixSampleVisible(m,wo,ul,u1,u2);BasixBsdfSample result;result.wi=visible.wi;result.pdf=visible.pdf;result.f=visible.f;result.valid=visible.valid;return result;
}

float ptMax(float3 v){return max(v.x,max(v.y,v.z));}
float ptNaN(){return asfloat(0x7fc00000u);}
float3 ptInvalid(){return float3(ptNaN());}
bool ptFiniteNonnegative(float3 v){return all(isfinite(v))&&all(v>=0);}
uint64_t ptHash(uint64_t x){
 x+=0x9e3779b97f4a7c15ull;x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;
 x=(x^(x>>27))*0x94d049bb133111ebull;return x^(x>>31);
}
struct PtRng {uint64_t key;uint64_t counter;};
float ptNext(inout PtRng rng){
 uint64_t bits=ptHash(rng.key+rng.counter*0x9e3779b97f4a7c15ull);rng.counter++;
 // Exactly representable [0,1) variates. The CPU comparison selects its
 // corresponding random_bits=24 mode; default CPU double53 remains separate.
 return float(uint(bits>>40))*0.000000059604644775390625f;
}
float ptPower(float a,float b){
 if(!(a>0)||!isfinite(a))return 0;
 if(!(b>0)||!isfinite(b))return 1;
 if(a>=b){float r=b/a;return 1/(1+r*r);}float r=a/b;return r*r/(1+r*r);
}
struct PtFrame {float3 x;float3 y;float3 z;};
PtFrame ptFrame(float3 n){
 PtFrame frame;frame.z=n;frame.x=normalize(cross(abs(n.z)<.999f?float3(0,0,1):float3(0,1,0),n));frame.y=cross(n,frame.x);return frame;
}
float3 ptLocal(PtFrame frame,float3 p){return float3(dot(p,frame.x),dot(p,frame.y),dot(p,frame.z));}
float3 ptWorld(PtFrame frame,float3 p){return frame.x*p.x+frame.y*p.y+frame.z*p.z;}
float ptOffsetSize(float3 p){return 2e-5f*max(1.f,ptMax(abs(p)));}
float3 ptOffset(float3 p,float3 normal,float3 direction){return p+normal*((dot(normal,direction)>=0?1.f:-1.f)*ptOffsetSize(p));}

struct PtHit {float t;float3 position;float3 normal;float3 outward;uint index;uint material;uint front;uint found;uint error;};
PtHit ptTrace(Root* root,float3 origin,float3 direction,float maximum){
 PtHit hit=(PtHit)0;
 if(!(maximum>0)||!isfinite(maximum)||!all(isfinite(origin))||!all(isfinite(direction))){hit.error=1;return hit;}
 RayDesc ray;ray.Origin=origin;ray.Direction=direction;ray.TMin=0;ray.TMax=maximum;
 RayQuery<RAY_FLAG_FORCE_OPAQUE> query;query.TraceRayInline(scene,0,255,ray);while(query.Proceed()){}
 if(query.CommittedStatus()!=COMMITTED_TRIANGLE_HIT)return hit;
 hit.index=query.CommittedPrimitiveIndex();hit.t=query.CommittedRayT();
 if(hit.index>=root.triangleCount||!isfinite(hit.t)||hit.t<0){hit.error=1;return hit;}
 Triangle triangle=root.triangles[hit.index];
 if(triangle.material>=root.materialCount||!all(isfinite(triangle.normal))||dot(triangle.normal,triangle.normal)<=0){hit.error=1;return hit;}
 hit.position=origin+direction*hit.t;hit.outward=normalize(triangle.normal);hit.front=dot(direction,hit.outward)<0?1:0;
 hit.normal=hit.front!=0?hit.outward:-hit.outward;hit.material=triangle.material;hit.found=1;return hit;
}
struct PtLight {float3 position;float3 normal;float3 emission;float3 wi;float distance;float pdf;uint index;uint found;uint error;};
PtLight ptLightSample(Root* root,float3 reference,float select,float u1,float u2){
 PtLight result=(PtLight)0;if(root.lightCount==0)return result;
 if(!(root.totalLightArea>0)||!isfinite(root.totalLightArea)){result.error=1;return result;}
 float threshold=select*root.totalLightArea,sum=0;uint index=root.lightIndices[root.lightCount-1];
 for(uint i=0;i<root.lightCount;++i){
  uint candidate=root.lightIndices[i];if(candidate>=root.triangleCount){result.error=1;return result;}
  Triangle t=root.triangles[candidate];if(!(t.area>0)||!isfinite(t.area)){result.error=1;return result;}
  sum+=t.area;if(threshold<sum){index=candidate;break;}
 }
 if(index>=root.triangleCount){result.error=1;return result;}
 Triangle t=root.triangles[index];if(t.material>=root.materialCount){result.error=1;return result;}
 BasixMaterial material=root.materials[t.material];if(!basixMaterialValid(material)||dot(t.normal,t.normal)<=0){result.error=1;return result;}
 float r=sqrt(u1);result.position=t.a*(1-r)+t.b*(r*(1-u2))+t.c*(r*u2);result.normal=normalize(t.normal);
 float3 delta=result.position-reference;result.distance=length(delta);if(!(result.distance>0)||!isfinite(result.distance))return result;
 result.wi=delta/result.distance;float cosine=dot(result.normal,-result.wi);if(!(cosine>0))return result;
 result.pdf=result.distance*result.distance/(root.totalLightArea*cosine);if(!(result.pdf>0)||!isfinite(result.pdf)){result.error=1;return result;}
 result.emission=material.emission;result.index=index;result.found=1;return result;
}
float ptLightPdf(Root* root,float3 reference,PtHit hit){
 if(root.lightCount==0||!(root.totalLightArea>0))return 0;
 bool selected=false;for(uint i=0;i<root.lightCount;++i)if(root.lightIndices[i]==hit.index){selected=true;break;}if(!selected)return 0;
 float3 delta=hit.position-reference;float distance=length(delta);if(!(distance>0))return 0;
 float cosine=dot(hit.outward,-delta/distance);if(!(cosine>0))return 0;
 float density=distance*distance/(root.totalLightArea*cosine);return isfinite(density)?density:0;
}
float3 ptDirect(Root* root,PtHit hit,float3 wo,PtLight light,bool mis){
 if(light.error!=0)return ptInvalid();if(light.found==0)return float3(0);
 PtFrame frame=ptFrame(hit.normal);float3 localWo=ptLocal(frame,wo),localWi=ptLocal(frame,light.wi);
 if(localWo.z<=0||localWi.z<=0)return float3(0);
 float3 start=ptOffset(hit.position,hit.normal,light.wi),delta=light.position-start;float distance=length(delta);
 // A finite endpoint margin avoids classifying the chosen emitter as its own
 // blocker. This is a unit-scale float experiment; gaps below this offset and
 // very near emitters require a separate robust-intersection acceptance gate.
 float endpointMargin=2e-5f*max(1.f,distance);if(distance<=endpointMargin)return float3(0);
 PtHit blocker=ptTrace(root,start,delta/distance,distance-endpointMargin);
 if(blocker.error!=0)return ptInvalid();if(blocker.found!=0)return float3(0);
 BasixMaterial material=root.materials[hit.material];float3 f=ptEvaluate(root,material,localWo,localWi);
 float weight=mis?ptPower(light.pdf,ptPdf(root,material,localWo,localWi)):1;
 return f*light.emission*(localWi.z*weight/light.pdf);
}
float3 ptRadiance(Root* root,float3 origin,float3 direction,uint64_t key){
 PtRng rng={key,0};float3 throughput=float3(1),radiance=float3(0),previous=float3(0);float previousPdf=0;
 bool nee=(root.flags&PT_NEE)!=0,mis=(root.flags&PT_MIS)!=0,rr=(root.flags&PT_RR)!=0;
 for(uint depth=0;depth<=root.maxDepth;++depth){
  PtHit hit=ptTrace(root,origin,direction,PT_RAY_MAX);if(hit.error!=0)return ptInvalid();
  if(hit.found==0){radiance+=throughput*root.environment.xyz;break;}
  BasixMaterial material=root.materials[hit.material];if(!basixMaterialValid(material))return ptInvalid();
  if(hit.front!=0&&ptMax(material.emission)>0){
   float weight=1,lightPdf=depth!=0?ptLightPdf(root,previous,hit):0;
   if(depth!=0&&nee&&lightPdf>0)weight=mis?ptPower(previousPdf,lightPdf):0;
   radiance+=throughput*material.emission*weight;
  }
  // maxDepth counts scattering events. The final ray still sees emission or
  // environment, so its BSDF technique remains paired with the preceding NEE.
  if(depth==root.maxDepth)break;
  PtFrame frame=ptFrame(hit.normal);float3 wo=ptLocal(frame,-direction);
  if(nee){
   float select=ptNext(rng),u1=ptNext(rng),u2=ptNext(rng);
   radiance+=throughput*ptDirect(root,hit,-direction,ptLightSample(root,hit.position,select,u1,u2),mis);
  }
  float lobe=ptNext(rng),u1=ptNext(rng),u2=ptNext(rng);BasixBsdfSample sample=ptSample(root,material,wo,lobe,u1,u2);
  if(sample.valid==0)break;
  throughput*=sample.f*(sample.wi.z/sample.pdf);if(!ptFiniteNonnegative(throughput))return ptInvalid();if(ptMax(throughput)<=0)break;
  float3 wi=normalize(ptWorld(frame,sample.wi));previous=hit.position;previousPdf=sample.pdf;
  if(rr&&depth+1>=root.rrStart){float survival=clamp(ptMax(throughput),.05f,.95f);if(ptNext(rng)>=survival)break;throughput/=survival;}
  origin=ptOffset(hit.position,hit.normal,wi);direction=wi;
 }
 return ptFiniteNonnegative(radiance)?radiance:ptInvalid();
}
float3 ptCamera(Root* root,uint2 pixel,float cameraU,float cameraV){
 float x=(2*(float(pixel.x)+cameraU)/float(root.width)-1)*root.cameraRight.w*root.cameraForward.w;
 float y=(1-2*(float(pixel.y)+cameraV)/float(root.height))*root.cameraForward.w;
 return normalize(root.cameraForward.xyz+root.cameraRight.xyz*x+root.cameraUp.xyz*y);
}

[numthreads(8,8,1)]
void computeMain(uint3 id:SV_DispatchThreadID){
 Root* root=push.data;if(root==nullptr||id.x>=root.width||id.y>=root.height)return;
 uint64_t pixel=uint64_t(id.y)*uint64_t(root.width)+uint64_t(id.x);
 if(root.mode==1){
  PtHit hit=ptTrace(root,root.cameraOrigin.xyz,ptCamera(root,id.xy,.5f,.5f),PT_RAY_MAX);
  root.debugHits[pixel]=hit.error!=0?float4(ptNaN()):(hit.found!=0?float4(hit.t,float(hit.index),float(hit.material),float(hit.front)):float4(-1,-1,-1,0));return;
 }
 if(root.mode!=0||root.maxDepth<1||root.maxDepth>64||root.rrStart<1||root.rrStart>64||
    (root.flags&~15u)!=0||root.sampleCount==0||root.sampleCount>16777215u||root.sampleStart>16777215u-root.sampleCount||
    !ptFiniteNonnegative(root.environment.xyz)){
  root.accumulation[pixel]=float4(ptNaN());return;
 }
 precise float4 sum=root.sampleStart==0?float4(0):root.accumulation[pixel];
 if(!all(isfinite(sum))||sum.w!=float(root.sampleStart)){root.accumulation[pixel]=float4(ptNaN());return;}
 for(uint s=0;s<root.sampleCount;++s){
  uint sampleIndex=root.sampleStart+s;
  uint64_t key=ptHash(root.seed^ptHash(pixel)^ptHash(uint64_t(sampleIndex)+0x4b1d3f857ef01a29ull));
  PtRng camera={key,0};float cameraU=ptNext(camera),cameraV=ptNext(camera);
  float3 sample=ptRadiance(root,root.cameraOrigin.xyz,ptCamera(root,id.xy,cameraU,cameraV),ptHash(key+2));
  sum.xyz+=sample;sum.w+=1;
 }
 root.accumulation[pixel]=sum;
}
