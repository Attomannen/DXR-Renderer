// Stage-3 validation shader, iteration 9: the shared bindless-scene decode +
// direct-lighting math moved into DxrCommon.hlsli (DecodeHit/ShadeDirect),
// so this file and GiTraceInlineCS.hlsl's probe capture can't drift apart --
// this is now a thin camera-ray-gen + shade wrapper around that shared code.
//
// Iteration 1 (flat hit/miss color) validated the TLAS + space2 bindings.
// Iteration 2 (normal-only hue) validated the geometry/normal decode.
// Iteration 3 (material-ID hue) validated the materialIndex round trip.
// Iteration 4 (unlit albedo) validated the actual texture fetch.
// Iteration 5 (Lambert + shadow ray) validated hit-point reconstruction.
// Iteration 6 (Cook-Torrance) validated the roughness/metalness -> BRDF response.
// Iteration 7 (multi-light) validated per-light shadow rays.
// Iteration 8 validated smoothly-interpolated authored tangents.
// This iteration factors it all into DxrCommon.hlsli for GiTraceInlineCS to reuse.

#include "DxrCommon.hlsli"
#include "Exposure.hlsli"

RWTexture2D<float4> gOutput : register(u0);
RWTexture2D<float4> gMotionVectors : register(u1); // xy pixel motion, z = viewZprev - viewZ (m) for NRD 2.5D
RWTexture2D<float> gTemporalDepth : register(u2);
RWTexture2D<float2> gMotionValidity : register(u3); // valid transform, expected previous depth
// Signed world normal + perceptual roughness.  This is shared by native TAA
// and DLSS Ray Reconstruction; do not remap XYZ to 0..1 because RR consumes
// the actual normalized vector from this FP16 texture.
RWTexture2D<float4> gSurfaceGuide : register(u4);
RWTexture2D<float4> gDiffuseAlbedo : register(u5); // linear diffuse albedo for DLSS Ray Reconstruction
RWTexture2D<float4> gSpecularAlbedo : register(u6); // linear F0/specular albedo for DLSS Ray Reconstruction
// NRD guides and signal, written only while gNrdEnabled is set.
// Motion for the final resolve (TAA/DLSS) only. NRD keeps the surface motion
// in gMotionVectors and derives its own specular reprojection from hit
// distance; this one is the surface motion blended toward the motion of the
// reflected virtual image, so mirror-like pixels stop smearing when the
// composited image is reprojected. Written at the end of main().
RWTexture2D<float2> gResolveMotion : register(u7);
RWTexture2D<float>  gNrdViewZ : register(u8);           // linear view depth, >NRD denoisingRange for sky
RWTexture2D<float4> gNrdNormalRoughness : register(u9); // NRD_NORMAL_ENCODING 2 (R10G10B10A2 octahedral)
RWTexture2D<float4> gNrdDiffuse : register(u10);         // demodulated indirect diffuse radiance + hit distance
RWTexture2D<float4> gNrdSpecular : register(u11);       // demodulated specular radiance + reflection hit distance
Texture2D<float2> gBrdfLut : register(t4);
Texture2D<float> gPreviousEv100 : register(t6);   // exposure history (pre-exposure)

#include "DxrLightingConstants.hlsli"

float2 ClipToPixel(float4 clip)
{
	return (clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f) * float2(gOutputSize);
}

void WriteTemporal(uint2 pixel, float4 currentClip, float4 previousClip, bool objectHistory)
{
	const bool valid = gTemporalHistoryValid != 0u && objectHistory && previousClip.w > 0.0001f && all(isfinite(previousClip));
	// Motion of the surface that was actually hit: both ends are projected with
	// the unjittered matrices, so a still camera gives exactly zero. (Measuring
	// from the pixel centre instead mixed this frame's jitter into every vector,
	// which DLSS and NRD read as the whole image moving: visible bouncing.)
	const float2 motion = valid ? ClipToPixel(previousClip) - ClipToPixel(currentClip) : float2(0,0);
	const float dz = valid && currentClip.w > 0.0f ? (previousClip.w - currentClip.w) * 0.01f : 0.0f;
	gMotionVectors[pixel] = float4(all(isfinite(motion)) ? clamp(motion, -65504.0f, 65504.0f) : float2(0,0), all(isfinite(dz)) ? dz : 0.0f, 0.0f);
	// Default the resolve motion to the surface motion; specular-dominated
	// pixels overwrite it at the end of main() with the virtual image's motion.
	gResolveMotion[pixel] = gMotionVectors[pixel].xy;
	gMotionValidity[pixel] = float2(valid && all(isfinite(motion)) ? 1.0f : 0.0f, previousClip.w > 0 ? saturate(previousClip.z / previousClip.w) : 1.0f);
	gTemporalDepth[pixel] = currentClip.w > 0.0f ? saturate(currentClip.z / currentClip.w) : 1.0f;
}

