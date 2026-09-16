// Production directional visibility: raster supplies primary hits, DXR supplies
// the sun occlusion query.  Output is 1=lit, 0=occluded.
#include "DxrCommon.hlsli"
RWTexture2D<float> gOut : register(u0);
cbuffer Params : register(b0)
{
 float3 gCameraOrigin; float gTanHalfFovY;
 float3 gCameraRight; float gAspect;
 float3 gCameraUp; float _pad0;
 float3 gCameraForward; float _pad1;
 float3 gSunDirToLight; float gBias;
 uint2 gSize; float2 _pad;
};
// PCG Hash for random numbers
uint pcg_hash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float RandomFloat(inout uint seed)
{
    seed = pcg_hash(seed);
    return asfloat(0x3f800000 | (seed >> 9)) - 1.0f;
}
float3x3 GetTangentBasis(float3 n) 
{
    float3 up = abs(n.z) < 0.999f ? float3(0,0,1) : float3(1,0,0);
    float3 t = normalize(cross(up, n));
    float3 b = cross(n, t);
    return float3x3(t, b, n);
}

[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID)
{
 if(any(id.xy>=gSize)) return;
 float2 uv=(float2(id.xy)+.5)/gSize; float2 ndc=float2(uv.x*2-1,1-uv.y*2);
 float3 primaryDir=normalize(gCameraForward+gCameraRight*(ndc.x*gAspect*gTanHalfFovY)+gCameraUp*(ndc.y*gTanHalfFovY));
 RayDesc primary; primary.Origin=gCameraOrigin; primary.Direction=primaryDir; primary.TMin=.01; primary.TMax=100000;
 RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES|RAY_FLAG_FORCE_OPAQUE> primaryQ;
 primaryQ.TraceRayInline(gScene,RAY_FLAG_NONE,0xff,primary); while(primaryQ.Proceed()){}
 if(primaryQ.CommittedStatus()!=COMMITTED_TRIANGLE_HIT){gOut[id.xy]=1;return;}
 const RayGeometryLookup geometry = gRayGeometry[primaryQ.CommittedInstanceID()];
 const uint3 tri = gRawGeometry[NonUniformResourceIndex(geometry.indexSrv)].Load3(primaryQ.CommittedPrimitiveIndex() * 12u);
 float3 p[3];
 [unroll] for (uint i = 0; i < 3; ++i)
  p[i] = asfloat(gRawGeometry[NonUniformResourceIndex(geometry.vertexSrv)].Load3(tri[i] * geometry.vertexStride + geometry.positionOffset));
 float3 world, normal, origin;
 ReconstructRaySurface(p[0], p[1], p[2], primaryQ.CommittedTriangleBarycentrics(),
  primaryQ.CommittedObjectToWorld3x4(), primaryQ.CommittedWorldToObject3x4(), primaryDir, world, normal, origin);
  
 uint seed = id.x + id.y * gSize.x; // Add frame index here if available in cb!
 float2 u = float2(RandomFloat(seed), RandomFloat(seed));
 float r_disk = sqrt(u.x);
 float theta = 2.0f * 3.14159265f * u.y;
 float2 diskPos = float2(r_disk * cos(theta), r_disk * sin(theta));
 
 // gBias could be repurposed as sun angular diameter if it's unused, or just hardcode for now e.g. 1.0 deg
 float sunRadiusRadians = 1.0f * 3.14159265f / 180.0f;
 float tanRadius = tan(sunRadiusRadians);
 
 float3x3 sunBasis = GetTangentBasis(normalize(gSunDirToLight));
 float3 jitteredDir = normalize(normalize(gSunDirToLight) + (sunBasis[0] * diskPos.x * tanRadius) + (sunBasis[1] * diskPos.y * tanRadius));

 RayDesc r; r.Origin=origin; r.Direction=jitteredDir; r.TMin=0.0f; r.TMax=100000;
 // Shadow visibility must see both windings. Imported Sponza meshes are not
 // guaranteed to retain a consistent winding relative to raster culling.
 RayQuery<RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
 q.TraceRayInline(gScene,RAY_FLAG_NONE,0xff,r); while(q.Proceed()){}
 gOut[id.xy]=(q.CommittedStatus()==COMMITTED_TRIANGLE_HIT)?0.0:1.0;
}
