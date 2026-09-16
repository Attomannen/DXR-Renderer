// Deferred lighting resolve. Fullscreen pass: reads the G-buffer + depth, runs
// the same PBR light loop as the forward shader, writes HDR radiance.
// Pair with PostprocessVS.
#include "Common.hlsli"
#include "PBRFunctions.hlsli"
#include "GiCommon.hlsli"

struct FSInput
{
	float4 position : SV_POSITION;
	float2 uv       : UV;
};

Texture2D GBufferAlbedo   : register(t10);
Texture2D GBufferNormal   : register(t11);
Texture2D GBufferMaterial : register(t12);
Texture2D GBufferEmissive : register(t13);
Texture2D GBufferDepth    : register(t14);

// Deferred structured light buffer -- bypasses the b2 8-light cbuffer cap.
struct GpuLight
{
	float3 position;
	float  range;
	float3 color;
	float  radius;         // 0 = punctual point light, >0 = soft area light
	float3 spotDir;        // cone axis (light -> scene); spot when spotCosOuter > 0
	float  spotCosOuter;
	float  spotCosInner;
	float  shadowSlot;     // reserved for point/spot shadow atlas
	float2 _lpad;
};

StructuredBuffer<GpuLight> DeferredLights : register(t15);
cbuffer DeferredLightParams : register(b6)
{
	uint  gDeferredLightCount;
	uint  gSsaoEnabled;
	float2 _deferredLightPad;
};

// Blurred screen-space AO (SSAOBlurPS). Only read when gSsaoEnabled.
Texture2D SsaoTexture : register(t18);

// Cascaded shadow maps for the directional light.
Texture2DArray ShadowMap : register(t19);
Texture2D DxrSunShadow : register(t23);
SamplerState PointSampler : register(s1);
SamplerComparisonState ShadowCmp : register(s2);
cbuffer ShadowParams : register(b9)
{
	float4x4 gCascadeViewProj[4];
	float4   gCascadeSplits;
	float4   gCascadeTexelWorld;   // world size of one shadow-map texel, per cascade
	float4   gCascadeDepthRange;   // world units across NDC z 0..1, per cascade
	float    gShadowTexel;         // 1 / shadow-map resolution
	float    gShadowDepthBias;     // WORLD units, slope-scaled, / cascade depth range
	float    gShadowStrength;
	float    gShadowEnabled;
	float    gShadowNormalOffset;  // in texels (scaled by gCascadeTexelWorld[c])
	float    gShadowShowCascades;
	float    gContactLength;       // world units marched toward the sun; 0 = contact shadows off
	float    gContactThickness;    // max depth gap counted as an occluder
	float    gDxrSunShadows;
	float    gDxrSunShadowDebug;
};

float FilterDxrSunShadow(float2 uv, float receiverDepth, float3 receiverNormal)
{
	uint width, height; DxrSunShadow.GetDimensions(width, height);
	const float2 texel = 1.0f / float2(width, height);
	const float taps[5] = { 0.0f, 1.0f, -1.0f, 2.0f, -2.0f };
	float sum = 0.0f, weightSum = 0.0f;
	[unroll] for (uint i = 0; i < 5; ++i)
	{
		const float2 offset = (i == 0u) ? 0.0f : (i & 1u ? float2(taps[i], 0) : float2(0, taps[i]));
		const float2 sampleUv = saturate(uv + offset * texel);
		const float neighborDepth = GBufferDepth.SampleLevel(PointSampler, sampleUv, 0).r;
		const float depthWeight = (receiverDepth >= 1.0f || neighborDepth >= 1.0f) ?
			(receiverDepth >= 1.0f && neighborDepth >= 1.0f ? 1.0f : 0.0f) :
			exp(-abs(neighborDepth - receiverDepth) * 180.0f);
		const float3 neighborNormal = normalize(GBufferNormal.SampleLevel(PointSampler, sampleUv, 0).xyz * 2.0f - 1.0f);
		const float normalWeight = pow(saturate(dot(receiverNormal, neighborNormal)), 8.0f);
		const float w = depthWeight * normalWeight;
		sum += DxrSunShadow.SampleLevel(PointSampler, sampleUv, 0).r * w;
		weightSum += w;
	}
	return weightSum > 1e-4f ? sum / weightSum : DxrSunShadow.Sample(PointSampler, uv).r;
}

