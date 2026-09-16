// Clustered light culling. One thread per froxel cluster: build the cluster's
// view-space AABB, test every light sphere against it, write a compact per-cluster
// light-index list. Consumed by DeferredLightingPS.
//
// Cluster grid: (gTileCount.x * gTileCount.y) screen tiles of gTilePx pixels,
// gZSlices exponential depth slices between gNear and gFar.

struct GpuLight
{
	float3 position;   // world space
	float  range;
	float3 color;
	float  radius;
	float3 spotDir;        // cone axis (light -> scene); unused unless spotCosOuter > 0
	float  spotCosOuter;   // > 0 => spot light
	float  spotCosInner;
	float  shadowSlot;     // atlas tile base, -1 = no shadow
	float2 _lpad;
};

StructuredBuffer<GpuLight> Lights : register(t0);

RWStructuredBuffer<uint> ClusterLightIndices : register(u0);   // gMaxPerCluster per cluster
RWStructuredBuffer<uint> ClusterLightCounts  : register(u1);   // one per cluster

cbuffer ClusterParams : register(b0)
{
	float4x4 ProjectionToView;   // inverse projection (clip -> view)
	float4x4 WorldToView;        // camera fast-inverse
	uint2 gTileCount;
	uint  gTilePx;
	uint  gZSlices;
	float gNear;
	float gFar;
	uint  gLightCount;
	uint  gMaxPerCluster;
	float2 gScreen;              // render target size in pixels
	float2 _cpad;
};

// NDC (x,y in [-1,1]) at clip z -> view-space position (before perspective divide handled).
float3 NdcToView(float2 ndc, float ndcZ)
{
	float4 v = mul(ProjectionToView, float4(ndc, ndcZ, 1.0f));
	return v.xyz / v.w;
}

// A view-space point on the ray through screen NDC (x,y) at a given view Z.
// Engine is left-handed: +Z is forward, visible geometry has viewZ in [near, far].
float3 ViewAtDepth(float2 ndc, float viewZ)
{
	float3 dir = NdcToView(ndc, 1.0f);      // far-plane point; ray from origin
	return dir * (viewZ / dir.z);
}

float SphereAabbDistSq(float3 c, float3 aMin, float3 aMax)
{
	float3 e = max(aMin - c, 0.0f) + max(c - aMax, 0.0f);
	return dot(e, e);
}

[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	const uint tilesXY = gTileCount.x * gTileCount.y;
	const uint numClusters = tilesXY * gZSlices;
	const uint cluster = dtid.x;
	if (cluster >= numClusters) return;

	const uint slice = cluster / tilesXY;
	const uint tileI = cluster - slice * tilesXY;
	const uint tx = tileI % gTileCount.x;
	const uint ty = tileI / gTileCount.x;

	// Tile screen rect -> NDC. y flips (screen +y down, NDC +y up).
	float2 pMin = float2(tx * gTilePx, ty * gTilePx);
	float2 pMax = float2(min((tx + 1) * gTilePx, (uint)gScreen.x),
	                     min((ty + 1) * gTilePx, (uint)gScreen.y));
	float2 ndcMin = float2(pMin.x / gScreen.x * 2.0f - 1.0f, 1.0f - pMax.y / gScreen.y * 2.0f);
	float2 ndcMax = float2(pMax.x / gScreen.x * 2.0f - 1.0f, 1.0f - pMin.y / gScreen.y * 2.0f);

	// Exponential depth slice bounds, view space (+Z forward, left-handed).
	float ratio = gFar / gNear;
	float zNear = gNear * pow(ratio, (float)slice / (float)gZSlices);
	float zFar  = gNear * pow(ratio, (float)(slice + 1) / (float)gZSlices);

	float3 aMin = 1e30f, aMax = -1e30f;
	[unroll]
	for (uint i = 0; i < 4; ++i)
	{
		float2 ndc = float2((i & 1) ? ndcMax.x : ndcMin.x, (i & 2) ? ndcMax.y : ndcMin.y);
		float3 pn = ViewAtDepth(ndc, zNear);
		float3 pf = ViewAtDepth(ndc, zFar);
		aMin = min(aMin, min(pn, pf));
		aMax = max(aMax, max(pn, pf));
	}

	uint count = 0;
	const uint base = cluster * gMaxPerCluster;
	for (uint li = 0; li < gLightCount && count < gMaxPerCluster; ++li)
	{
		GpuLight L = Lights[li];
		float3 pv = mul(WorldToView, float4(L.position, 1.0f)).xyz;
		if (SphereAabbDistSq(pv, aMin, aMax) <= L.range * L.range)
		{
			ClusterLightIndices[base + count] = li;
			++count;
		}
	}
	ClusterLightCounts[cluster] = count;
}
