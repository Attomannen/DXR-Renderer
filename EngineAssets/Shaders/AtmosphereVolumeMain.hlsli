#include "AtmosphereCommon.hlsli"
RWTexture2D<float4> FogVolume : register(u0);
[numthreads(8,8,1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint2 size = uint2((FogWidth + 1) / 2, (FogHeight + 1) / 2);
    if (any(tid.xy >= size)) return;
    float2 uv = (tid.xy + 0.5) / size;
    int2 pixel = min(int2(uv * float2(FogWidth, FogHeight) + FogJitter), int2(FogWidth - 1, FogHeight - 1));
    pixel = max(pixel, 0);
    float depth = FogDepth.Load(int3(pixel, 0));
    float3 endpoint = FogWorld((pixel + 0.5 - FogJitter) / float2(FogWidth, FogHeight), min(depth, 0.99999));
    float3 delta = endpoint - FogCamera;
    float distance = length(delta) * 0.01;
    float3 direction = normalize(delta);
    if (depth >= 0.999999) distance = FogMaxDistance;
    float end = min(distance, min(FogMaxDistance, FogVolumeDistance));
    float stepLength = max(0, end - FogStart) / FogSteps;
    float3 scatter = 0;
    // Interleaved jitter removes coherent half-resolution march bands. The
    // DXR temporal resolve accumulates this stochastic sequence before HDR
    // tone mapping, turning the residual into stable fine grain instead.
    uint hash = tid.x * 1664525u + tid.y * 1013904223u;
    hash ^= asuint(FogJitter.x * 8192.0) + asuint(FogJitter.y * 4096.0);
    float offset = (hash & 0x00ffffffu) * (1.0 / 16777216.0);
    // Each step used to fire an unconditional full-scene shadow ray, so this
    // loop cost FogSteps (default 32) unbounded rays per half-res pixel no
    // matter how little light the far steps could still contribute. Both exits
    // below are bounded, not quality guesses.
    //
    // FogOpticalDepth is monotonically increasing in distance, so everything
    // step i onward can still scatter is
    //   sum_{j>=i} (exp(-tau(a_j)) - exp(-tau(b_j))) = exp(-tau(a_i)) - exp(-tau(end))
    // which is at most exp(-tau(a_i)). Once that transmittance falls below
    // kMinRemaining, the whole rest of the march is worth less than that, so
    // breaking has a provable error bound.
    const float kMinRemaining = 0.002;
    // Per step, the shadow ray can only scale `weight` by [0,1], so skipping a
    // step whose weight is already negligible costs at most that weight.
    const float kMinStepWeight = 0.0005;
    // Carrying the previous segment's exit transmittance into the next segment
    // also halves the FogOpticalDepth evaluations: one per step, not two.
    float transmittance = exp(-FogOpticalDepth(direction, FogStart));
    for (uint i = 0; i < FogSteps && stepLength > 0; ++i)
    {
        const float a = FogStart + i * stepLength, b = a + stepLength;
        const float nextTransmittance = exp(-FogOpticalDepth(direction, b));
        const float weight = transmittance - nextTransmittance;
        transmittance = nextTransmittance;
        if (weight >= kMinStepWeight)
        {
            // Jitter WITHIN this segment. This previously read
            // `a + (float(i) + offset) * stepLength`, but `a` already includes
            // i * stepLength, so the sample point advanced at double rate and
            // ran to twice the intended end distance: roughly half the shadow
            // rays were fired outside the fog volume entirely, and every sample
            // was paired with a `weight` belonging to a different segment.
            const float t = a + offset * stepLength;
            scatter += weight * FogSunVisibility(FogCamera + direction * (t * 100));
        }
        if (transmittance < kMinRemaining) break;
    }
    // Was a hardcoded g = 0.8 with an inlined Henyey-Greenstein, which silently
    // ignored Tunables::volumetricAnisotropy (default 0.45) and left the
    // matching FogPhase() helper in AtmosphereCommon.hlsli dead. Same formula,
    // now actually driven by the authored value.
    scatter *= FogSunRadiance * FogPhase(dot(direction, FogSunDirection)) * FogVolumeStrength;
    if (depth >= 0.999999 && FogAffectSky == 0) scatter = 0;
    FogVolume[tid.xy] = float4(scatter, distance);
}