// Point / spot shadow atlas. One entry per assigned tile; a spot uses 1, a point
// uses 6 (cube faces) starting at shadowSlot. Filled by DeferredRenderer::RenderLocalShadows.
struct LocalShadow
{
	float4x4 viewProj;   // column-major interpretation of raw engine data, as gCascadeViewProj
	float4   tile;   // xy = atlas-uv min, zw = atlas-uv extent
	float4   misc;   // x = valid, y = type (0 spot / 1 point face), z = NDC bias, w = pad
};
StructuredBuffer<LocalShadow> LocalShadows : register(t20);
Texture2D LocalShadowAtlas : register(t21);

// +X,-X,+Y,-Y,+Z,-Z -- must match kCubeFwd in DeferredRenderer::RenderLocalShadows.
int CubeFace(float3 d)
{
	float3 a = abs(d);
	if (a.x >= a.y && a.x >= a.z) return d.x > 0.0f ? 0 : 1;
	if (a.y >= a.z)               return d.y > 0.0f ? 2 : 3;
	return d.z > 0.0f ? 4 : 5;
}

float SampleLocalShadow(int slot, float3 worldPos, float ndl)
{
	LocalShadow s = LocalShadows[slot];
	if (s.misc.x < 0.5f) return 1.0f;

	float4 c = mul(s.viewProj, float4(worldPos, 1.0f));
	if (c.w <= 0.0f) return 1.0f;
	c.xyz /= c.w;

	float2 uv = float2(c.x * 0.5f + 0.5f, 0.5f - c.y * 0.5f);
	if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || c.z < 0.0f || c.z > 1.0f)
		return 1.0f;

	const float texel = 1.0f / 4096.0f;   // kLocalAtlasRes
	// Inset so the 3x3 PCF never samples across a tile / cube-face boundary.
	float2 auv = s.tile.xy + clamp(uv, 2.0f * texel / s.tile.zw, 1.0f - 2.0f * texel / s.tile.zw) * s.tile.zw;
	// slope-scaled NDC bias: grazing faces (low ndl) need far more.
	float d = c.z - s.misc.z * (1.0f + 6.0f * saturate(1.0f - ndl));

	float sh = 0.0f;
	[unroll] for (int y = -1; y <= 1; ++y)
	[unroll] for (int x = -1; x <= 1; ++x)
		sh += LocalShadowAtlas.SampleCmpLevelZero(ShadowCmp, auv + float2(x, y) * texel, d);
	return sh / 9.0f;
}

float3 ShadeDeferredLight(GpuLight L, float3 diffuseColor, float3 specularColor,
                          float3 n, float roughness, float3 toEye, float3 worldPos)
{
	float3 lit;
	if (L.spotCosOuter > 0.0f)
		lit = EvaluateSpotLight(diffuseColor, specularColor, n, roughness,
			L.color, L.range, L.position, L.spotDir,
			acos(clamp(L.spotCosOuter, -1.0f, 1.0f)),
			acos(clamp(L.spotCosInner, -1.0f, 1.0f)), toEye, worldPos);
	else if (L.radius == 0.0f)
		lit = EvaluatePointLight(diffuseColor, specularColor, n, roughness,
			L.color, L.range, L.position, toEye, worldPos);
	else
		lit = EvaluateSoftAreaLight(diffuseColor, specularColor, n, roughness,
			L.color, L.radius, L.range, L.position, toEye, worldPos);

	if (L.shadowSlot >= 0.0f)
	{
		float3 toFrag = worldPos - L.position;
		float  ndl    = saturate(dot(n, normalize(-toFrag)));
		int    slot   = (int)L.shadowSlot;
		if (L.spotCosOuter <= 0.0f) slot += CubeFace(toFrag);   // point light: pick cube face
		lit *= SampleLocalShadow(slot, worldPos, ndl);
	}

	return lit;
}

