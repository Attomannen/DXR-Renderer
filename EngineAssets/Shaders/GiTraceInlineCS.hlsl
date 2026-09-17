// Stage-3 real GI: ray-traced alternative to GiProjectSHCS's cubemap-capture
// pipeline. Instead of rasterizing the scene 6 times per probe into a
// cubemap and then SH-projecting that, this shoots gRayCount rays directly
// from the probe position over the full sphere (Fibonacci lattice -- equal
// solid angle per sample, same 4*pi/N weighting the cubemap texel loop
// used), shades each hit with the SAME analytic direct-lighting model
// DxrLightingCS.hlsl uses (DxrCommon.hlsli's DecodeHit + ShadeDirect: real
// textures, real shadow rays, real point/spot lights), and projects the
// result into L2 SH -- writing into the identical GiSH buffer/layout
// GiProjectSHCS.hlsl targets, so DeferredLightingPS.hlsl's EvalGiSH needs no
// changes to consume either source.
#include "DxrCommon.hlsli"
#include "GiCommon.hlsli"

// 9 float4 per probe (rgb used, a spare -- slot 0's spare holds sky
// visibility). base = probeIndex * 9. Same buffer GiProjectSHCS.hlsl writes.
RWStructuredBuffer<float4> GiSH : register(u0);
RWStructuredBuffer<float4> GiVisibility : register(u1); // {mean distance, mean squared, valid, pad}
// The same prefiltered environment used by the deferred IBL resolve. A ray
// that misses the scene represents direct sky radiance at the probe. A ray
// that hits geometry represents reflected radiance leaving that surface. The
// camera pass still owns the primary sky/IBL term; this is probe transport.

// One entry per thread group in this dispatch: xyz = probe world position,
// w = that probe's volume index, bit-cast (read it back with asuint, not a
// float conversion). DeferredRenderer::GiProjectProbeBatchRT fills this, and
// it is what lets N probes share one Dispatch(N,1,1) instead of running as N
// serialized Dispatch(1,1,1) calls at one-thread-group occupancy.
StructuredBuffer<float4> gGiProbeBatch : register(t6);

cbuffer GiTraceParams : register(b0)
{
	float  gHysteresis;   // fraction of the previous SH to keep (0 = replace)
	uint   gRayCount;
	uint   gLightCount;
	uint   gTextureFiltering;
	float3 gSunDirToLight;
	float  gSpecularAaStrength;
	float3 gSunRadiance;        // Tunables::dxrSunTint * dxrSunIntensity
	float  gAmbientIntensity;   // Tunables::dxrAmbientIntensity
	float3 gEnvironmentTint;    // AmbientLight colour, including its intensity
	float  gEnvironmentDiffuseMip;
	uint   gSampleSequence;
	float  gFireflyClamp;
	float  gInfiniteBounceIntensity; // 0 = single-bounce only; feeds the probe
	                                 // volume's own irradiance back into new
	                                 // probe updates to approximate further
	                                 // bounces. See EvaluateDxrGi() call below.
	float  gGiTracePad;
};

// One group traces a probe in batches of 64 rays. Only sample results live
// in shared memory; no thread carries the old 192-element moment arrays.
// No float atomics or assumptions about hardware wave width are required.
groupshared float4 sRadiance[64]; // rgb radiance, w sky visibility
groupshared float4 sDirectionDistance[64];
groupshared uint sCell[64];