// NRD_FrontEnd_PackNormalAndRoughness for NRD_NORMAL_ENCODING 2 (R10G10B10A2)
// and NRD_ROUGHNESS_ENCODING 1 (linear), the configuration NRD.dll is built with.
float4 PackNrdNormalRoughness(float3 n, float linearRoughness)
{
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	float3 r;
	r.y = n.y * 0.5f + 0.5f;
	r.x = n.x * 0.5f + r.y;
	r.y -= n.x * 0.5f;
	const float roughness = max(saturate(linearRoughness), 1.5f / 512.0f); // keeps the n.z sign bit
	r.z = (n.z < 0.0f ? -roughness : roughness) * 0.5f + 0.5f;
	return float4(r, 0.0f);
}

// Checkerboard (NRD CheckerboardMode::BLACK for diffuse, specular opposite):
// on even NRD frames diffuse owns the cells where x + y is even. Noisy
// signals are packed into the left half of their textures, one texel per
// active 2x1 pair; guides stay full resolution.
bool CheckerboardOn() { return gNrdEnabled != 0u && gNrdCheckerboard != 0u && gLightingView == 0u; }
bool DiffuseCell(uint2 pixel) { return !CheckerboardOn() || ((pixel.x + pixel.y + gCheckerboardPhase) & 1u) == 0u; }
bool SpecularCell(uint2 pixel) { return !CheckerboardOn() || !DiffuseCell(pixel); }
uint2 SignalTexel(uint2 pixel) { return CheckerboardOn() ? uint2(pixel.x >> 1, pixel.y) : pixel; }

// NRD front-end packing (NRD.hlsli: REBLUR_FrontEnd_GetNormHitDist,
// REBLUR_FrontEnd_PackRadianceAndNormHitDist, RELAX_FrontEnd_PackRadianceAndHitDist).
// Hit-distance parameters B = 0.1 and C = 20 are NRD's defaults; A is in world
// metres, like every other distance NRD sees.
// NRD's thresholds assume metres; the engine works in centimetres. viewZ and
// hit distances are converted on the way in (the view matrices on the C++ side).
static const float kNrdMetersPerUnit = 0.01f;

float NrdNormHitDist(float hitDist, float viewZ, float roughness)
{
	float smc = 1.0f - exp2(-200.0f * roughness * roughness);
	smc *= pow(saturate(roughness), 0.5f);
	const float f = (gNrdHitDistA + abs(viewZ) * 0.1f) * lerp(20.0f, 1.0f, smc);
	return max(saturate(hitDist / f), 1e-6f);
}

float4 PackNrdSignal(float3 radiance, float hitDist, float viewZ, float roughness)
{
	radiance = all(isfinite(radiance)) ? clamp(radiance, 0.0f, 65504.0f) : 0.0f;
	hitDist *= kNrdMetersPerUnit;
	viewZ *= kNrdMetersPerUnit;
	if (gNrdReblur < 0.5f)
		return float4(radiance, clamp(hitDist, 0.0f, 65504.0f));
	const float3 yCoCg = float3(
		dot(radiance, float3(0.25f, 0.5f, 0.25f)),
		dot(radiance, float3(0.5f, 0.0f, -0.5f)),
		dot(radiance, float3(-0.25f, 0.5f, -0.25f)));
	return float4(yCoCg, NrdNormHitDist(hitDist, viewZ, roughness));
}

void WriteNrdMiss(uint2 pixel)
{
	if (gNrdEnabled == 0u) return;
	gNrdViewZ[pixel] = 1e7f; // beyond CommonSettings::denoisingRange: NRD skips it
	gNrdNormalRoughness[pixel] = float4(0.5f, 0.5f, 1.0f, 0.0f);
	if (DiffuseCell(pixel)) gNrdDiffuse[SignalTexel(pixel)] = 0.0f;
	if (SpecularCell(pixel)) gNrdSpecular[SignalTexel(pixel)] = 0.0f;
}

float PreExposure()
{
	return gPreExposed != 0u ? PreExposureFromEv100(gPreviousEv100.Load(int3(0, 0, 0))) : 1.0f;
}

float4 PreExposed(float4 radiance)
{
	return float4(radiance.rgb * PreExposure(), radiance.a);
}


