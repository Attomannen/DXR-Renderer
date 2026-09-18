#include "AtmosphereCommon.hlsli"
struct FsIn { float4 position : SV_POSITION; float2 uv : UV; };
Texture2D<float4> FogHdr : register(t1);
Texture2D<float4> FogSunlight : register(t2);
SamplerState FogLinear : register(s3);
Texture2D<float> FogPreviousEv100 : register(t5);
// Hero volumetric clouds (CloudsVolumeCS.hlsl): premultiplied color in rgb,
// transmittance in a. Composited onto the sky first, fog/shafts on top of
// that -- clouds are the far background layer, fog is near the camera.
Texture2D<float4> FogClouds : register(t6);
#include "Exposure.hlsli"
float4 main(FsIn input) : SV_TARGET
{
    int2 p = min(int2(input.position.xy + FogJitter), int2(FogWidth - 1, FogHeight - 1));
    p = max(p,0);
    float depth = FogDepth.Load(int3(p,0));
    float3 delta = FogWorld((p + 0.5 - FogJitter) / float2(FogWidth, FogHeight), min(depth,0.99999)) - FogCamera;
    float distance = depth >= 0.999999 ? FogMaxDistance : length(delta) * 0.01;
    float transmittance = exp(-FogOpticalDepth(normalize(delta), distance));
    if (depth >= 0.999999 && FogAffectSky == 0) transmittance = 1;
    float3 sunlight = 0;
    if (FogVolumeEnabled != 0)
    {
        uint w,h; FogSunlight.GetDimensions(w,h);
        float2 pos = input.uv * float2(w,h) - 0.5, f = frac(pos);
        int2 base = int2(floor(pos));
        float weightSum = 0;
        [unroll] for (int y=0;y<2;++y) [unroll] for (int x=0;x<2;++x)
        {
            float4 tap = FogSunlight.Load(int3(clamp(base+int2(x,y),0,int2(w-1,h-1)),0));
            float weight = (x ? f.x : 1-f.x) * (y ? f.y : 1-f.y);
            weight *= exp(-abs(tap.a-distance) / max(0.25, distance * 0.02));
            sunlight += tap.rgb * weight; weightSum += weight;
        }
        sunlight /= max(weightSum, 0.00001);
    }
    if (FogDebugView == 1) return float4(transmittance.xxx,1);
    if (FogDebugView == 2) return float4(sunlight,1);
    float3 hdr = FogHdr.SampleLevel(FogLinear,input.uv,0).rgb;
    if (depth >= 0.999999f)
    {
        // Bilinear upsample from the clouds' own (reduced) resolution -- point
        // sampling the reduced target with a linear filter already does this;
        // FogClouds is written at whatever size CreateVolumeTexture chose, and
        // SampleLevel over [0,1] uv resolves that regardless of the mismatch.
        float4 clouds = FogClouds.SampleLevel(FogLinear, input.uv, 0);
        hdr = hdr * clouds.a + clouds.rgb;
    }
    // The DXR HDR carries pre-exposure; bring this pass's own light to match.
    const float preExposure = FogPreExposed > 0.5f ? PreExposureFromEv100(FogPreviousEv100.Load(int3(0,0,0))) : 1.0f;
    sunlight *= preExposure;
	// The disk is a directional sky feature, not a screen-space sprite. It
	// shares FogSunDirection with direct lighting and the shadowed volume march,
	// so rotating the directional light moves the visible sun and its shafts
	// together. Keep it behind geometry by evaluating only sky-depth pixels.
	if (FogSunDiskEnabled != 0u && depth >= 0.999999)
	{
		const float3 viewDir = normalize(FogWorld(input.uv, 0.99999) - FogCamera);
		const float cosAngle = dot(viewDir, FogSunDirection);
		const float innerCos = cos(FogSunDiskAngularRadius);
		const float outerCos = cos(FogSunDiskAngularRadius * 2.5f);
		const float disk = smoothstep(outerCos, innerCos, cosAngle);
		// A restrained halo avoids an aliased hard edge and provides the bright
		// atmospheric cue that makes forward-scattered shafts readable.
		const float halo = pow(saturate((cosAngle - cos(FogSunDiskAngularRadius * 12.0f)) /
			max(innerCos - cos(FogSunDiskAngularRadius * 12.0f), 1e-5f)), 3.0f) * 0.08f;
		hdr += FogSunRadiance * FogSunDiskIntensity * (disk + halo) * preExposure;
	}
    return float4(hdr * transmittance + FogColor * preExposure * (1-transmittance) + sunlight,1);
}
