[[vk::binding(0,0)]] RWTexture2D<float4> externalImage;
[numthreads(8,8,1)]
void computeMain(uint3 id:SV_DispatchThreadID) {
 uint w,h;externalImage.GetDimensions(w,h);
 if(id.x>=w||id.y>=h)return;
 float4 c=externalImage[id.xy];
 externalImage[id.xy]=float4(c.b,1-c.g,c.r,c.a);
}
