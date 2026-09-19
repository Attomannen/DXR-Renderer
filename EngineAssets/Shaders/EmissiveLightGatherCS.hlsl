// Emissive triangle gather: turns the scene's emissive geometry into a list of
// area lights that direct lighting can sample.
//
// Bistro (and most art like it) has no punctual lights at all -- every lamp,
// sign and lantern is a mesh with an emissive material. Those meshes are
// visible, and they feed the irradiance probes, but nothing samples them as
// LIGHTS, so a street lamp glows without lighting the pavement under it. This
// pass builds the list that fixes that.
//
// One thread group per instance, threads striding that instance's triangles,
// so no prefix sum over the scene is needed: the geometry lookup already knows
// each instance's index count.
#include "MaterialParams.hlsli"

struct RayGeometryLookup
{
	uint vertexSrv;
	uint indexSrv;
	uint materialIndex;
	uint vertexStride;
	uint positionOffset;
	uint normalOffset;
	uint uv0Offset;
	uint tangentOffset;
	uint binormalOffset;
	uint vertexFormat;
	uint indexCount;
	uint _pad2;
	float4 previousTransform0, previousTransform1, previousTransform2;
	uint motionHistoryValid;
	uint3 _motionPad;
	float4 transform0, transform1, transform2;
};

struct RayMaterialRecord
{
	uint albedoSrv;
	uint normalSrv;
	uint ormSrv;
	uint emissiveSrv;
	MaterialParams params;
	uint rayVisibility;
	uint3 _recordPad;
};

// Must match EmissiveLight in DxrCommon.hlsli.
struct EmissiveLight
{
	float3 p0;   float area;
	float3 e0;   uint  instanceId;      // edge vectors, not absolute corners:
	float3 e1;   uint  primitiveIndex;  // sampling a point is p0 + e0*u + e1*v
	float3 radiance;
	float  power;                       // luminance * area, for importance
};

StructuredBuffer<RayMaterialRecord> gMaterials : register(t0);
ByteAddressBuffer gRawGeometry[32768] : register(t0, space1);
StructuredBuffer<RayGeometryLookup> gRayGeometry : register(t1, space2);
Texture2D gRaySceneTex[32768] : register(t0, space4);
SamplerState gMaterialSampler : register(s0);

RWStructuredBuffer<EmissiveLight> gEmissiveLights : register(u0);
RWStructuredBuffer<uint> gEmissiveCount : register(u1);

cbuffer EmissiveGatherCb : register(b0)
{
	uint gMaxLights;
	float gMinPower;       // reject lights too dim to matter
	uint2 _gatherPad;
};

float3 TransformPoint(RayGeometryLookup g, float3 p)
{
	// Row-major 3x4, the same layout the TLAS instance uses.
	return float3(dot(g.transform0.xyz, p) + g.transform0.w,
	              dot(g.transform1.xyz, p) + g.transform1.w,
	              dot(g.transform2.xyz, p) + g.transform2.w);
}

[numthreads(64, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID)
{
	const uint instanceId = groupId.x;
	const RayGeometryLookup g = gRayGeometry[instanceId];
	const RayMaterialRecord mat = gMaterials[g.materialIndex];

	// Constants-only materials carry their emission in emissiveFactor alone;
	// textured ones gate it behind the map, exactly as MaterialEmissive does.
	const bool textured = (mat.params.flags & MATERIAL_FLAG_USE_TEXTURES) != 0u;
	const bool hasMap = (mat.params.flags & MATERIAL_FLAG_HAS_EMISSIVE) != 0u;
	if (textured && !hasMap) return;
	const float3 tint = mat.params.emissiveFactor * mat.params.emissiveIntensity;
	if (dot(tint, float3(0.2126f, 0.7152f, 0.0722f)) <= 0.0f) return;

	const uint triangleCount = g.indexCount / 3u;
	for (uint t = threadId.x; t < triangleCount; t += 64u)
	{
		const uint3 idx = gRawGeometry[NonUniformResourceIndex(g.indexSrv)].Load3(t * 12u);
		float3 p[3];
		float2 uv[3];
		[unroll] for (uint c = 0; c < 3; ++c)
		{
			const uint base = idx[c] * g.vertexStride;
			p[c] = TransformPoint(g, asfloat(gRawGeometry[NonUniformResourceIndex(g.vertexSrv)].Load3(base + g.positionOffset)));
			uv[c] = asfloat(gRawGeometry[NonUniformResourceIndex(g.vertexSrv)].Load2(base + g.uv0Offset));
		}

		const float3 e0 = p[1] - p[0];
		const float3 e1 = p[2] - p[0];
		const float3 cross01 = cross(e0, e1);
		const float area = 0.5f * length(cross01);
		if (area <= 1e-10f) continue;   // same physical threshold, in m^2

		// Radiance at the triangle's UV centroid. One sample per triangle is
		// enough for a light list: it decides importance and the emitted colour,
		// while the shading itself still samples the map at the hit point.
		//
		// Decoded through MaterialEmissive, the same function the shading hit
		// uses, NOT by reading the map's .rgb. That shortcut is only correct
		// for the Unreal-style RGB convention; under the legacy _FX packing
		// (r = mask, g = strength) it reads the strength channel as "green"
		// AND ignores the mask, so every triangle of the material becomes a
		// green emitter. Sponza's stone shares one masked emissive map, which
		// is exactly how the whole courtyard turned green.
		float4 emissiveTexel = 0.0f;
		float3 baseColor = mat.params.baseColorFactor;
		if (textured)
		{
			const float2 uvCentre = (uv[0] + uv[1] + uv[2]) / 3.0f;
			// A high mip is the cheap stand-in for "average over the triangle".
			if (mat.emissiveSrv != 0u)
				emissiveTexel = gRaySceneTex[NonUniformResourceIndex(mat.emissiveSrv)].SampleLevel(gMaterialSampler, uvCentre, 4.0f);
			// The legacy path tints by the surface's own base colour.
			if ((mat.params.flags & MATERIAL_FLAG_EMISSIVE_RGB) == 0u && mat.albedoSrv != 0u)
				baseColor *= gRaySceneTex[NonUniformResourceIndex(mat.albedoSrv)].SampleLevel(gMaterialSampler, uvCentre, 4.0f).rgb;
		}
		const float3 radiance = MaterialEmissive(mat.params, baseColor, emissiveTexel);

		// Scene units are centimetres; power in metre units keeps the numbers in
		// a range where a float32 threshold is meaningful.
		// World units are metres, so the triangle area is already m^2. This used
		// to be area_cm2 * 1e-4; leaving that in cost every emissive surface a
		// factor of 10,000 of its radiated power, which is exactly as dark as it
		// sounds -- the glass still glowed, but it lit nothing around it.
		const float areaM2 = area;
		const float power = dot(radiance, float3(0.2126f, 0.7152f, 0.0722f)) * areaM2;
		if (power <= gMinPower) continue;

		uint slot;
		InterlockedAdd(gEmissiveCount[0], 1u, slot);
		if (slot >= gMaxLights) continue;   // the count still reports the true total

		EmissiveLight light;
		light.p0 = p[0];
		light.area = area;
		light.e0 = e0;
		light.instanceId = instanceId;
		light.e1 = e1;
		light.primitiveIndex = t;
		light.radiance = radiance;
		light.power = power;
		gEmissiveLights[slot] = light;
	}
}