// --- Stage-1 emissive-GI irradiance volume ---
StructuredBuffer<float4> GiSH : register(t22);   // 9 float4 (rgb) per probe
cbuffer GiVolumeBuffer : register(b13)
{
	float4 gGiOrigin;    // xyz world min corner, w = enabled
	float4 gGiSpacing;   // xyz per-axis spacing, w = intensity
	int4   gGiCounts;    // xyz probe counts, w = asfloat(auto-seal strength 0..1)
};

// Trilinear sky visibility from the GI probe grid (slot-0 .w per probe). 1 =
// open to the sky, 0 = sealed interior. Positions outside the grid read 1.
float GiSkyVisibility(float3 worldPos)
{
	if (gGiOrigin.w < 0.5f) return 1.0f;
	float3 counts = float3(gGiCounts.xyz);
	float3 gfRaw = (worldPos - gGiOrigin.xyz) / gGiSpacing.xyz;
	// Clamp into the grid so boundary surfaces (floor just below the lowest probe
	// row, walls at the edge) still sample interior probes and seal correctly.
	// Fade back to "open" for points well outside the volume so exterior geometry
	// near a building's GI box isn't wrongly darkened.
	float3 gf = clamp(gfRaw, 0.0f, counts - 1.001f);
	float outside = length(max(0.0f, abs(gfRaw - (counts - 1.0f) * 0.5f) - (counts - 1.0f) * 0.5f));
	float edgeFade = saturate(outside / 1.5f);   // ~1.5 probe spacings

	int3 b0 = clamp((int3)floor(gf), 0, gGiCounts.xyz - 2);
	float3 fr = saturate(gf - (float3)b0);

	float vis = 0.0f;
	float totalWeight = 0.0f;
	[unroll] for (int o = 0; o < 8; ++o)
	{
		int3 off = int3(o & 1, (o >> 1) & 1, (o >> 2) & 1);
		int3 c = b0 + off;
		float3 tw = lerp(1.0f - fr, fr, (float3)off);
		float w = tw.x * tw.y * tw.z;
		uint idx = c.x + c.y * gGiCounts.x + c.z * gGiCounts.x * gGiCounts.y;
		if (GiSH[idx * 9 + 8].w < 0.5f) continue;
		vis += GiSH[idx * 9].w * w;
		totalWeight += w;
	}
	const float reconstructedVisibility = totalWeight > 1e-4f ? vis / totalWeight : 1.0f;
	return lerp(saturate(reconstructedVisibility), 1.0f, edgeFade);
}

