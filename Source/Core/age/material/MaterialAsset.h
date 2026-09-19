#pragma once
#include <age/math/Photometry.h>
#include <age/material/MaterialParams.h>

#include <array>
#include <string>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace Ag
{
	// A .tgmat material: PBR parameters plus optional texture maps, packed the
	// Unreal way (see MaterialParams.hlsli). Read by the game and edited by the
	// GameEditor's Material Editor.
	//
	// Without maps the material is constants only (baseColor, roughness, ...).
	// With maps, the constants become multipliers and are only written when
	// the material asks for them (baseColorTint, roughnessScale, ...).
	struct MaterialAsset
	{
		enum Map { BaseColor = 0, Normal = 1, Orm = 2, Emissive = 3 };

		// A material instance references a stable engine shader family instead of
		// duplicating a shader per texture set.
		std::string masterMaterial = "PBR";
		std::string surfaceType = "Opaque"; // Opaque, Masked, Transparent (glass)
		float alphaCutoff      = 0.33f;
		float opacity          = 1.0f;
		// Written by the TextureCooker: false when the base colour map has no
		// alpha, which lets ray queries skip the cutout test.
		bool  baseColorHasAlpha = true;

		// Constants-only values
		float baseColor[3]     = { 0.8f, 0.8f, 0.8f };
		float roughness        = 0.5f;
		float metalness        = 0.0f;
		float ao               = 1.0f;

		// Multipliers applied on top of the maps
		float baseColorTint[3] = { 1.0f, 1.0f, 1.0f };
		float roughnessScale   = 1.0f;
		float metalnessScale   = 1.0f;
		float aoStrength       = 1.0f;
		float normalStrength   = 1.0f;

		// Emissive. emissiveStrength is scene units (1 = 100000/pi cd/m²).
		// A legacy _FX map ignores it; an RGB map and the constants use it.
		float emissiveColor[3] = { 1.0f, 1.0f, 1.0f };
		float emissiveStrength = 0.0f;
		std::string emissiveMode = "Auto";       // Auto (by file name), RGB, Legacy
		std::string normalConvention = "DirectX"; // DirectX (Unreal), OpenGL

		// Glass (surfaceType Transparent)
		float ior              = 1.52f;
		float refractionScale  = 1.0f;
		float thickness        = 0.12f;   // metres
		float absorption       = 0.08f;

		std::array<std::string, 4> maps{ "", "", "", "" }; // base colour, normal, ORM, emissive
		std::string previewMesh = "Sphere";

		bool AnyMaps() const
		{
			for (const std::string& m : maps) if (!m.empty()) return true;
			return false;
		}

		bool IsTransparent() const { return surfaceType == "Transparent"; }
		bool IsMasked() const { return surfaceType == "Masked"; }

		bool EmissiveIsRgb() const
		{
			if (emissiveMode == "RGB") return true;
			if (emissiveMode == "Legacy") return false;
			std::string name = maps[Emissive];
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return name.find("_fx.") == std::string::npos;
		}

		// Colour data (base colour, RGB emissive) is sRGB; everything else linear.
		bool MapIsSrgb(int aSlot) const
		{
			return aSlot == BaseColor || (aSlot == Emissive && EmissiveIsRgb());
		}

		MaterialParams ToParams() const
		{
			const bool textured = AnyMaps();
			MaterialParams p = MakeMaterialParams(textured);
			if (textured)
			{
				p.baseColorFactor = { baseColorTint[0], baseColorTint[1], baseColorTint[2] };
				p.roughnessFactor = roughnessScale;
				p.metalnessFactor = metalnessScale;
				p.aoStrength = aoStrength;
				if (!maps[Emissive].empty())
				{
					p.flags |= MaterialFlags::HasEmissive;
					if (EmissiveIsRgb())
					{
						p.flags |= MaterialFlags::EmissiveRgb;
						// Maps are authored as colour; a zero strength would hide them.
						p.emissiveIntensity = emissiveStrength > 0.f ? emissiveStrength : 1.f;
					}
				}
			}
			else
			{
				p.baseColorFactor = { baseColor[0], baseColor[1], baseColor[2] };
				p.roughnessFactor = roughness;
				p.metalnessFactor = metalness;
				p.aoStrength = ao;
				p.emissiveIntensity = emissiveStrength;
			}
			if (!textured || (p.flags & MaterialFlags::EmissiveRgb))
				p.emissiveFactor = { emissiveColor[0], emissiveColor[1], emissiveColor[2] };
			if (normalConvention == "OpenGL") p.flags |= MaterialFlags::NormalOpenGl;
			p.opacity = opacity;
			p.alphaCutoff = alphaCutoff;
			p.normalStrength = normalStrength;
			p.shadingModel = (uint32_t)(IsTransparent() ? ShadingModel::Glass : ShadingModel::DefaultLit);
			p.ior = ior;
			p.refractionScale = refractionScale;
			p.thickness = thickness;
			p.absorption = absorption;
			return p;
		}

		static MaterialAsset Default() { return {}; }

		bool Load(const std::string& path)
		{
			std::ifstream in(path);
			if (!in.is_open()) return false;
			nlohmann::json j;
			try { in >> j; } catch (const std::exception&) { return false; }
			return FromJson(j);
		}

		bool FromJson(const nlohmann::json& j)
		{
			auto arr3 = [&](const char* key, float* v)
			{
				if (!j.contains(key)) return;
				const nlohmann::json& a = j[key];
				if (a.is_array() && a.size() >= 3)
					for (int i = 0; i < 3; ++i) v[i] = a[i].get<float>();
			};
			arr3("baseColor", baseColor);
			arr3("emissiveColor", emissiveColor);
			arr3("baseColorTint", baseColorTint);
			masterMaterial   = j.value("masterMaterial", masterMaterial);
			surfaceType      = j.value("surfaceType", surfaceType);
			alphaCutoff      = j.value("alphaCutoff", alphaCutoff);
			opacity          = j.value("opacity", opacity);
			baseColorHasAlpha = j.value("baseColorHasAlpha", baseColorHasAlpha);
			roughness        = j.value("roughness", roughness);
			metalness        = j.value("metalness", metalness);
			ao               = j.value("ao", ao);
			roughnessScale   = j.value("roughnessScale", roughnessScale);
			metalnessScale   = j.value("metalnessScale", metalnessScale);
			aoStrength       = j.value("aoStrength", aoStrength);
			normalStrength   = j.value("normalStrength", normalStrength);
			emissiveStrength = j.value("emissiveStrength", emissiveStrength);
			// Physical alternative: surface luminance in cd/m² (a lit phone screen
			// is ~500, a frosted bulb ~100 000).
			if (j.contains("emissiveLuminance"))
				emissiveStrength = Ag::Photometry::NitsToUnits(j["emissiveLuminance"].get<float>());
			emissiveMode     = j.value("emissiveMode", emissiveMode);
			normalConvention = j.value("normalConvention", normalConvention);
			ior              = j.value("ior", ior);
			refractionScale  = j.value("refractionScale", refractionScale);
			// Older materials stored centimetres under "thicknessCm".
			thickness        = j.contains("thicknessCm") ? j.value("thicknessCm", 12.0f) * 0.01f
			                                             : j.value("thickness", thickness);
			absorption       = j.value("absorption", absorption);
			previewMesh      = j.value("previewMesh", previewMesh);
			if (j.contains("maps") && j["maps"].is_object())
			{
				const nlohmann::json& m = j["maps"];
				// "fx" is the pre-Unreal name of the emissive slot.
				const char* keys[4] = { "albedo", "normal", "orm", "emissive" };
				for (int i = 0; i < 4; ++i)
				{
					std::string s = m.value(keys[i], std::string());
					if (i == Emissive && s.empty()) s = m.value("fx", std::string());
					std::replace(s.begin(), s.end(), '\\', '/');
					maps[i] = s;
				}
			}
			return true;
		}

		bool Save(const std::string& path) const
		{
			nlohmann::json j;
			j["masterMaterial"]   = masterMaterial;
			j["surfaceType"]      = surfaceType;
			j["alphaCutoff"]      = alphaCutoff;
			j["opacity"]          = opacity;
			j["baseColorHasAlpha"] = baseColorHasAlpha;
			j["baseColor"]        = { baseColor[0], baseColor[1], baseColor[2] };
			j["roughness"]        = roughness;
			j["metalness"]        = metalness;
			j["ao"]               = ao;
			j["baseColorTint"]    = { baseColorTint[0], baseColorTint[1], baseColorTint[2] };
			j["roughnessScale"]   = roughnessScale;
			j["metalnessScale"]   = metalnessScale;
			j["aoStrength"]       = aoStrength;
			j["emissiveColor"]    = { emissiveColor[0], emissiveColor[1], emissiveColor[2] };
			j["emissiveStrength"] = emissiveStrength;
			j["emissiveLuminance"] = emissiveStrength * Ag::Photometry::kNitsPerUnit;   // cd/m², preferred on load
			j["emissiveMode"]     = emissiveMode;
			j["normalConvention"] = normalConvention;
			j["normalStrength"]   = normalStrength;
			j["ior"]              = ior;
			j["refractionScale"]  = refractionScale;
			j["thickness"]        = thickness;
			j["absorption"]       = absorption;
			j["previewMesh"]      = previewMesh;
			j["maps"] = {
				{ "albedo", maps[0] }, { "normal", maps[1] }, { "orm", maps[2] }, { "emissive", maps[3] }
			};
			std::ofstream out(path);
			if (!out.is_open()) return false;
			out << j.dump(2) << "\n";
			return true;
		}
	};
}
