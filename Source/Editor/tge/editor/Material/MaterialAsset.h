#pragma once

#include <array>
#include <string>
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Tga
{
	// A fixed-parameter PBR material with optional [C,N,M,FX] texture maps.
	// Serialised as .tgmat JSON. Read at runtime by the game (GameWorld::LoadTgmat)
	// and here by the GameEditor's Material Editor.
	struct MaterialAsset
	{
		float baseColor[3]     = { 0.8f, 0.8f, 0.8f };
		float roughness        = 0.5f;
		float metalness        = 0.0f;
		float ao               = 1.0f;
		float emissiveColor[3] = { 1.0f, 1.0f, 1.0f };
		float emissiveStrength = 0.0f;
		float normalStrength   = 1.0f;
		std::array<std::string, 4> maps{ "", "", "", "" }; // albedo, normal, orm, fx
		std::string previewMesh = "Sphere";

		bool AnyMaps() const
		{
			for (const std::string& m : maps) if (!m.empty()) return true;
			return false;
		}

		static MaterialAsset Default() { return {}; }

		bool Load(const std::string& path)
		{
			std::ifstream in(path);
			if (!in.is_open()) return false;
			nlohmann::json j;
			try { in >> j; } catch (const std::exception&) { return false; }

			auto arr3 = [](const nlohmann::json& a, float* v)
			{
				if (a.is_array() && a.size() >= 3)
					for (int i = 0; i < 3; ++i) v[i] = a[i].get<float>();
			};
			if (j.contains("baseColor"))     arr3(j["baseColor"], baseColor);
			if (j.contains("emissiveColor")) arr3(j["emissiveColor"], emissiveColor);
			roughness        = j.value("roughness", roughness);
			metalness        = j.value("metalness", metalness);
			ao               = j.value("ao", ao);
			emissiveStrength = j.value("emissiveStrength", emissiveStrength);
			normalStrength   = j.value("normalStrength", normalStrength);
			previewMesh      = j.value("previewMesh", previewMesh);
			if (j.contains("maps") && j["maps"].is_object())
			{
				const char* keys[4] = { "albedo", "normal", "orm", "fx" };
				for (int i = 0; i < 4; ++i)
				{
					std::string s = j["maps"].value(keys[i], std::string());
					std::replace(s.begin(), s.end(), '\\', '/');
					maps[i] = s;
				}
			}
			return true;
		}

		bool Save(const std::string& path) const
		{
			nlohmann::json j;
			j["baseColor"]        = { baseColor[0], baseColor[1], baseColor[2] };
			j["roughness"]        = roughness;
			j["metalness"]        = metalness;
			j["ao"]               = ao;
			j["emissiveColor"]    = { emissiveColor[0], emissiveColor[1], emissiveColor[2] };
			j["emissiveStrength"] = emissiveStrength;
			j["normalStrength"]   = normalStrength;
			j["previewMesh"]      = previewMesh;
			j["maps"] = {
				{ "albedo", maps[0] }, { "normal", maps[1] }, { "orm", maps[2] }, { "fx", maps[3] }
			};
			std::ofstream out(path);
			if (!out.is_open()) return false;
			out << j.dump(2) << "\n";
			return true;
		}
	};
}
