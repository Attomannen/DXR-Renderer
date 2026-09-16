#include "common.hlsli"

ModelVertexToPixel main(ModelVertexInput input)
{
	ModelVertexToPixel result;

	float4 vertexObjectPos = input.position;
	float4 vertexWorldPos = mul(ObjectToWorld, vertexObjectPos);
	float4 vertexViewPos = mul(WorldToCamera, vertexWorldPos);
	float4 vertexProjectionPos = mul(CameraToProjection, vertexViewPos);

	float3x3 toWorldRotation = (float3x3)ObjectToWorld;
	float3 vertexWorldNormal = mul(toWorldRotation, input.normal);
	float3 vertexWorldBinormal = mul(toWorldRotation, input.binormal);
	float3 vertexWorldTangent = mul(toWorldRotation, input.tangent);

	result.position = vertexProjectionPos;
	result.worldPosition = vertexWorldPos;
	result.vertexColor0 = input.vertexColor0;
	result.vertexColor1 = input.vertexColor1;
	result.vertexColor2 = input.vertexColor2;
	result.vertexColor3 = input.vertexColor3;

	result.texCoord0 = input.texCoord0;
	result.texCoord1 = input.texCoord1;
	result.texCoord2 = input.texCoord2;
	result.texCoord3 = input.texCoord3;

	result.normal = vertexWorldNormal;
	result.binormal = vertexWorldBinormal;
	result.tangent = vertexWorldTangent;

	return result;
}