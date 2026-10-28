[[vk::binding(0,0)]] RaytracingAccelerationStructure scene;
[[vk::binding(1,0)]] RWStructuredBuffer<float> distances;
[numthreads(8,8,1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
 if(id.x>=64 || id.y>=64) return;
 RayDesc ray;
 ray.Origin=float3((float(id.x)+0.5)/32.0-1.0,(float(id.y)+0.5)/32.0-1.0,-2.0);
 ray.Direction=float3(0,0,1);ray.TMin=0.001;ray.TMax=100.0;
 RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
 query.TraceRayInline(scene,0,255,ray);
 while(query.Proceed()){}
 distances[id.y*64+id.x]=query.CommittedStatus()==COMMITTED_TRIANGLE_HIT ? query.CommittedRayT() : -1.0;
}
