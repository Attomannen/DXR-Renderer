#ifndef MATERIAL_PARAMS_HLSLI
#define MATERIAL_PARAMS_HLSLI

// Per-material parameters, shared by C++ (Ag::MaterialParams) and every
// material shader: the deferred G-buffer pass, forward glass, the editor
// preview and the DXR hit decode (inside RayMaterialRecord).
//
// Texture packing follows Unreal Engine:
//   BaseColor  _BC / _D / _BaseColor / _C   sRGB rgb, alpha = opacity mask
//   Normal     _N / _Normal                 DirectX (green down), only xy read
//   ORM        _ORM / _M                    r AO, g roughness, b metalness
//   Emissive   _E / _Emissive               sRGB rgb emissive colour
// Legacy _FX emissive (r mask, g strength / MAX) is still decoded when
// MATERIAL_FLAG_EMISSIVE_RGB is clear.
//
// Every row is 16 bytes so the struct is identical in a cbuffer, a
// structured buffer and C++.

#define MATERIAL_FLAG_USE_TEXTURES   1u   // 0: constants only (material preview, procedural)
#define MATERIAL_FLAG_EMISSIVE_RGB   2u   // emissive map is an Unreal-style RGB colour
#define MATERIAL_FLAG_NORMAL_GL      4u   // normal map is OpenGL style (green up): flip y
#define MATERIAL_FLAG_HAS_EMISSIVE   8u   // an emissive map is bound

#define SHADING_MODEL_DEFAULT_LIT    0u
#define SHADING_MODEL_GLASS          1u   // thin transmissive surface, forward pass

#ifdef __cplusplus
#	include <cstdint>
namespace Ag::MaterialShared
{
	using uint = uint32_t;
	struct float3 { float x, y, z; };
#	define MATERIAL_STRUCT_BEGIN(name) struct name {
#	define MATERIAL_STRUCT_END };
#else
#	define MATERIAL_STRUCT_BEGIN(name) struct name {
#	define MATERIAL_STRUCT_END };
#endif

MATERIAL_STRUCT_BEGIN(MaterialParams)
	float3 baseColorFactor;     // multiplies the base colour map (or is the base colour)
	float  opacity;             // multiplies base colour alpha
	float  roughnessFactor;
	float  metalnessFactor;
	float  aoStrength;          // 0: ignore the AO channel, 1: full
	float  emissiveIntensity;   // scene units (1 = 100000/pi nits)
	float3 emissiveFactor;      // tint for the emissive map (or the colour)
	uint   flags;               // MATERIAL_FLAG_*
	float  ior;                 // glass
	float  refractionScale;     // glass: screen-space offset strength
	float  thicknessCm;         // glass: path length for absorption
	float  absorption;          // glass: per metre, tinted by 1 - base colour
	uint   shadingModel;        // SHADING_MODEL_*
	float  normalStrength;
	float  alphaCutoff;         // masked materials
	float  _pad0;
MATERIAL_STRUCT_END

#ifdef __cplusplus
}   // namespace Ag::MaterialShared
#else

// Bound per draw by ModelShader::RenderMesh.
cbuffer MaterialBuffer : register(b11)
{
	MaterialParams gMaterial;
};

// Tangent-space normal from a two-channel map, honouring the material's green
// convention and normal strength.
float3 DecodeMaterialNormal(float2 encoded, MaterialParams m)
{
	float2 xy = encoded * 2.0f - 1.0f;
	if ((m.flags & MATERIAL_FLAG_NORMAL_GL) != 0u) xy.y = -xy.y;
	xy *= m.normalStrength;
	return normalize(float3(xy, sqrt(saturate(1.0f - dot(xy, xy)))));
}

// Emissive radiance from the bound map (or constants) and the base colour.
float3 MaterialEmissive(MaterialParams m, float3 baseColor, float4 emissiveTexel)
{
	const float3 tint = m.emissiveFactor * m.emissiveIntensity;
	if ((m.flags & MATERIAL_FLAG_USE_TEXTURES) == 0u)
		return tint;   // constants-only material: emissiveFactor is the colour
	if ((m.flags & MATERIAL_FLAG_HAS_EMISSIVE) == 0u)
		return 0.0f;
	if ((m.flags & MATERIAL_FLAG_EMISSIVE_RGB) != 0u)
		return emissiveTexel.rgb * tint;
	// Legacy _FX: r mask, g strength / MAX_EMISSIVE_STRENGTH (16), coloured by albedo.
	return baseColor * emissiveTexel.r * (emissiveTexel.g * 16.0f) * tint;
}

#endif
#endif