float3 EvaluateGI(float3 worldPos, float3 n)
{
	if (gGiOrigin.w < 0.5f) return 0.0f;

	float3 counts = float3(gGiCounts.xyz);
	float3 gfRaw = (worldPos - gGiOrigin.xyz) / gGiSpacing.xyz;
	// Clamp into the grid + fade out over ~1 probe spacing instead of a hard cutoff,
	// so GI doesn't pop to black when the camera crosses the volume boundary.
	float3 gf = clamp(gfRaw, 0.0f, counts - 1.001f);
	float outG = length(max(0.0f, abs(gfRaw - (counts - 1.0f) * 0.5f) - (counts - 1.0f) * 0.5f));
	float giFade = saturate(1.0f - outG);
	if (giFade <= 0.0f) return 0.0f;

	int3 b0 = clamp((int3)floor(gf), 0, gGiCounts.xyz - 2);
	float3 fr = saturate(gf - (float3)b0);

	float3 sumIrr = 0.0f;
	float  sumW = 1e-4f;
	[unroll] for (int o = 0; o < 8; ++o)
	{
		int3 off = int3(o & 1, (o >> 1) & 1, (o >> 2) & 1);
		int3 c = b0 + off;
		float3 tw = lerp(1.0f - fr, fr, (float3)off);
		float w = tw.x * tw.y * tw.z;

		float3 ppos = gGiOrigin.xyz + (float3)c * gGiSpacing.xyz;
		float3 toP = ppos - worldPos;
		float d = length(toP);
		if (d > 1e-3f) w *= saturate(dot(n, toP / d) * 0.5f + 0.5f);   // soft backface reject

		uint idx = c.x + c.y * gGiCounts.x + c.z * gGiCounts.x * gGiCounts.y;
		// Slot 8.w is the explicit validity bit set by both raster and DXR
		// probe projection. Cleared probes are not black lighting samples.
		if (GiSH[idx * 9 + 8].w < 0.5f) continue;
		float3 sh[9];
		uint base = idx * 9;
		[unroll] for (uint i = 0; i < 9; ++i) sh[i] = GiSH[base + i].rgb;

		sumIrr += EvalGiSH(sh, n) * w;
		sumW += w;
	}
	return (sumIrr / sumW) * gGiSpacing.w * giFade;
}

int PickCascade(float viewZ)
{
	int c = 3;
	[unroll] for (int i = 0; i < 3; ++i)
		if (viewZ < gCascadeSplits[i]) { c = i; break; }
	return c;
}

// PCF 5x5 in one cascade. p = normal-offset world position, ndl = N.L.
float SampleCascadePCF(int c, float3 worldPos, float3 worldN, float ndl)
{
	float3 p = worldPos + worldN * (gShadowNormalOffset * gCascadeTexelWorld[c]);

	float4 sc = mul(gCascadeViewProj[c], float4(p, 1.0f));
	sc.xyz /= sc.w;
	float2 uv = float2(sc.x * 0.5f + 0.5f, 0.5f - sc.y * 0.5f);
	if (uv.x < 0.f || uv.x > 1.f || uv.y < 0.f || uv.y > 1.f || sc.z > 1.0f || sc.z < 0.f)
		return 1.0f;

	// world-space depth bias -> NDC (consistent across cascades), slope-scaled.
	float worldBias = gShadowDepthBias * (1.0f + 2.0f * saturate(1.0f - ndl));
	float d = sc.z - worldBias / max(gCascadeDepthRange[c], 1.0f);

	float s = 0.0f;
	[unroll] for (int y = -2; y <= 2; ++y)
	[unroll] for (int x = -2; x <= 2; ++x)
		s += ShadowMap.SampleCmpLevelZero(ShadowCmp, float3(uv + float2(x, y) * gShadowTexel, c), d);
	return s / 25.0f;
}

float SampleDirectionalShadow(float3 worldPos, float3 worldN, float viewZ)
{
	if (gShadowEnabled < 0.5f) return 1.0f;

	float ndl = saturate(dot(worldN, GetSunDirectionToLight()));

	int c = PickCascade(viewZ);
	float lit = SampleCascadePCF(c, worldPos, worldN, ndl);

	// Blend into the next cascade over the last slice of this one's range,
	// so the cascade seam isn't a hard band.
	if (c < 3)
	{
		float near = (c == 0) ? 0.0f : gCascadeSplits[c - 1];
		float far  = gCascadeSplits[c];
		float t = saturate((viewZ - lerp(near, far, 0.80f)) / max(far - lerp(near, far, 0.80f), 1e-3f));
		if (t > 0.0f)
			lit = lerp(lit, SampleCascadePCF(c + 1, worldPos, worldN, ndl), t);
	}

	return lerp(1.0f, lit, gShadowStrength);
}

