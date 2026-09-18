#define _CRT_SECURE_NO_WARNINGS
#include "SceneFiles.h"

#include <tge/settings/settings.h>
#include <tge/log/Log.h>
#ifndef _RETAIL
#include <imgui/imgui.h>
#endif
#include <tge/math/Photometry.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace GameScene
{
	std::string EnvStr(const char* name, const char* def)
	{
		const char* v = std::getenv(name);
		return v ? std::string(v) : std::string(def);
	}

	std::string LowerStr(std::string s)
	{
		for (char& c : s) c = (char)std::tolower((unsigned char)c);
		return s;
	}
	// "glass.003" -> "glass" : drop a trailing ".<digits>" (Blender dedup suffix)
	std::string StripDupSuffix(const std::string& s)
	{
		const size_t dot = s.rfind('.');
		if (dot == std::string::npos || dot == 0 || dot + 1 >= s.size()) return s;
		for (size_t i = dot + 1; i < s.size(); ++i)
			if (!std::isdigit((unsigned char)s[i])) return s;
		return s.substr(0, dot);
	}
	bool MatchesAny(const std::string& matName, const std::vector<std::string>& keys)
	{
		const std::string n = StripDupSuffix(LowerStr(matName));
		for (const std::string& k : keys)
			if (!k.empty() && n.find(k) != std::string::npos) return true;
		return false;
	}


	// Sentinel scene name for the procedural "engine room" (no .tgs on disk).


#ifndef _RETAIL
	// Edits an emissive strength (scene units) as surface luminance in cd/m².
	bool EmissiveLuminanceSlider(const char* aLabel, float& aStrength)
	{
		float nits = aStrength * Photometry::kNitsPerUnit;
		if (!ImGui::SliderFloat(aLabel, &nits, 0.f, 1.0e7f, "%.0f cd/m2", ImGuiSliderFlags_Logarithmic)) return false;
		aStrength = Photometry::NitsToUnits(nits);
		return true;
	}
#endif

	bool LoadTgmat(const fs::path& path, MaterialDef& out)
	{
		std::ifstream in(path);
		if (!in) { ERROR_PRINT("tgmat: cannot open %s", path.string().c_str()); return false; }
		json j; try { in >> j; } catch (const std::exception& e) { ERROR_PRINT("tgmat: parse %s: %s", path.string().c_str(), e.what()); return false; }
		return out.FromJson(j);
	}

	// Pull the "Model" property out of a .tgo's "properties" array (or a scene
	// object file that carries the property inline).
	bool ParseModelProperty(const json& propsHolder, SceneEntry& out)
	{
		if (!propsHolder.contains("properties")) return false;
		for (const json& p : propsHolder["properties"])
		{
			if (p.value("type", "") != "Model" && p.value("name", "") != "Model") continue;
			const json& v = p["value"];
			out.fbx = v.value("path", "");
			// tgo paths use backslashes; normalise for ResolveAssetPath
			std::replace(out.fbx.begin(), out.fbx.end(), '\\', '/');
			out.physics.modelCollision = v.value("collision", "None");
			out.materials.clear();
			if (v.contains("materials"))
			{
				for (const json& material : v["materials"])
				{
					std::string path = material.get<std::string>();
					std::replace(path.begin(), path.end(), '\\', '/');
					out.materials.push_back(path);
				}
			}
			return !out.fbx.empty();
		}
		return false;
	}

	// Collider and Rigidbody components from a .tgo's "properties" array.
	void ParsePhysicsProperties(const json& propsHolder, SceneEntryPhysics& out)
	{
		if (!propsHolder.contains("properties")) return;
		auto vec3 = [](const json& v, const char* key, Vector3f& dst)
		{
			if (v.contains(key) && v[key].is_array() && v[key].size() >= 3)
				dst = { v[key][0].get<float>(), v[key][1].get<float>(), v[key][2].get<float>() };
		};
		for (const json& p : propsHolder["properties"])
		{
			const std::string type = p.value("type", "");
			if (!p.contains("value") || !p["value"].is_object()) continue;
			const json& v = p["value"];
			if (type == "Collider")
			{
				out.hasCollider = true;
				out.colliderShape = v.value("shape", "Auto");
				vec3(v, "halfExtents", out.halfExtents);
				out.radius = v.value("radius", out.radius);
				out.halfHeight = v.value("halfHeight", out.halfHeight);
				vec3(v, "offset", out.offset);
			}
			else if (type == "Rigidbody")
			{
				out.hasBody = true;
				out.motion = v.value("motion", "Dynamic");
				out.mass = v.value("mass", out.mass);
				out.friction = v.value("friction", out.friction);
				out.restitution = v.value("restitution", out.restitution);
				out.gravityFactor = v.value("gravityFactor", out.gravityFactor);
				out.linearDamping = v.value("linearDamping", out.linearDamping);
				out.angularDamping = v.value("angularDamping", out.angularDamping);
			}
		}
	}

	std::optional<SceneEntry> LoadTgo(const fs::path& tgoPath)
	{
		std::ifstream in(tgoPath);
		if (!in) { ERROR_PRINT("bench: cannot open tgo %s", tgoPath.string().c_str()); return std::nullopt; }
		json j; try { in >> j; } catch (const std::exception& e) { ERROR_PRINT("bench: tgo parse: %s", e.what()); return std::nullopt; }
		SceneEntry e;
		if (!ParseModelProperty(j, e)) { ERROR_PRINT("bench: no Model property in %s", tgoPath.string().c_str()); return std::nullopt; }
		ParsePhysicsProperties(j, e.physics);
		{
			std::error_code pathEc;
			fs::path relative = fs::relative(tgoPath, Tga::Settings::GameAssetRoot(), pathEc);
			if (!pathEc) e.tgoPath = relative.replace_extension("").generic_string();
		}
		return e;
	}

	// Scenes are the runtime entry point. When a caller has not selected one,
	// pick the first one in a stable order instead of assuming a sample model is
	// present in the asset tree.
	std::optional<std::string> FindFirstTgsScene()
	{
		const fs::path root = Tga::Settings::GameAssetRoot();
		std::error_code ec;
		std::vector<fs::path> scenes;
		for (const fs::directory_entry& item : fs::recursive_directory_iterator(root, ec))
		{
			if (item.is_regular_file() && LowerStr(item.path().extension().string()) == ".tgs")
				scenes.push_back(item.path());
		}
		if (ec)
		{
			ERROR_PRINT("bench: cannot scan scenes under %s: %s", root.string().c_str(), ec.message().c_str());
			return std::nullopt;
		}
		if (scenes.empty()) return std::nullopt;

		std::sort(scenes.begin(), scenes.end());
		return fs::relative(scenes.front(), root, ec).replace_extension().generic_string();
	}

	// Load a .tgs scene: <gameRoot>/<name>.tgs + its <name>.leveldata/ folder of
	// object files. Each object references a .tgo (via "path") or carries the
	// Model property inline, plus translation/rotation/scale.
	std::vector<SceneEntry> LoadTgs(const std::string& name)
	{
		std::vector<SceneEntry> out;
		const fs::path root = fs::path(Tga::Settings::GameAssetRoot());
		const fs::path scenePath = root / fs::path(name).replace_extension(".tgs");
		fs::path levelData = scenePath;
		levelData.replace_extension(".leveldata");
		std::error_code ec;
		if (!fs::exists(levelData, ec))
		{
			ERROR_PRINT("bench: no leveldata for scene '%s'", name.c_str());
			return out;
		}

		// Lazily built once per scene load (only if an object actually needs
		// "object-definition" resolution) and reused for every object, instead of
		// re-walking the entire asset tree with recursive_directory_iterator for
		// each individual object -- that was an O(objects * assetTreeSize) scan
		// that dominated load time on scenes with many objects.
		std::unordered_map<std::string, fs::path> tgoByStem;
		bool tgoByStemBuilt = false;
		auto buildTgoIndex = [&]()
		{
			if (tgoByStemBuilt) return;
			tgoByStemBuilt = true;
			std::error_code scanEc;
			for (const fs::directory_entry& de : fs::recursive_directory_iterator(root, scanEc))
			{
				if (de.is_regular_file() && de.path().extension() == ".tgo")
					tgoByStem.emplace(de.path().stem().string(), de.path());
			}
			if (scanEc)
				ERROR_PRINT("bench: cannot scan object-definitions under %s: %s", root.string().c_str(), scanEc.message().c_str());
		};

		for (const fs::directory_entry& item : fs::directory_iterator(levelData, ec))
		{
			if (!item.is_regular_file() || item.path().has_extension()) continue;
			std::ifstream in(item.path());
			if (!in) continue;
			json obj; try { in >> obj; } catch (...) { continue; }

			SceneEntry e;
			bool haveModel = ParseModelProperty(obj, e);
			if (haveModel) ParsePhysicsProperties(obj, e.physics);
			if (!haveModel && obj.contains("path") && !obj["path"].get<std::string>().empty())
			{
				std::string tgoRel = obj["path"].get<std::string>();
				std::replace(tgoRel.begin(), tgoRel.end(), '\\', '/');
				if (auto le = LoadTgo(root / tgoRel)) { e = *le; haveModel = true; }
			}
			// The real editor format: "object-definition": "<name>" resolves to
			// the <name>.tgo object definition anywhere under the game data root.
			if (!haveModel && obj.contains("object-definition"))
			{
				const std::string defName = obj["object-definition"].get<std::string>();
				if (!defName.empty())
				{
					buildTgoIndex();
					auto found = tgoByStem.find(defName);
					if (found != tgoByStem.end()) { if (auto le = LoadTgo(found->second)) { e = *le; haveModel = true; } }
					else ERROR_PRINT("bench: object-definition '%s' not found under %s",
						defName.c_str(), root.string().c_str());
				}
			}
			if (!haveModel) continue;

			Vector3f t{ 0,0,0 }, r{ 0,0,0 }, sc{ 1,1,1 };
			auto arr3 = [](const json& a, Vector3f& v) {
				if (a.is_array() && a.size() >= 3) v = { a[0].get<float>(), a[1].get<float>(), a[2].get<float>() };
			};
			if (obj.contains("translation")) arr3(obj["translation"], t);
			if (obj.contains("rotation"))    arr3(obj["rotation"], r);
			if (obj.contains("scale"))       arr3(obj["scale"], sc);

			Matrix4x4f m = Matrix4x4f::CreateFromScale(sc) * Matrix4x4f::CreateFromRollPitchYaw(r);
			m.SetPosition(t);
			e.transform = m;
			out.push_back(std::move(e));
		}
		return out;
	}
}
