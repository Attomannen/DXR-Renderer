#pragma once

// Scene and material file loading for the GameWorld harness (.tgs, .tgo,
// .tgmat) plus the small string and environment helpers it shares.

#include <tge/render/DeferredRenderer.h>
#include <tge/material/MaterialAsset.h>
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

	// .tgmat materials; the format lives in the Graphics library.
	using MaterialDef = Tga::MaterialAsset;

	// One renderable from a .tgo / scene object: model + one .tgmat asset per
	// mesh/material + a world transform.
	// Physics authored on the .tgo: the Model's Collision setting, plus optional
	// Collider and Rigidbody components. Strings are the names written to the file.
	struct SceneEntryPhysics
	{
		std::string modelCollision = "None";     // None, Auto, Box, ConvexHull, TriangleMesh

		bool hasCollider = false;
		std::string colliderShape = "Auto";      // Auto, Box, Sphere, Capsule, ConvexHull, TriangleMesh
		Vector3f halfExtents{ 50.f, 50.f, 50.f };
		float radius = 50.f;
		float halfHeight = 50.f;
		Vector3f offset{ 0.f, 0.f, 0.f };
		bool isTrigger = false;

		bool hasBody = false;
		std::string motion = "Dynamic";          // Static, Kinematic, Dynamic
		float mass = 0.f;
		float friction = 0.5f;
		float restitution = 0.f;
		float gravityFactor = 1.f;
		float linearDamping = 0.05f;
		float angularDamping = 0.05f;

		bool Any() const { return hasCollider || modelCollision != "None"; }
	};

	struct SceneEntryCamera
	{
		bool has = false;
		Vector3f offset{ 0.f, 170.f, 0.f };
		float fov = 90.f;
		bool activeOnStart = true;
	};

	struct SceneEntryCharacter
	{
		bool has = false;
		float radius = 35.f, height = 170.f, stepHeight = 40.f, maxSlope = 50.f, mass = 80.f;
	};

	struct SceneEntry
	{
		std::string fbx;
		std::vector<std::string> materials;
		SceneEntryPhysics physics;
		SceneEntryCamera camera;
		SceneEntryCharacter character;
		json instanceProperties;                            // per-instance property values written in the scene file
		std::string tgoPath;                                // "Folder/Name" of the .tgo (no extension); scripts live in <tgoPath>/
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
	bool ParseModelProperty(const json& propsHolder, SceneEntry& out);
	void ParsePhysicsProperties(const json& propsHolder, SceneEntryPhysics& out);
	std::optional<SceneEntry> LoadTgo(const fs::path& tgoPath);
	std::optional<std::string> FindFirstTgsScene();
	std::vector<SceneEntry> LoadTgs(const std::string& name);
}
