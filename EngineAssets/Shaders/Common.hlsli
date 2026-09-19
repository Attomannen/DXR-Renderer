#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#define NUMBER_OF_LIGHTS_ALLOWED 8
#define MAX_ANIMATION_BONES 128
// _FX texture: r = emissive mask (max of source rgb), g = emissive strength
// normalised by this. Emissive radiance = albedo * fx.r * (fx.g * this).
#define MAX_EMISSIVE_STRENGTH 16.0f
#define USE_LIGHTS
#define USE_NOISE

int GetNumMips(TextureCube cubeTex)
{
	int iWidth = 0;
	int iheight = 0;
	int numMips = 0;
	cubeTex.GetDimensions(0, iWidth, iheight, numMips);
	return numMips;
}

cbuffer FrameBuffer : register(b0)
{
    float2 Resolution;
    float TotalTime;
    float DeltaTime;
}

cbuffer CameraBuffer : register(b1)
{
	float4x4 WorldToCamera;
    float4x4 CameraToWorld;
	float4x4 CameraToProjection;
    float4x4 ProjectionToCamera;
	float NearPlane;
	float FarPlane;
	float Unused0;
	float Unused1;
};

cbuffer LightConstantBufferData : register(b2)
{
	struct PointLightData
	{
		float4 position;
		float4 color;
		float range;
		float radius;
        float2 padding;
    } PointLights[NUMBER_OF_LIGHTS_ALLOWED];

	uint NumberOfLights;
	float DirectionalLightSoftness;
	float Garbage1;
    float Garbage2;

	float4 AmbientLightColor;
	float4 DirectionalLightColor;
    float4x4 DirectionalLightToWorldTransform;
    float4x4 DirectionalWorldToLightTransform;
};

// Light transform forward is the direction sunlight TRAVELS. BRDF and
// shadow rays need the opposite direction, from the surface toward the sun.
float3 GetSunDirectionToLight()
{
	return normalize(-DirectionalLightToWorldTransform._m02_m12_m22);
}

cbuffer ShaderSettingsConstantBuffer : register(b3)
{
	float4 CustomShaderParameters;
	float AlphaTestThreshold;
};

cbuffer ObjectBuffer : register(b4)
{
	float4x4 ObjectToWorld;
}

cbuffer CustomShapeConstantBufferData : register(b4)
{
	float4x4 ModelToWorld;
};

cbuffer BoneBuffer : register(b5)
{
	float4x4 Bones[MAX_ANIMATION_BONES];
};

// Box-projected reflection probe for the IBL specular. Unbound => zeros =>
// gProbeBoxCenter.w == 0 => no correction (behaves as an infinite cubemap).
cbuffer ReflectionProbeBuffer : register(b12)
{
	float4 gProbeBoxCenter;   // xyz world centre, w = enabled flag
	float4 gProbeBoxHalf;     // xyz half-extents of the probe influence box
};

struct ModelVertexInput
{
	float4 position	    :	POSITION;
	float4 vertexColor0	:	COLOR0;
	float4 vertexColor1	:	COLOR1;
	float4 vertexColor2	:	COLOR2;
	float4 vertexColor3	:	COLOR3;
	float2 texCoord0	:	TEXCOORD0;
	float2 texCoord1	:	TEXCOORD1;
	float2 texCoord2	:	TEXCOORD2;
	float2 texCoord3	:	TEXCOORD3;
	float3 normal		:	NORMAL;
	float3 tangent		:	TANGENT;
	float3 binormal	    :	BINORMAL;
	float4 boneIndices  :   BONES;
	float4 weights      :   WEIGHTS;
};

// Compact vertex of static meshes (Ag::MeshVertex, 40 bytes).
struct MeshVertexInput
{
	float4 position      : POSITION;   // xyz, w = bitangent sign
	float4 normalTangent : NORMAL;     // snorm: octahedral normal (xy), tangent (zw)
	float2 texCoord0     : TEXCOORD0;
	float2 texCoord1     : TEXCOORD1;
	float4 vertexColor0  : COLOR0;
};

