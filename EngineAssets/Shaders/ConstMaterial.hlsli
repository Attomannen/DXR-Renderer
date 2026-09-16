#ifndef CONST_MATERIAL_HLSLI
#define CONST_MATERIAL_HLSLI

// Fixed-parameter PBR material, shared by:
//   - GBufferDebugMatPS.hlsl        (deferred, debug sphere)
//   - PbrConstModelShaderPS.hlsl    (forward, editor material preview)
// Layout matches DeferredRenderer::DebugMaterial (12 floats) and the 48-byte
// cbuffer the GameEditor's Material Editor maps.
cbuffer ConstMaterial : register(b11)
{
	float4 gCMBaseColor;      // rgb linear base colour  (a unused)
	float4 gCMParams;         // x roughness  y metalness  z ao  w emissive strength
	float4 gCMEmissiveColor;  // rgb emissive tint       (a unused)
};

#endif