float TraceAmbientOcclusion(float3 position, float3 normal, uint sourceInstanceId, uint sourcePrimitiveIndex, uint2 pixel, uint frameIndex, out float hitDistance)
{
	hitDistance = 0.0f;
	const float3 tangent = normalize(abs(normal.z) < 0.999f ? cross(float3(0,0,1), normal) : cross(float3(0,1,0), normal));
	const float3 bitangent = cross(normal, tangent);
	float visible = 0.0f;
	// Stratified hemisphere rays. This was a hardcoded 4, which measured as the
	// single most expensive term in the whole DXR frame; it is now
	// Tunables::dxrAoSamples so the cost can actually be dialled. A
	// Cranley-Patterson rotation gives each frame a fresh estimate, so the
	// temporal resolve converges the lower sample counts rather than preserving
	// a fixed screen-space stipple.
	const uint aoSamples = clamp(gAoSamples, 1u, 8u);
	const float invAoSamples = 1.0f / float(aoSamples);
	[loop] for (uint i = 0u; i < aoSamples; ++i)
	{
		const uint seed = frameIndex * 0x9e3779b9u + i * 0x85ebca6bu;
		// Stratify over the actual sample count, not a fixed quarter, so one or
		// two rays still cover the full [0,1) azimuth range instead of bunching
		// into the first quadrant.
		const float r0 = frac((((float)i + 0.5f) + Hash01(pixel + uint2(i * 19u, i * 47u), seed)) * invAoSamples);
		const float r1 = Hash01(pixel.yx + uint2(17u + i * 13u, 71u + i * 29u), seed + 1u);
		const float phi = 6.2831853f * r0;
		const float z = sqrt(1.0f - r1);
		const float radial = sqrt(r1);
		const float3 aoDir = normalize(tangent * (cos(phi) * radial) + bitangent * (sin(phi) * radial) + normal * z);
		RayDesc aoRay;
		aoRay.Origin = OffsetRayOrigin(position, normal, aoDir); aoRay.Direction = aoDir;
		aoRay.TMin = 0.05f; aoRay.TMax = max(gAoDistance, 0.05f);
		RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> aq;
		aq.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, aoRay);
		while (aq.Proceed()) {
			if (aq.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
				AcceptRayTriangle(aq.CandidateInstanceID(), aq.CandidatePrimitiveIndex(), aq.CandidateTriangleBarycentrics(), 2.0f))
				aq.CommitNonOpaqueTriangleHit();
		}
		
		// Use a smoother quadratic falloff for the AO hit instead of linear,
		// which makes corners and deep crevices look punchier and more natural.
		if (aq.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			float hitDist = saturate(aq.CommittedRayT() / aoRay.TMax);
			visible += hitDist * hitDist; // Quadratic falloff
			hitDistance += aq.CommittedRayT();
		}
		else
		{
			visible += 1.0f;
			hitDistance += aoRay.TMax;
		}
	}
	hitDistance *= invAoSamples;
	return visible * invAoSamples;
}

// Split-sum directional albedo of the GGX lobe (F0 * A + B): the fraction of
// incoming radiance a specular surface reflects towards the viewer.
float3 SpecularAlbedo(HitSurface hs, float3 viewDir)
{
	const float nDotV = saturate(dot(hs.worldNormal, viewDir));
	const float p = saturate(hs.roughness);
	const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), hs.albedo, hs.metalness);
	// Numerically integrated at startup with the same GGX distribution and
	// Smith visibility term as the split-sum prefilter.  This is deliberately
	// a LUT rather than the old fitted approximation so grazing and rough
	// materials preserve reflection energy consistently.
	float2 ab;
	if (gBrdfLutValid != 0u)
		ab = gBrdfLut.SampleLevel(gMaterialSampler, float2(nDotV, p), 0.0f);
	else
	{
		const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
		const float4 c1 = float4( 1.0f,  0.0425f, 1.04f, -0.04f);
		const float4 r = p * c0 + c1;
		const float a004 = min(r.x * r.x, exp2(-9.28f * nDotV)) * r.x + r.y;
		ab = float2(-1.04f, 1.04f) * a004 + r.zw;
	}
	return f0 * ab.x + ab.y;
}