// Clustered light grid (filled by ClusterCullCS). When gClusterLightCount == 0
// clustering is off and the shader loops all lights instead.
StructuredBuffer<uint> ClusterLightIndices : register(t16);
StructuredBuffer<uint> ClusterLightCounts  : register(t17);
cbuffer ClusterParams : register(b7)
{
	float4x4 _cProjToView;
	float4x4 _cWorldToView;
	uint2 gClusterTileCount;
	uint  gClusterTilePx;
	uint  gClusterZSlices;
	float gClusterNear;
	float gClusterFar;
	uint  gClusterLightCount;
	uint  gClusterMaxPerCluster;
	float2 gClusterScreen;
	float2 _cParamsPad;
};


// Screen-space contact shadow: a short depth-buffer ray-march from the shaded
// point toward the sun. Catches the fine contact occlusion the cascades are too
// coarse to resolve (objects sitting on floors, small gaps). Returns 0..1,
// 1 = unoccluded. Marches in view space, re-projecting each step to sample depth.
float ContactShadow(float3 viewPos, float3 Lview, float2 uv)
{
	if (gContactLength <= 0.0f) return 1.0f;

	const int kSteps = 16;
	float3 rayStep = Lview * (gContactLength / (float)kSteps);
	float3 p = viewPos + rayStep * 2.0f;   // start clear of the origin surface

	float occ = 0.0f;
	[loop] for (int i = 2; i < kSteps; ++i)
	{
		float4 clip = mul(CameraToProjection, float4(p, 1.0f));
		if (clip.w <= 0.0f) break;
		float2 suv = clip.xy / clip.w;
		suv = float2(suv.x * 0.5f + 0.5f, 0.5f - suv.y * 0.5f);
		if (suv.x < 0.0f || suv.x > 1.0f || suv.y < 0.0f || suv.y > 1.0f) break;

		float sd = GBufferDepth.SampleLevel(PointSampler, suv, 0).r;
		float4 svH = mul(ProjectionToCamera,
			float4(suv.x * 2.0f - 1.0f, 1.0f - suv.y * 2.0f, sd, 1.0f));
		float sceneZ = svH.z / svH.w;

		// >0 : the ray has passed behind a surface. Min threshold scales with the
		// ray point's own depth so grazing floors don't self-occlude at distance.
		float delta = p.z - sceneZ;
		float minGap = max(0.05f, p.z * 0.004f);
		if (delta > minGap && delta < gContactThickness)
		{
			occ = 1.0f - (float)i / (float)kSteps;   // soft toward the ray tip
			break;
		}
		p += rayStep;
	}

	// fade out near the screen edge where samples run off-buffer
	float2 e = abs(uv * 2.0f - 1.0f);
	float edge = saturate((1.0f - max(e.x, e.y)) * 8.0f);
	return 1.0f - occ * edge;
}

uint ClusterIndexForPixel(float2 pixelXY, float linearViewZ)
{
	uint2 tile = (uint2)(pixelXY / (float)gClusterTilePx);
	tile = min(tile, gClusterTileCount - 1);
	float sliceF = log(max(linearViewZ, gClusterNear) / gClusterNear)
	             / log(gClusterFar / gClusterNear) * (float)gClusterZSlices;
	uint slice = (uint)clamp(sliceF, 0.0f, (float)(gClusterZSlices - 1));
	return tile.x + tile.y * gClusterTileCount.x
	     + slice * gClusterTileCount.x * gClusterTileCount.y;
}

struct LightingOutput
{
	float4 color   : SV_TARGET0;
	float4 iblSpec : SV_TARGET1;   // probe IBL specular, for the SSR-over-probe resolve
};

