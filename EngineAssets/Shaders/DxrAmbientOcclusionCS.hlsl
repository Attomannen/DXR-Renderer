#include "DxrCommon.hlsli"

RWTexture2D<float> gOut : register(u0);

cbuffer Params : register(b0)
{
	float3 gCameraOrigin; float gTanHalfFovY;
	float3 gCameraRight; float gAspect;
	float3 gCameraUp; float _pad0;
	float3 gCameraForward; float gAoRadius;
	uint2 gSize; uint gFrameIndex; float _pad1;
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
    return float3x3(t, b, n); // mul(local, basis)
}

[numthreads(8,8,1)] 
void main(uint3 id : SV_DispatchThreadID)
{
	if(any(id.xy >= gSize)) return;
	
	float2 uv = (float2(id.xy) + 0.5f) / float2(gSize); 
	float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
	
	float3 primaryDir = normalize(gCameraForward + gCameraRight * (ndc.x * gAspect * gTanHalfFovY) + gCameraUp * (ndc.y * gTanHalfFovY));
	
	RayDesc primary; 
	primary.Origin = gCameraOrigin; 
	primary.Direction = primaryDir; 
	primary.TMin = 0.01f; 
	primary.TMax = 100000.0f;
	
	RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE> primaryQ;
	primaryQ.TraceRayInline(gScene, RAY_FLAG_NONE, 0xff, primary); 
	while(primaryQ.Proceed()) {}
	
	if(primaryQ.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
	{
		gOut[id.xy] = 1.0f;
		return;
	}
	
	const RayGeometryLookup geometry = gRayGeometry[primaryQ.CommittedInstanceID()];
	const uint3 tri = gRawGeometry[NonUniformResourceIndex(geometry.indexSrv)].Load3(primaryQ.CommittedPrimitiveIndex() * 12u);
	
	float3 p[3];
	[unroll] for (uint i = 0; i < 3; ++i)
		p[i] = asfloat(gRawGeometry[NonUniformResourceIndex(geometry.vertexSrv)].Load3(tri[i] * geometry.vertexStride + geometry.positionOffset));
		
	float3 world, normal, origin;
	ReconstructRaySurface(p[0], p[1], p[2], primaryQ.CommittedTriangleBarycentrics(),
		primaryQ.CommittedObjectToWorld3x4(), primaryQ.CommittedWorldToObject3x4(), primaryDir, world, normal, origin);
	
	// Cosine-weighted hemisphere sampling
	uint seed = id.x + id.y * gSize.x + gFrameIndex * gSize.x * gSize.y;
	float2 u = float2(RandomFloat(seed), RandomFloat(seed));
	
	float r = sqrt(u.x);
	float theta = 2.0f * 3.14159265f * u.y;
	
	float3 rayDirLocal = float3(r * cos(theta), r * sin(theta), sqrt(max(0.0f, 1.0f - u.x)));
	float3 rayDirWorld = normalize(mul(rayDirLocal, GetTangentBasis(normal)));
	
	RayDesc aoRay; 
	aoRay.Origin = origin; 
	aoRay.Direction = rayDirWorld; 
	aoRay.TMin = 0.01f; 
	aoRay.TMax = max(gAoRadius, 1.5f); // Local occlusion radius
	
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
	q.TraceRayInline(gScene, RAY_FLAG_NONE, 0xff, aoRay); 
	while(q.Proceed()) {}
	
	gOut[id.xy] = (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0f : 1.0f;
}