float3 OctDecode(float2 e)
{
	float3 n = float3(e.x, e.y, 1.0f - abs(e.x) - abs(e.y));
	const float t = saturate(-n.z);
	n.xy -= (step(0.0f, n.xy) * 2.0f - 1.0f) * t;   // no vector ternary: DXC HLSL 2021 rejects it
	return normalize(n);
}

// Rebuilds the full model vertex so the static shaders keep one code path.
ModelVertexInput ExpandMeshVertex(MeshVertexInput packed)
{
	ModelVertexInput v = (ModelVertexInput)0;
	v.position = float4(packed.position.xyz, 1.0f);
	v.normal = OctDecode(packed.normalTangent.xy);
	v.tangent = OctDecode(packed.normalTangent.zw);
	v.binormal = cross(v.normal, v.tangent) * (packed.position.w < 0.0f ? -1.0f : 1.0f);
	v.texCoord0 = packed.texCoord0;
	v.texCoord1 = packed.texCoord1;
	v.vertexColor0 = packed.vertexColor0;
	return v;
}

struct ModelVertexToPixel
{
	float4 position			:	SV_POSITION;
	float4 worldPosition	:	POSITION;
	float4 vertexColor0		:	COLOR0;
	float4 vertexColor1		:	COLOR1;
	float4 vertexColor2		:	COLOR2;
	float4 vertexColor3		:	COLOR3;
	float2 texCoord0		:	TEXCOORD0;
	float2 texCoord1		:	TEXCOORD1;
	float2 texCoord2		:	TEXCOORD2;
	float2 texCoord3		:	TEXCOORD3;
	float3 normal			:	NORMAL;
	float3 tangent			:	TANGENT;
	float3 binormal			:	BINORMAL;
};

struct InstancedPixelInputType
{
	float4 position			:	SV_POSITION;
	float4 worldPosition	:	POSITION;
	float2 tex				:	TEXCOORD0;
	float4 color			:	TEXCOORD2;
	float3 normal			:	NORMAL;
	uint instanceId			:	SV_InstanceID;
};

struct PixelOutput
{
	float4 color		:	SV_TARGET;
};

TextureCube environmentTexture : register(t0);

Texture2D albedoTexture		: register(t1);
Texture2D normalTexture		: register(t2);
Texture2D materialTexture	: register(t3);
Texture2D fxTexture			: register(t4);
Texture2D gBrdfLutTexture   : register(t5);

Texture2D directionalLightShadowMap : register(t8);

SamplerState defaultSampler : register(s0);

float2x2 ComputeRotation(float aRotation) 
{
	float c = cos(aRotation); 
	float s = sin(aRotation);
	return float2x2(c, -s, s, c);
}


// This gets Log Depth from worldPosition
float GetLogDepth(float4 worldPosition)
{
	float4 cameraPos = mul(WorldToCamera, worldPosition);
	float4 projectionPos = mul(CameraToProjection, cameraPos);
	return projectionPos.z / projectionPos.w;
}

float GetLinDepth(float4 worldPosition)
{
	float logDepth = GetLogDepth(worldPosition);
	return NearPlane / (FarPlane - logDepth * (FarPlane - NearPlane));
}

// Converts Log Depth to Lin Depth
float LogDepthToLinDepth(float depth)
{
	return NearPlane / (FarPlane - depth * (FarPlane - NearPlane));
}

// Get screen texture coordinates from world position?
float2 GetScreenCoords(float4 worldPosition)
{
	float4 worldToView = mul(WorldToCamera, worldPosition);
	float4 viewToProj = mul(CameraToProjection, worldToView);

	float2 projectedTextureCoords;
	projectedTextureCoords.x = viewToProj.x / viewToProj.w / 2.0f + 0.5f;
	projectedTextureCoords.y = viewToProj.y / viewToProj.w / 2.0f + 0.5f;

	return projectedTextureCoords;
}

#endif