void EvaluateEnvironmentLighting(HitSurface hs, float3 viewDir, out float3 diffuse, out float3 specular)
{
	diffuse = 0.0f; specular = 0.0f;
	if (gEnvironmentMip < 0.0f) return;
	const float nDotV = saturate(dot(hs.worldNormal, viewDir));
	const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), hs.albedo, hs.metalness);

	uint faceWidth, faceHeight, mipCount;
	gDxrEnvironment.GetDimensions(0, faceWidth, faceHeight, mipCount);
	const float maxMip = float(max(int(mipCount) - 1, 0));
	// CubemapPrefilter reserves the final three mips for diffuse irradiance.
	// The first diffuse mip retains enough resolution to avoid directional
	// banding while already representing the cosine-convolved hemisphere.
	const float diffuseMip = max(0.0f, maxMip - 3.0f);
	const float p = saturate(hs.roughness);
	const float specularMip = (p * (1.7f - 0.7f * p)) * diffuseMip;
	const float3 diffuseEnv = gDxrEnvironment.SampleLevel(gMaterialSampler, hs.worldNormal, diffuseMip).rgb * gEnvironmentTint;
	const float3 specularEnv = gDxrEnvironment.SampleLevel(gMaterialSampler, reflect(-viewDir, hs.worldNormal), specularMip).rgb * gEnvironmentTint;

	const float3 specularWeight = SpecularAlbedo(hs, viewDir);
	diffuse = (1.0f - saturate(specularWeight)) * (1.0f - hs.metalness) * hs.albedo * diffuseEnv;
	specular = specularEnv * specularWeight;
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= gOutputSize.x || dtid.y >= gOutputSize.y)
		return;

	const float2 uv = (float2(dtid.xy) + 0.5f + gJitter) / float2(gOutputSize);
	const float2 ndc = float2(uv.x * 2.f - 1.f, 1.f - uv.y * 2.f);

	const float3 dir = normalize(
		gCameraForward +
		gCameraRight * (ndc.x * gAspect * gTanHalfFovY) +
		gCameraUp    * (ndc.y * gTanHalfFovY));

	RayDesc ray;
	ray.Origin = gCameraOrigin;
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

	if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
	{
		// Sky is infinitely distant: rotation affects motion, camera translation does not.
		WriteTemporal(dtid.xy, mul(float4(dir, 0), gWorldToClip), mul(float4(dir, 0), gPreviousWorldToClip), true);
		gTemporalDepth[dtid.xy] = 1.0f;
		gMotionValidity[dtid.xy].y = 1.0f;
		gSurfaceGuide[dtid.xy] = float4(0,0,0,-1);
		gDiffuseAlbedo[dtid.xy] = 0;
		gSpecularAlbedo[dtid.xy] = 0;
		WriteNrdMiss(dtid.xy);
		// Raw background display, matching SkyboxPS's mip-0/intensity-1 sample
		// exactly -- gEnvironmentTint is the GI-tuned scale (see SkyRadiance's
		// comment) and must not dim what the player actually sees as sky.
		gOutput[dtid.xy] = PreExposed(float4(SkyRadiance(dir, gEnvironmentMip, gSkyDisplayScale.xxx), 1.f));
		if (gLightingView == 7u) gOutput[dtid.xy] = PreExposed(float4(saturate(0.5f + gMotionVectors[dtid.xy].xy / 32.0f), gMotionValidity[dtid.xy].x, 1));
		if (gLightingView == 8u) gOutput[dtid.xy] = PreExposed(float4(0,0,0,1));
		if (gLightingView == 9u) gOutput[dtid.xy] = PreExposed(float4(0,0,0,1));
		return;
	}

	const float coneSpread = gTextureFiltering != 0u ? 2.0f * gTanHalfFovY / float(max(gOutputSize.y, 1u)) : 0.0f;
	const HitSurface hs = DecodeHit(q, 0.0f, coneSpread, gSpecularAaStrength);
	gSurfaceGuide[dtid.xy] = float4(hs.worldNormal, hs.roughness);
	// RR expects linear material guides at render resolution.  Keep these
	// independent of lighting so a bright reflection never masquerades as a
	// different surface to the neural denoiser.
	gDiffuseAlbedo[dtid.xy] = float4(hs.albedo * (1.0f - hs.metalness), 1.0f);
	gSpecularAlbedo[dtid.xy] = float4(lerp(0.04f.xxx, hs.albedo, hs.metalness), 1.0f);
	const float3 hitPos = hs.worldPosition;
	const RayGeometryLookup geometry = gRayGeometry[q.CommittedInstanceID()];
	const float4 localPos = float4(mul(q.CommittedWorldToObject3x4(), float4(hitPos, 1)), 1);
	const float3 previousPos = float3(dot(geometry.previousTransform0, localPos), dot(geometry.previousTransform1, localPos), dot(geometry.previousTransform2, localPos));
	WriteTemporal(dtid.xy, mul(float4(hitPos, 1), gWorldToClip), mul(float4(previousPos, 1), gPreviousWorldToClip), geometry.motionHistoryValid != 0u);
	// Offset along the geometric normal, not the ray, so a grazing ray doesn't
	// self-intersect the origin triangle on the shadow traces inside ShadeDirect.
	const float3 shadowOrigin = hs.rayOrigin;
	// These views bypass lighting and TAA to isolate visibility from normals.
	if (gLightingView != 0u)
		WriteNrdMiss(dtid.xy);
	if (gLightingView >= 17u && gLightingView <= 18u)
	{
		// 17: emissive light list size, raw (not pre-exposed) so it can be read
		//     straight off a screenshot. 18: the emissive direct term alone.
		float3 d = 0.0f;
		if (gLightingView == 17u) d = float3(gEmissiveLightCount[0] / 65536.0f, gEmissiveLightCount[0] > 0u ? 1.0f : 0.0f, 0.0f);
		// viewDir is not in scope this early in main(); it is just the direction
		// back to the camera.
		else
		{
			float3 dd, ds; float dHitT;
			SampleEmissiveDirect(hs, normalize(gCameraOrigin - hs.worldPosition), dtid.xy,
				gReflectionFrameIndex, max(gEmissiveLightSamples, 1u), gOutputSize, int2(-1, -1), false, dd, ds, dHitT);
			d = dd + ds;
		}
		gOutput[dtid.xy] = float4(d, 1);
		return;
	}
	if (gLightingView >= 10u && gLightingView <= 14u)
	{
		float3 diagnostic = 0.0f;
		if (gLightingView == 10u) diagnostic = TraceSunVisibility(hs.shadowPosition, hs.geoWorldNormal, gSunDirToLight, hs.instanceId, hs.primitiveIndex).xxx;
		if (gLightingView == 11u) diagnostic = hs.geoWorldNormal * 0.5f + 0.5f;
		if (gLightingView == 12u) diagnostic = hs.worldNormal * 0.5f + 0.5f;
		if (gLightingView == 13u) diagnostic = saturate(dot(hs.worldNormal, gSunDirToLight)).xxx;
		if (gLightingView == 14u) diagnostic = saturate(dot(hs.geoWorldNormal, gSunDirToLight)).xxx;
		gOutput[dtid.xy] = PreExposed(float4(diagnostic, 1));
		return;
	}
	if (gLightingView == 16u)
	{
		// Debug-only: who blocks the sun here? Traces one un-jittered sun ray
		// to its NEAREST occluder, ignoring only the originating triangle.
		//   white   = unoccluded
		//   red     = own instance, front face    magenta = own instance, back face
		//   blue    = other instance, front face  cyan    = other instance, back face
		// Brightness falls off with occluder distance (bright = close).
		// Separates real shadowing from self-shadowing artefacts on thin meshes.
		RayDesc sr;
		sr.Origin = OffsetRayOrigin(hs.shadowPosition, hs.geoWorldNormal, gSunDirToLight);
		sr.Direction = gSunDirToLight;
		sr.TMin = 0.05f; sr.TMax = 100000.0f;
		RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> sq;
		sq.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, sr);
		while (sq.Proceed()) {
			const bool originTri = sq.CandidateInstanceID() == hs.instanceId && sq.CandidatePrimitiveIndex() == hs.primitiveIndex;
			if (sq.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && !originTri &&
				AcceptRayTriangle(sq.CandidateInstanceID(), sq.CandidatePrimitiveIndex(), sq.CandidateTriangleBarycentrics(), 2.0f))
				sq.CommitNonOpaqueTriangleHit();
		}
		float3 occ = 1.0f;
		if (sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			const bool self = sq.CommittedInstanceID() == hs.instanceId;
			const bool front = sq.CommittedTriangleFrontFace();
			const float closeness = lerp(0.35f, 1.0f, saturate(1.0f - sq.CommittedRayT() / 300.0f));
			occ = (self ? (front ? float3(1,0,0) : float3(1,0,1)) : (front ? float3(0,0,1) : float3(0,1,1))) * closeness;
		}
		gOutput[dtid.xy] = PreExposed(float4(occ, 1));
		return;
	}
	if (gLightingView == 15u)
	{
		// Debug-only: raw EvaluateDxrGi output (no albedo/ao multiply, ignores the
		// gEnableIndirectGi toggle entirely) amplified 5000x, so even a very faint
		// but real non-zero signal becomes visible on screen. Distinguishes "GI
		// volume genuinely has no data" (stays pure black even amplified this
		// much) from "there is real data, it's just small/scaled wrong" (shows
		// visible colour/structure once amplified).
		float3 rawGi = EvaluateDxrGi(hs.worldPosition, hs.worldNormal) * 5000.0f;
		gOutput[dtid.xy] = PreExposed(float4(saturate(rawGi), 1));
		return;
	}
	const float3 viewDir = normalize(-ray.Direction);

	float ao = hs.ao;
	float aoHitDistance = max(gAoDistance, 0.05f);
	const bool diffuseCell = DiffuseCell(dtid.xy);
	const bool specularCell = SpecularCell(dtid.xy);
	// In checkerboard mode only the diffuse cells need traced AO: the rest
	// only feed NRD's specular signal.
	if (gEnableAmbientOcclusion != 0u && diffuseCell)
		ao *= lerp(1.0f, TraceAmbientOcclusion(hs.worldPosition, hs.geoWorldNormal, hs.instanceId, hs.primitiveIndex, dtid.xy, gReflectionFrameIndex, aoHitDistance), saturate(gAoStrength));
	float3 envDiffuse, envSpecular;
	EvaluateEnvironmentLighting(hs, viewDir, envDiffuse, envSpecular);
	if (gEnableEnvironmentLighting == 0u) { envDiffuse = 0.0f; envSpecular = 0.0f; }
	const float3 gi = gEnableIndirectGi != 0u ? (1.0f - hs.metalness) * hs.albedo * ao * EvaluateDxrGi(hitPos, hs.worldNormal) : 0.0f;
	float3 color = hs.emissive;
	// Emissive geometry sampled as area lights (RIS + one shadow ray): what
	// makes a lamp light the pavement instead of merely glowing. The diffuse
	// half joins the indirect signal so NRD denoises it with everything else.
	float3 emissiveDiffuse = 0.0f, emissiveSpecular = 0.0f;
	float emissiveHitDistance = 0.0f;
	if (gEnableDirectLighting != 0u)
	{
		// Reproject with the surface motion this pass already wrote: the
		// reservoir belongs to the surface, so it follows the surface.
		const float2 motion = gMotionVectors[dtid.xy].xy;
		const int2 previousPixel = int2(round(float2(dtid.xy) + motion));
		const bool historyValid = gTemporalHistoryValid != 0u && gMotionValidity[dtid.xy].x > 0.5f;
		SampleEmissiveDirect(hs, viewDir, dtid.xy, gReflectionFrameIndex, gEmissiveLightSamples,
			gOutputSize, previousPixel, historyValid, emissiveDiffuse, emissiveSpecular, emissiveHitDistance);
	}
	const float3 indirectDiffuse = gi + envDiffuse * ao + emissiveDiffuse;
	if (gNrdEnabled != 0u && gLightingView == 0u)
	{
		// NRD denoises the stochastic (AO-traced) indirect diffuse on its own.
		// It must not see material detail, so divide out diffuse albedo and
		// texture AO here; NrdCompositeCS multiplies them back afterwards using
		// max(gDiffuseAlbedo.rgb * gDiffuseAlbedo.a, 0.01), the exact inverse.
		const float3 demodulator = gDiffuseAlbedo[dtid.xy].rgb * hs.ao;
		gDiffuseAlbedo[dtid.xy].a = hs.ao;
		float3 radiance = indirectDiffuse / max(demodulator, 0.01f);
		radiance = all(isfinite(radiance)) ? clamp(radiance, 0.0f, 65504.0f) : 0.0f;
		const float nrdViewZ = mul(float4(hitPos, 1), gWorldToView).z;   // same matrix NRD reconstructs with
		if (diffuseCell) gNrdDiffuse[SignalTexel(dtid.xy)] = PackNrdSignal(radiance * PreExposure(), aoHitDistance, nrdViewZ, 1.0f);
		gNrdViewZ[dtid.xy] = nrdViewZ * kNrdMetersPerUnit;
		gNrdNormalRoughness[dtid.xy] = PackNrdNormalRoughness(hs.worldNormal, hs.roughness);
	}
	else
	{
		color += indirectDiffuse;
	}
	if (gEnableDirectLighting != 0u)
		color += ShadeDirect(hs, shadowOrigin, viewDir, gSunDirToLight, gLightCount, gSunRadiance, gAmbientIntensity, clamp(gSunShadowSamples, 1u, 4u),
			frac(float(gReflectionFrameIndex) * 0.618034f));
	// Replace environment specular smoothly; do not add the same sky twice.
	// Keep the prefiltered environment as a conservative fallback at the rough
	// end where one scene ray is too noisy without a dedicated denoiser.
	// A couple of stochastic scene rays cannot fully converge a broad GGX
	// lobe while the camera is moving. The environment remains the fallback
	// at the rough end of the cutoff, while smooth surfaces retain contacts.
	// Environment-only specular is infinitely far away as far as NRD's
	// reflection reprojection is concerned.
	float specularHitDistance = 65504.0f;
	const float rayWeight = 1.0f - smoothstep(gReflectionRoughnessCutoff * 0.70f,
		max(gReflectionRoughnessCutoff, 0.001f), hs.roughness);
	if (gEnableReflections != 0u && specularCell && hs.roughness < gReflectionRoughnessCutoff && rayWeight > 0.01f)
	{
		float3 reflection = 0.0f;
		float reflectionHitDistance = 0.0f;
		// Spend the per-pixel budget only where it buys variance reduction.
		// The estimator below is an average of unbiased GGX samples, so the
		// sample count changes noise, never the expected result.
		//  - A near-mirror's GGX lobe is so narrow that extra samples land on
		//    almost the same point: one ray is already the converged answer.
		//  - In the fade band above 0.7 x cutoff, only rayWeight of whatever is
		//    traced survives the lerp below, and its noise is scaled down by the
		//    same factor.
		// Previously every pixel under the cutoff paid the full budget, which
		// measured as most of a 56 ms reflection pass on the Bistro scene.
		const uint maxReflectionSamples = clamp(gReflectionSamples, 1u, 8u);
		const float lobe = saturate(hs.roughness / max(gReflectionRoughnessCutoff * 0.70f, 1e-3f));
		const uint reflectionSamples = clamp((uint)ceil(float(maxReflectionSamples) * lobe * rayWeight), 1u, maxReflectionSamples);
		[loop] for (uint sample = 0u; sample < reflectionSamples; ++sample)
		{
			const uint seed = gReflectionFrameIndex * 5u + sample * 0x9e37u;
			const float2 xi = float2(Hash01(dtid.xy + uint2(sample * 71u, sample * 29u), seed),
				Hash01(dtid.yx + uint2(0x9e37u + sample * 13u, 0x85ebu + sample * 47u), seed + 1u));
			float3 reflectionDir, brdfOverPdf;
			SampleGgxReflection(viewDir, hs.worldNormal, hs.roughness,
				lerp(float3(0.04f, 0.04f, 0.04f), hs.albedo, hs.metalness), xi,
				reflectionDir, brdfOverPdf);
			[branch] if (any(brdfOverPdf > 0.0f))
			{
				reflection += TraceReflection(OffsetRayOrigin(hs.worldPosition, hs.geoWorldNormal, reflectionDir), reflectionDir, gSunDirToLight, gLightCount, gSunRadiance, gAmbientIntensity, gEnvironmentMip, gEnvironmentTint, gEnableIndirectGi != 0u, coneSpread * q.CommittedRayT(), coneSpread, gSpecularAaStrength) * brdfOverPdf;
				reflectionHitDistance += gLastReflectionHitDistance;
			}
		}
		reflection /= float(reflectionSamples);
		envSpecular = lerp(envSpecular, reflection, rayWeight);
		specularHitDistance = lerp(specularHitDistance, reflectionHitDistance / float(reflectionSamples), rayWeight);
	}
	// Specular occlusion relaxes toward smooth surfaces, preserving mirrors.
	// The emissive area lights' specular half joins here rather than being added
	// straight to the image, so it goes through NRD with everything else --
	// previously the diffuse half was denoised and this one was not, which left
	// lamp highlights sparkling on top of an otherwise clean frame.
	const float3 specular = envSpecular * lerp(1.0f, ao, hs.roughness) + emissiveSpecular;

	// NRD derives specular reprojection from the hit distance, so the combined
	// signal needs a combined one. Weight by luminance in INVERSE distance: that
	// is the space parallax actually lives in, and it takes the 65504 "the
	// environment is infinitely far" sentinel in its stride, where a plain
	// average of it with a lamp 3 m away would produce nonsense.
	{
		const float3 kL = float3(0.2126f, 0.7152f, 0.0722f);
		const float envLuma = dot(max(envSpecular * lerp(1.0f, ao, hs.roughness), 0.0f), kL);
		const float emiLuma = dot(max(emissiveSpecular, 0.0f), kL);
		if (emiLuma > 0.0f && emissiveHitDistance > 0.0f)
		{
			const float invEnv = specularHitDistance > 0.0f ? 1.0f / specularHitDistance : 0.0f;
			const float invEmi = 1.0f / emissiveHitDistance;
			const float invMean = (envLuma * invEnv + emiLuma * invEmi) / max(envLuma + emiLuma, 1e-6f);
			specularHitDistance = invMean > 1e-9f ? min(1.0f / invMean, 65504.0f) : 65504.0f;
		}
	}

	// Resolve-time motion for a reflection.
	//
	// The temporal resolve reprojects the COMPOSITED image, so it moves a
	// mirror's reflection with the mirror's own motion vector. A reflected
	// image does not move with the surface: it moves with the virtual image
	// that sits behind the surface, along the view ray, at the reflection's hit
	// distance. Reprojecting it as if it were painted on the surface is what
	// smeared reflections whenever the camera moved -- worst on smooth,
	// curved surfaces, where the reflection sweeps fastest.
	//
	// Blend by how much this pixel actually IS its reflection, so a rough or
	// mostly-diffuse surface keeps the surface motion it wants. NRD is not
	// affected: it still reads gMotionVectors and derives specular
	// reprojection from the hit distance itself.
	[branch] if (specularHitDistance < 65504.0f && specularHitDistance > 0.0f)
	{
		const float3 toCamera = normalize(gCameraOrigin - hitPos);
		const float3 virtualPos = hitPos - toCamera * specularHitDistance;
		const float2 virtualMotion = ClipToPixel(mul(float4(virtualPos, 1), gPreviousWorldToClip))
			- ClipToPixel(mul(float4(hitPos, 1), gWorldToClip));
		const float3 kLuma = float3(0.2126f, 0.7152f, 0.0722f);
		const float specularLuma = dot(max(specular, 0.0f), kLuma);
		const float otherLuma = dot(max(color, 0.0f), kLuma);
		const float dominance = specularLuma / max(specularLuma + otherLuma, 1e-4f);
		// Only near-mirror surfaces have a well-defined virtual image; a broad
		// lobe has no single one, and its blur hides the error anyway.
		const float smoothness = 1.0f - smoothstep(0.0f, max(gReflectionRoughnessCutoff, 1e-3f), hs.roughness);
		const float w = saturate(dominance * smoothness);
		const float2 blended = lerp(gMotionVectors[dtid.xy].xy, virtualMotion, w);
		if (all(isfinite(blended))) gResolveMotion[dtid.xy] = clamp(blended, -65504.0f, 65504.0f);
	}
	if (gNrdEnabled != 0u && gLightingView == 0u)
	{
		// Same demodulation contract as the diffuse signal: NrdCompositeCS
		// multiplies by gSpecularAlbedo.rgb afterwards.
		const float3 demodulator = max(SpecularAlbedo(hs, viewDir) * lerp(1.0f, ao, hs.roughness), 0.01f);
		gSpecularAlbedo[dtid.xy] = float4(demodulator, 1.0f);
		float3 radiance = specular / demodulator;
		radiance = all(isfinite(radiance)) ? clamp(radiance, 0.0f, 65504.0f) : 0.0f;
		if (specularCell) gNrdSpecular[SignalTexel(dtid.xy)] = PackNrdSignal(radiance * PreExposure(), specularHitDistance,
			dot(hitPos - gCameraOrigin, gCameraForward), hs.roughness);
	}
	else
	{
		color += specular;
	}
	if (gLightingView == 1u) color = ao.xxx;
	if (gLightingView == 2u) color = envDiffuse * ao + envSpecular;
	if (gLightingView == 3u) color = gi;
	if (gLightingView == 4u) color = hs.albedo;
	if (gLightingView == 5u) color = float3(hs.ao, hs.roughness, hs.metalness);
	if (gLightingView == 6u) color = lerp(float3(0,0.05f,0.8f), float3(1,0.15f,0), saturate(hs.textureMip / 8.0f));
	if (gLightingView == 7u) color = float3(saturate(0.5f + gMotionVectors[dtid.xy].xy / 32.0f), gMotionValidity[dtid.xy].x);
	if (gLightingView == 8u) color = (1.0f - gTemporalDepth[dtid.xy]).xxx;
	if (gLightingView == 9u) color = float3(saturate(hs.roughnessAdjustment*8.0f),hs.roughness,0);
	// Reject invalid math without clipping legitimate HDR radiance.
	color = all(isfinite(color)) ? max(color, 0.0f) : float3(1, 0, 1);
	gOutput[dtid.xy] = PreExposed(float4(color, 1.f));
}
