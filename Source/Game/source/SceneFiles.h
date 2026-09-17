#pragma once

// Scene and material file loading for the GameWorld harness (.tgs, .tgo,
// .tgmat) plus the small string and environment helpers it shares.

#include <tge/render/DeferredRenderer.h>
#include <tge/math/Matrix4x4.h>
#include <nlohmann/json.hpp>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace GameScene
{
	namespace fs = std::filesystem;
	using json = nlohmann::json;
	using namespace Tga;

	inline int   EnvInt(const char* name, int def)   { const char* v = std::getenv(name); return v ? std::atoi(v) : def; }
	inline float EnvFloat(const char* name, float def) { const char* v = std::getenv(name); return v ? (float)std::atof(v) : def; }
	inline float Deg2Rad(float d) { return d * 0.01745329252f; }
	inline float Rad2Deg(float r) { return r * 57.2957795131f; }

	// A fixed-parameter PBR material, matching DeferredRenderer::DebugMaterial +
	// optional [C,N,M,FX] texture map paths. Serialised as .tgmat JSON by the
	// GameEditor's Material Editor; loaded here for the debug sphere.
	struct MaterialDef
	{
		std::string surfaceType = "Opaque"; // Opaque, Masked, Transparent; authored in .tgmat
		float alphaCutoff      = 0.33f;
		float baseColor[3]     = { 0.8f, 0.8f, 0.8f };
		float roughness        = 0.5f;
		float metalness        = 0.0f;
		float ao               = 1.0f;
		float emissiveColor[3] = { 0.0f, 0.0f, 0.0f };
		float emissiveStrength = 0.0f;
		std::array<std::string, 4> maps{ "", "", "", "" };
	};

	// One renderable from a .tgo / scene object: model + one .tgmat asset per
	// mesh/material + a world transform.
	struct SceneEntry
	{
		std::string fbx;
		std::vector<std::string> materials;
		Matrix4x4f transform;                               // identity by default
	};

	std::string EnvStr(const char* name, const char* def);
	std::string LowerStr(std::string s);
	std::string StripDupSuffix(const std::string& s);
	bool MatchesAny(const std::string& matName, const std::vector<std::string>& keys);
#ifndef _RETAIL
	bool EmissiveLuminanceSlider(const char* aLabel, float& aStrength);
#endif
	bool LoadTgmat(const fs::path& path, MaterialDef& out);
	bool LoadTgmatInto(const char* path, DeferredRenderer::DebugMaterial& dm);
	bool ParseModelProperty(const json& propsHolder, SceneEntry& out);
	std::optional<SceneEntry> LoadTgo(const fs::path& tgoPath);
	std::optional<std::string> FindFirstTgsScene();
	std::vector<SceneEntry> LoadTgs(const std::string& name);
}
