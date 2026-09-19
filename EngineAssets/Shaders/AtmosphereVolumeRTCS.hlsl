#include "DxrCommon.hlsli"
#include "AtmosphereCommon.hlsli"
float FogSunVisibility(float3 world)
{
    RayDesc ray;
    ray.Origin = world; ray.Direction = FogSunDirection;
    ray.TMin = 0.0001; ray.TMax = 1000;
    RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(gScene,RAY_FLAG_NONE,0xFF,ray);
    while (query.Proceed())
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            AcceptRayTriangle(query.CandidateInstanceID(),query.CandidatePrimitiveIndex(),query.CandidateTriangleBarycentrics()))
            query.CommitNonOpaqueTriangleHit();
    return query.CommittedStatus() == COMMITTED_NOTHING ? 1 : 0;
}
#include "AtmosphereVolumeMain.hlsli"
