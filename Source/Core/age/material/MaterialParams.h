#pragma once

// C++ view of EngineAssets/Shaders/MaterialParams.hlsli. See that file for the
// texture packing and the meaning of each field.

#include "../../../../EngineAssets/Shaders/MaterialParams.hlsli"

namespace Ag
{
	using MaterialParams = MaterialShared::MaterialParams;
	static_assert(sizeof(MaterialParams) == 80);

	namespace MaterialFlags
	{
		constexpr uint32_t UseTextures = MATERIAL_FLAG_USE_TEXTURES;
		constexpr uint32_t EmissiveRgb = MATERIAL_FLAG_EMISSIVE_RGB;
		constexpr uint32_t NormalOpenGl = MATERIAL_FLAG_NORMAL_GL;
		constexpr uint32_t HasEmissive = MATERIAL_FLAG_HAS_EMISSIVE;
	}

	enum class ShadingModel : uint32_t
	{
		DefaultLit = SHADING_MODEL_DEFAULT_LIT,
		Glass = SHADING_MODEL_GLASS,
	};

	// Neutral parameters: a textured material renders exactly as its maps say,
	// a constants-only one is mid-grey dielectric.
	inline MaterialParams MakeMaterialParams(bool aTextured)
	{
		MaterialParams m{};
		m.baseColorFactor = aTextured ? MaterialShared::float3{ 1.f, 1.f, 1.f } : MaterialShared::float3{ 0.8f, 0.8f, 0.8f };
		m.opacity = 1.f;
		m.roughnessFactor = aTextured ? 1.f : 0.5f;
		m.metalnessFactor = aTextured ? 1.f : 0.f;
		m.aoStrength = 1.f;
		m.emissiveIntensity = aTextured ? 1.f : 0.f;
		m.emissiveFactor = { 1.f, 1.f, 1.f };
		m.flags = aTextured ? MaterialFlags::UseTextures : 0u;
		m.ior = 1.52f;
		m.refractionScale = 1.f;
		m.thickness = 0.12f;
		m.absorption = 0.08f;
		m.shadingModel = (uint32_t)ShadingModel::DefaultLit;
		m.normalStrength = 1.f;
		m.alphaCutoff = 0.33f;
		return m;
	}
}