LightingOutput main(FSInput input)
{
	LightingOutput O;
	O.iblSpec = 0.0f;

	float2 uv = input.uv;

	float depth = GBufferDepth.Sample(PointSampler, uv).r;

	// Reconstruct a world-space ray/position from hardware depth.
	float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
	float4 clip = float4(ndc, depth, 1.0f);
	float4 viewH = mul(ProjectionToCamera, clip); // ProjectionToCamera holds inverse(projection)
	float3 viewPos = viewH.xyz / viewH.w;
	float3 worldPos = mul(CameraToWorld, float4(viewPos, 1.0f)).xyz;

	if (depth >= 1.0f)
	{
		// Sky: show the environment along the view ray.
		float3 dir = normalize(worldPos - CameraToWorld._m03_m13_m23);
		O.color = float4(environmentTexture.SampleLevel(defaultSampler, dir, 0).rgb, 1.0f);
		return O;
	}

	float3 albedo   = GBufferAlbedo.Sample(PointSampler, uv).rgb;
	float3 n        = normalize(GBufferNormal.Sample(PointSampler, uv).xyz);
	float3 orm      = GBufferMaterial.Sample(PointSampler, uv).rgb;
	float3 emissive = GBufferEmissive.Sample(PointSampler, uv).rgb;

	float ao        = orm.r;
	float roughness = orm.g;
	float metalness = orm.b;

	if (gSsaoEnabled)
		ao *= SsaoTexture.Sample(PointSampler, uv).r;

	float3 toEye = normalize(CameraToWorld._m03_m13_m23 - worldPos);
	float3 specularColor = lerp((float3) 0.04f, albedo, metalness);
	float3 diffuseColor  = lerp((float3) 0.00f, albedo, 1.0f - metalness);

	float3 iblSpec;
	// Keep the IBL contribution, but also provide a true uniform ambient term.
	// A Uniform Ambient light must remain visible when the default cubemap is
	// black or absent; otherwise its color only multiplies zero and appears to
	// do nothing in an empty editor scene.
	float3 ambiance = AmbientLightColor.rgb * EvaluateAmbiance(
		environmentTexture, n, n, toEye, roughness, ao, diffuseColor, specularColor, worldPos, iblSpec);
	ambiance += AmbientLightColor.rgb * diffuseColor * ao * 0.2f;
	O.iblSpec = float4(AmbientLightColor.rgb * iblSpec, 1.0f);

	float3 directional;
	if (DirectionalLightSoftness == 0.f)
	{
		directional = EvaluateDirectionalLight(
			diffuseColor, specularColor, n, roughness,
			DirectionalLightColor.xyz, GetSunDirectionToLight(), toEye);
	}
	else
	{
		directional = EvaluateSoftDirectionalLight(
			diffuseColor, specularColor, n, roughness, DirectionalLightSoftness,
			DirectionalLightColor.xyz, GetSunDirectionToLight(), toEye);
	}

	float sunShadow = gDxrSunShadows > 0.5f ? FilterDxrSunShadow(uv, GBufferDepth.SampleLevel(PointSampler, uv, 0).r, n) : SampleDirectionalShadow(worldPos, n, viewPos.z);
	if (gDxrSunShadowDebug > 0.5f) { O.color = float4(sunShadow.xxx, 1.0f); O.iblSpec = 0.0f; return O; }

	// Screen-space contact shadow on top of the cascades (directional only).
	float csContact = 1.0f;
	[branch] if (gDxrSunShadows < 0.5f && gShadowEnabled > 0.5f && gContactLength > 0.0f)
	{
		float3 Lworld = GetSunDirectionToLight();
		float grazing = dot(n, Lworld);
		if (grazing > 0.15f)   // skip near-tangent angles (grazing floors self-occlude)
		{
			float3 Lview = normalize(mul((float3x3)WorldToCamera, Lworld));
			csContact = ContactShadow(viewPos, Lview, uv);
			// ease in over the first bit of the angle range
			csContact = lerp(1.0f, csContact, saturate((grazing - 0.15f) * 5.0f));
			sunShadow *= csContact;
		}
	}

	if (gShadowShowCascades > 3.5f)   // BENCH_GI_VIZ: raw GI irradiance
	{
		O.color = float4(EvaluateGI(worldPos, n), 1.0f); O.iblSpec = 0.0f; return O;
	}

	if (gShadowShowCascades > 2.5f)   // BENCH_LOCALSH_VIZ: first slotted local light's projection/sample
	{
		float3 dbg = float3(0.0f, 0.0f, 0.15f);   // no slotted light here
		[loop] for (uint q = 0; q < gDeferredLightCount; ++q)
		{
			GpuLight L = DeferredLights[q];
			if (L.shadowSlot >= 0.0f)
			{
				LocalShadow s = LocalShadows[(int)L.shadowSlot];
				float4 cc = mul(s.viewProj, float4(worldPos, 1.0f));
				if (cc.w > 0.0f)
				{
					cc.xyz /= cc.w;
					float2 uvv = float2(cc.x * 0.5f + 0.5f, 0.5f - cc.y * 0.5f);
					bool inb = uvv.x >= 0.0f && uvv.x <= 1.0f && uvv.y >= 0.0f && uvv.y <= 1.0f && cc.z >= 0.0f && cc.z <= 1.0f;
					dbg = inb ? float3(uvv, cc.z) : float3(1.0f, 0.0f, 0.0f);   // red = outside light frustum
				}
				else dbg = float3(0.4f, 0.0f, 0.4f);   // behind the light
				break;
			}
		}
		O.color = float4(dbg, 1.0f); O.iblSpec = 0.0f; return O;
	}

	if (gShadowShowCascades > 1.5f)   // BENCH_CONTACT_VIZ: isolate the contact term
	{
		O.color = float4(csContact.xxx, 1.0f); O.iblSpec = 0.0f; return O;
	}

	directional *= sunShadow;

	if (gShadowShowCascades > 0.5f)
	{
		static const float3 kCascadeTint[4] =
			{ float3(1.0,0.35,0.35), float3(0.35,1.0,0.35), float3(0.35,0.55,1.0), float3(1.0,1.0,0.35) };
		float3 tint = kCascadeTint[PickCascade(viewPos.z)];
		O.color = float4(tint * (0.15f + 0.85f * sunShadow), 1.0f); O.iblSpec = 0.0f; return O;
	}


	float3 pointLights = 0;
	if (gClusterLightCount > 0)
	{
		// Clustered: only this froxel's lights.
		uint cluster = ClusterIndexForPixel(input.position.xy, viewPos.z);
		uint n0 = min(ClusterLightCounts[cluster], gClusterMaxPerCluster);
		uint base = cluster * gClusterMaxPerCluster;
		for (uint ci = 0; ci < n0; ci++)
		{
			GpuLight L = DeferredLights[ClusterLightIndices[base + ci]];
			pointLights += ShadeDeferredLight(L, diffuseColor, specularColor, n, roughness, toEye, worldPos);
		}
	}
	else
	{
		for (uint p = 0; p < gDeferredLightCount; p++)
		{
			GpuLight L = DeferredLights[p];
			pointLights += ShadeDeferredLight(L, diffuseColor, specularColor, n, roughness, toEye, worldPos);
		}
	}

	float3 gi = diffuseColor * EvaluateGI(worldPos, n) * ao;

	// Procedural interior seal: fade the sun + sky-IBL where the GI probes report
	// low sky visibility (surface is enclosed). Strength 0 = feature off.
	float sealStrength = asfloat(gGiCounts.w);
	if (sealStrength > 0.001f)
	{
		float skyVis = lerp(1.0f, GiSkyVisibility(worldPos), saturate(sealStrength));
		directional   *= skyVis;
		ambiance      *= skyVis;
		O.iblSpec.rgb *= skyVis;
	}

	O.color = float4(directional + ambiance + gi + pointLights + emissive, 1.0f);
	return O;
}