[numthreads(64, 1, 1)]
void main(uint lane : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    // This group's probe. Every write below (GiSH and GiVisibility) is indexed
    // by gProbeIndex alone, so groups in the same dispatch never overlap.
    const float4 gProbeEntry = gGiProbeBatch[groupId.x];
    const float3 gProbePos = gProbeEntry.xyz;
    const uint gProbeIndex = asuint(gProbeEntry.w);
    const uint N = max(gRayCount, 1u);
    const float dOmega = (4.0f * 3.14159265f) / float(N);
    const float goldenAngle = 2.39996323f;
    float3 coefficient = 0.0f; // lanes 0..8 each own one SH coefficient
    float skyCount = 0.0f;
    float momentSum = 0.0f, momentSumSq = 0.0f, momentCount = 0.0f;

    for (uint batch = 0; batch < N; batch += 64u)
    {
        const uint i = batch + lane;
        if (i < N)
        {
            const float z = 1.0f - (2.0f * float(i) + 1.0f) / float(N);
            const float r = sqrt(saturate(1.0f - z * z));
			// Preserve Fibonacci coverage within one capture, but rotate it for
			// every refresh. Otherwise a fixed low ray count bakes a permanent
			// constellation around small, bright emitters instead of converging.
			const float rotation = frac(sin(float(gProbeIndex * 1664525u + gSampleSequence * 1013904223u)) * 43758.5453f);
			const float phi = goldenAngle * (float(i) + rotation * float(N));
            const float3 dir = float3(r * cos(phi), r * sin(phi), z);
            RayDesc ray;
            ray.Origin = gProbePos;
            ray.Direction = dir;
            ray.TMin = 0.01f;
            ray.TMax = 100000.f;

            RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> q;
            q.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, ray);
            while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
                AcceptRayTriangle(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
                q.CommitNonOpaqueTriangleHit();
        }

            float3 radiance;
            float hitGeom;   // 1 = hit (matches the captured cube's "alpha 1 = geometry"), 0 = sky
            // Boundary probes can be embedded in walls/floors. Treat
            // near-zero hits as invalid probe self-intersections instead of
            // baking that surface into the probe's indirect-light signal.
            const bool validHit = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT && q.CommittedRayT() > 0.5f;
            if (validHit)
            {
                const float spread = gTextureFiltering != 0u ? sqrt(4.0f * 3.14159265f / float(max(gRayCount, 1u))) : 0.0f;
                const HitSurface hs = DecodeHit(q, 0.0f, spread, gSpecularAaStrength);
                const float3 shadowOrigin = hs.rayOrigin;
                // SH is later multiplied by the receiving surface's diffuse albedo.
                // Store only diffuse incoming radiance here; specular and display
                // ambient are evaluated in the final pass. Include direct light in
                // the transport so a sunlit wall, not just an emissive wall, becomes
                // a source for the next diffuse bounce.
                radiance = ShadeDiffuseDirect(hs, shadowOrigin, gSunDirToLight,
                    gLightCount, gSunRadiance);

                // A diffuse surface can also reflect the environment toward this
                // probe. This is a low-frequency sample; sky misses below use mip 0
                // so openings retain their directional environment signal in SH.
                if (gEnvironmentDiffuseMip >= 0.0f)
                {
                    const float3 envDiffuse = gDxrEnvironment.SampleLevel(gMaterialSampler, hs.worldNormal, gEnvironmentDiffuseMip).rgb;
                    radiance += (1.0f - hs.metalness) * hs.albedo * hs.ao * envDiffuse * gEnvironmentTint;
                }

                radiance += hs.emissive;

                // Infinite-bounce feedback (DDGI-style): a probe-update ray hit
                // is itself a diffuse surface, so it also reflects whatever the
                // volume already believes reaches it -- light that has already
                // bounced through one or more prior probe updates. Sampling the
                // live SH volume here, instead of only direct light, lets energy
                // recirculate across successive updates and approximates further
                // bounces without tracing them explicitly. EvaluateDxrGi reads
                // the same GiSH buffer this shader writes; early updates see
                // mostly-unprimed (zero) neighbours and behave like single-bounce
                // until the volume has real data to feed back, which keeps this
                // stable during priming instead of amplifying noise from frame 1.
                if (gInfiniteBounceIntensity > 0.0f)
                {
                    const float3 bounced = EvaluateDxrGi(hs.worldPosition, hs.worldNormal);
                    radiance += (1.0f - hs.metalness) * hs.albedo * hs.ao * bounced * gInfiniteBounceIntensity;
                }

                hitGeom = 1.0f;
            }
			else
			{
                // A sky miss is not a zero-light sample for a DDGI/Lumen-style
                // probe: it is environment radiance arriving from `dir`. Keeping
                // the direction lets the SH projection represent windows and
                // partially open regions. The primary pass samples the sky
                // separately, so this remains an indirect contribution there.
                radiance = gEnvironmentDiffuseMip >= 0.0f
                    ? SkyRadiance(dir, 0.0f, gEnvironmentTint)
                    : 0.0f;
				hitGeom = 0.0f;
			}
			// A uniform probe ray can catch a tiny, very bright emitter. At low
			// ray counts that one sample represents a huge solid angle and becomes
			// a visible white firefly after SH reconstruction. Compress only this
			// exceptional HDR tail; ordinary indirect light remains linear.
			const float lum = dot(radiance, float3(0.2126f, 0.7152f, 0.0722f));
			if (lum > gFireflyClamp)
				radiance *= gFireflyClamp / lum;

			sRadiance[lane] = float4(radiance, 1.0f - hitGeom);
            const float distance = hitGeom != 0.0f ? q.CommittedRayT() : 100000.0f;
            sDirectionDistance[lane] = float4(dir, distance);
            float2 o = dir.xy / max(abs(dir.x)+abs(dir.y)+abs(dir.z), 1e-5f);
            if (dir.z < 0) o = (1-abs(o.yx))*sign(o.xy+1e-6f);
            const uint2 t = min(uint2((o*0.5f+0.5f)*8), uint2(7,7));
            sCell[lane] = t.x + t.y * 8u;
        }
        // All lanes participate, including the final partial batch.
        GroupMemoryBarrierWithGroupSync();
        const uint sampleCount = min(64u, N - batch);
        for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
        {
            if (lane < 9u)
            {
                float basis[9];
                ShBasis(sDirectionDistance[sampleIndex].xyz, basis);
                coefficient += sRadiance[sampleIndex].rgb * basis[lane] * dOmega;
            }
            if (lane == 0u) skyCount += sRadiance[sampleIndex].w;
            // Each lane owns one octahedral bin, eliminating write races.
            if (sCell[sampleIndex] == lane)
            {
                const float distance = sDirectionDistance[sampleIndex].w;
                momentSum += distance;
                momentSumSq += distance * distance;
                momentCount += 1.0f;
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (lane < 9u)
    {
        const uint index = gProbeIndex * 9u + lane;
        const float4 previous = GiSH[index];
        // Slot 0.w is sky visibility; slot 8.w is an explicit probe-valid bit.
        // A cleared probe must not enter trilinear reconstruction as black.
        const float w = lane == 0u ? lerp(saturate(skyCount / float(N)), previous.w, gHysteresis) :
            (lane == 8u ? 1.0f : 0.0f);
        GiSH[index] = float4(lerp(coefficient, previous.rgb, gHysteresis), w);
    }
    const uint momentIndex = gProbeIndex * 64u + lane;
    const float4 previous = GiVisibility[momentIndex];
    const float mean = momentCount > 0 ? momentSum / momentCount : 100000.0f;
    const float meanSq = momentCount > 0 ? momentSumSq / momentCount : 1e10f;
    GiVisibility[momentIndex] = float4(lerp(mean, previous.x, gHysteresis),
        lerp(meanSq, previous.y, gHysteresis), momentCount > 0 ? 1 : 0, 0);
}
