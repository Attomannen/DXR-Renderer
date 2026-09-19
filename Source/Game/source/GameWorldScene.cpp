#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"

// GameWorld: Scene content and light loading.

void GameWorld::Impl::ComputeBounds(const std::shared_ptr<Model>& model)
{
	if (bench.modelRotX) modelRotX = *bench.modelRotX;

	const float rad = Deg2Rad(modelRotX);
	const float cs = std::cos(rad), sn = std::sin(rad);
	auto rot = [&](Vector3f v) { return Vector3f{ v.x, v.y * cs - v.z * sn, v.y * sn + v.z * cs }; };

	Vector3f mn{ 1e9f, 1e9f, 1e9f }, mx{ -1e9f, -1e9f, -1e9f };
	for (const auto& md : model->GetMeshDataList())
	{
		const Vector3f c = md.bounds.center;
		const Vector3f e = md.bounds.boxExtents;
		for (int sx = -1; sx <= 1; sx += 2)
		for (int sy = -1; sy <= 1; sy += 2)
		for (int sz = -1; sz <= 1; sz += 2)
		{
			const Vector3f p = rot({ c.x + sx * e.x, c.y + sy * e.y, c.z + sz * e.z });
			mn = { std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z) };
			mx = { std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z) };
		}
	}
	if (mn.x > mx.x) { mn = { -1000,-1000,-1000 }; mx = { 1000,1000,1000 }; }
	sceneCenter  = (mn + mx) * 0.5f;
	sceneExtents = (mx - mn) * 0.5f;
}

bool GameWorld::Impl::LoadLights(const std::string& file, float sceneRadius, float exposure)
{
	std::ifstream in(file);
	if (!in) return false;
	nlohmann::json j;
	try { in >> j; } catch (...) { ERROR_PRINT("bench: bad light file %s", file.c_str()); return false; }
	if (j.contains("lighting")) j = j["lighting"];
	if (!j.contains("lights") || !j["lights"].is_array()) return false;

	auto a3 = [](const nlohmann::json& v, Vector3f d) {
		if (v.is_array() && v.size() >= 3)
			return Vector3f{ v[0].get<float>(), v[1].get<float>(), v[2].get<float>() };
		return d;
	};

	int n = 0, skipped = 0;
	for (const nlohmann::json& L : j["lights"])
	{
		if (n >= DeferredRenderer::kMaxLights) { ++skipped; continue; }
		PointLight p;
		const Vector3f pos = a3(L.value("pos", L.value("position", nlohmann::json::array({0,0,0}))), Vector3f{0,0,0});
		const Vector3f col = L.contains("color") ? a3(L["color"], Vector3f{ 1,1,1 }) : Vector3f{ 1,1,1 };
		const float intensity = L.value("intensity", 6.0f) * exposure;
		p.position = pos;
		p.color  = Color{ col.x * intensity, col.y * intensity, col.z * intensity, 1.f };
		p.range  = L.value("range",  sceneRadius * 1.2f);
		p.radius = L.value("radius", 20.0f);
		pointLights.push_back(p);

		// Optional spot cone: "spot": { "dir":[x,y,z], "outer":<deg>, "inner":<deg> }
		LightExtra ex;
		if (L.contains("spot") && L["spot"].is_object())
		{
			const nlohmann::json& sp = L["spot"];
			Vector3f dir = a3(sp.value("dir", nlohmann::json::array({ 0, -1, 0 })), Vector3f{ 0,-1,0 });
			float len = dir.Length();
			ex.spotDir = len > 1e-4f ? dir / len : Vector3f{ 0,-1,0 };
			const float outer = sp.value("outer", 35.0f) * 3.14159265f / 180.0f;
			const float inner = sp.value("inner", outer * 0.7f * 180.0f / 3.14159265f) * 3.14159265f / 180.0f;
			ex.spotCosOuter = std::cos(outer);
			ex.spotCosInner = std::cos(std::min(inner, outer - 0.01f));
		}
		if (L.contains("spot") && L["spot"].is_boolean() && L["spot"].get<bool>()) {
			Vector3f dir = a3(L.value("direction", nlohmann::json::array({0,-1,0})), Vector3f{0,-1,0});
			const float len = dir.Length();
			ex.spotDir = len > 1e-4f ? dir / len : Vector3f{0,-1,0};
			const float outer = std::clamp(L.value("outerAngle", 35.0f), 0.1f, 89.9f);
			const float inner = std::clamp(L.value("innerAngle", 20.0f), 0.0f, outer);
			ex.spotCosOuter = std::cos(outer * 3.14159265f / 180.0f);
			ex.spotCosInner = std::cos(inner * 3.14159265f / 180.0f);
		}
		// Photometric alternatives to the unitless "intensity": luminous
		// intensity in candela, or flux in lumens spread over the cone.
		if (L.contains("candela") || L.contains("lumens"))
		{
			const float candela = L.contains("candela") ? L["candela"].get<float>()
				: ex.spotCosOuter > -1.f
				? Photometry::SpotLumensToCandela(L["lumens"].get<float>(), std::acos(ex.spotCosOuter))
				: Photometry::PointLumensToCandela(L["lumens"].get<float>());
			const float units = Photometry::CandelaToUnits(candela) * exposure;
			pointLights.back().color = Color{ col.x * units, col.y * units, col.z * units, 1.f };
		}
		lightExtra.push_back(ex);
		++n;
	}
	INFO_PRINT("bench: %d authored light(s) from %s%s", n, file.c_str(),
		skipped ? "  (some skipped: over kMaxLights)" : "");
	return true;
}

// Re-applies the BENCH_SUN_* values the scene's lighting block just clobbered.
// Only the ones actually given on the command line: an unset override must
// leave the scene's authored value alone.
void GameWorld::Impl::ApplySunOverrides()
{
	if (sunPitchOverridden)  sunPitch = benchSunPitch;
	if (sunYawOverridden)    sunYaw = benchSunYaw;
	if (sunLuxOverridden)    sunIlluminanceLux = benchSunLux;
	if (sunKelvinOverridden) { sunTemperatureK = benchSunKelvin; sunUseTemperature = true; }
}

bool GameWorld::Impl::LoadSceneContent(const std::string& sceneName, bool aEnv)
{
	// Every mesh/texture upload below shares GPU submissions.
	struct UploadBatch
	{
		UploadBatch() { if (rhi::IDevice* d = DX11::Rhi()) d->BeginUploadBatch(); }
		~UploadBatch() { if (rhi::IDevice* d = DX11::Rhi()) d->EndUploadBatch(); }
	} uploadBatch;
	previousRayTransforms.clear();
	if (deferred) deferred->ResetTemporalHistory();
	if (sceneName != currentScene) {
		sunPitch = 55.f; sunYaw = -35.f; sunIlluminanceLux = 100000.f; sunUseTemperature = false;
		sunColor[0] = 1.f; sunColor[1] = 0.96f; sunColor[2] = 0.88f;
		ambientColor[0] = 0.35f; ambientColor[1] = 0.42f; ambientColor[2] = 0.55f;
	}
	currentScene = sceneName;
	levelGameMode.clear();
	std::ifstream lightingFile(fs::path(Settings::GameAssetRoot()) / fs::path(sceneName).replace_extension(".tgs"));
	if (lightingFile) {
		try {
			json document; lightingFile >> document;
			levelGameMode = document.value("gameMode", std::string());
			if (document.contains("lighting")) {
				const auto& lighting = document["lighting"];
				// Negated on the way in, the same as the editor viewport does.
				//
				// The .tgs stores pitch negative-down (the editor's Sun widget
				// writes -75 for a high sun) while the renderer -- and this
				// struct, and BENCH_SUN_PITCH -- take it positive-down. The
				// editor applied that flip in DefaultEditorGraphics and the game
				// did not, so a scene that read as bright midday in the viewport
				// came up at night in the game with the sun 75 degrees below the
				// horizon.
				sunPitch = -lighting.value("sunPitch", -sunPitch);
				sunYaw = lighting.value("sunYaw", sunYaw);
				sunIlluminanceLux = lighting.value("sunIlluminance", lighting.value("sunIntensity", SunIntensity()) * 100000.0f);
				if (lighting.contains("sunTemperature"))
				{
					sunTemperatureK = lighting["sunTemperature"].get<float>();
					sunUseTemperature = true;
				}
				for (int i = 0; i < 3; ++i) {
					if (lighting.contains("sunColor") && lighting["sunColor"].size() == 3) sunColor[i] = lighting["sunColor"][i].get<float>();
					if (lighting.contains("ambientColor") && lighting["ambientColor"].size() == 3) ambientColor[i] = lighting["ambientColor"][i].get<float>();
				}
			}
		} catch (const std::exception& e) { ERROR_PRINT("Scene lighting: %s", e.what()); }
	}
	// An explicitly set BENCH_SUN_* wins over the scene's own lighting. It is
	// applied in BenchConfig, before the scene file is read, so the scene used
	// to silently overwrite it and a run asking for a different sun angle or a
	// night illuminance quietly rendered the scene's daylight instead.
	ApplySunOverrides();

	models.clear();
	opaqueMeshes.clear();
	transparentMeshes.clear();
	instanceOffsets.clear();
	pointLights.clear();
	lightExtra.clear();
	anyTransparent = false;
	ClearScenePhysics();
	ClearSceneScripts();
	SetSceneCameraActive(-1);
	sceneCharacters.clear();
	sceneCameras.clear();
	sceneInstances.clear();
	playerPawn = -1;
	ClearSceneParticles();

	ModelFactory& mf = ModelFactory::GetInstance();
	auto& texMgr = GraphicsEngine::GetInstance()->GetTextureManager();

	std::vector<SceneEntry> entries;
	if (!sceneName.empty())
	{
		entries = LoadTgs(sceneName);
		INFO_PRINT("bench: scene '%s' -> %zu object(s)", sceneName.c_str(), entries.size());
	}
	if (entries.empty())
	{
		ERROR_PRINT("bench: scene '%s' contains no loadable objects", sceneName.c_str());
		return false;
	}

	// The Game Mode adds its own object and spawns the player's pawn at a Player Start.
	SpawnGameModeEntries(entries);

	// Decode every texture the scene's materials reference in parallel
	// before the (main-thread) per-instance texture assignment below.
	{
		std::vector<std::string> texturePaths;
		for (const SceneEntry& e : entries)
			for (const std::string& materialFile : e.materials)
			{
				MaterialDef material;
				if (materialFile.empty() || !LoadTgmat(fs::path(Settings::GameAssetRoot()) / materialFile, material)) continue;
				for (const std::string& map : material.maps)
					if (!map.empty()) texturePaths.push_back(map);
			}
		texMgr.PrefetchTextures(texturePaths);
	}
	struct PrefetchCleanup { TextureManager& t; ~PrefetchCleanup() { t.ClearPrefetchedTextures(); } } prefetchCleanup{ texMgr };

	const auto tLoad0 = std::chrono::high_resolution_clock::now();
	const int side = (int)std::ceil(std::sqrt((double)sponzaCopies));
	const bool tileCopies = (entries.size() == 1);

	for (const SceneEntry& e : entries)
	{
		if (e.fbx.empty())
		{
			// An object without a mesh still has a place in the world and can carry a camera, particles or a script.
			const size_t objectIndex = AddSceneInstance(e, -1, e.transform);
			RegisterSceneScripts(e, objectIndex);
			RegisterSceneCamera(e, objectIndex);
			RegisterSceneParticles(e, objectIndex);
			continue;
		}

		std::shared_ptr<Model> model = mf.GetModel(e.fbx.c_str());
		if (!model) { ERROR_PRINT("bench: failed to load '%s'", e.fbx.c_str()); continue; }
		const int meshCount = std::min((int)model->GetMeshCount(), MAX_MESHES_PER_MODEL);
		// Surface type is authored per scene material.  Keep the legacy name
		// override as a fallback for old scenes, but never let it be the only
		// route by which a .tgmat becomes transparent in raster or DXR.
		std::vector<bool> authoredTransparent(meshCount, false);
		// Masked (real alpha cutout, e.g. foliage/fences) is distinct from
		// Opaque so AcceptRayTriangle can skip the texture sample entirely
		// for ordinary opaque geometry -- see RayTracingMaterialTable::kRayOpaque.
		std::vector<bool> authoredMasked(meshCount, false);
		std::vector<bool> authoredMaterial(meshCount, false);
		std::vector<MaterialDef> materials(meshCount);
		// Which entry of e.materials each mesh actually resolved to. The
		// positional index is only a fallback; see the name match below.
		std::vector<int> resolvedSlot(meshCount, -1);
		// Resolve each mesh's material by NAME, not by slot order.
		//
		// A .tgo is a positional list, which silently assumed the exporter's
		// material order and the importer's mesh order agree. They do not: on a
		// reimported Bistro, mesh 1 carried the material the .tgo listed at slot
		// 0, and the drift changed again further down the list -- so every mesh
		// wore some other mesh's material. Match on the name the mesh actually
		// reports instead, and keep the positional entry only as a fallback for
		// meshes whose name is missing from the list.
		//
		// Names carry suffixes the material assets do not: Blender's ".001" dedup
		// and tags like ".DoubleSided". Try the full name first, then drop
		// dotted suffixes one at a time.
		std::unordered_map<std::string, int> tgmatByName;
		auto lower = [](std::string v) { for (char& c : v) c = (char)std::tolower((unsigned char)c); return v; };
		for (int i = 0; i < (int)e.materials.size(); ++i)
		{
			if (e.materials[i].empty()) continue;
			std::string n = e.materials[i];
			if (const size_t slash = n.find_last_of("/\\"); slash != std::string::npos) n = n.substr(slash + 1);
			if (n.size() > 6) n = n.substr(0, n.size() - 6);   // ".tgmat"
			tgmatByName.emplace(lower(n), i);
		}
		for (int m = 0; m < meshCount; ++m)
		{
			int slot = m < (int)e.materials.size() ? m : -1;
			if (const char* meshMat = model->GetMaterialName(m).GetString(); meshMat && *meshMat)
			{
				for (std::string n = lower(meshMat);;)
				{
					if (auto it = tgmatByName.find(n); it != tgmatByName.end()) { slot = it->second; break; }
					const size_t dot = n.rfind('.');
					if (dot == std::string::npos) break;
					n = n.substr(0, dot);
				}
			}
			if (slot < 0 || slot >= (int)e.materials.size() || e.materials[slot].empty()) continue;
			if (!LoadTgmat(fs::path(Settings::GameAssetRoot()) / e.materials[slot], materials[m])) continue;
			resolvedSlot[m] = slot;
			authoredMaterial[m] = true;
			authoredTransparent[m] = materials[m].IsTransparent();
			authoredMasked[m] = materials[m].IsMasked();
		}

		const int copies = tileCopies ? sponzaCopies : 1;
		const float sizeXZ0 = std::max(sceneExtents.x, sceneExtents.z) * 2.f;
		const float step = sizeXZ0 * 1.15f;

		for (int i = 0; i < copies; ++i)
		{
			ModelInstance mi;
			mi.Init(model);

			// Bounded by meshCount alone. It used to also stop at
			// e.materials.size(), which silently left every mesh past the end of
			// the list untextured even though the name match above had already
			// resolved it -- the list is a pool to match names against, not a
			// per-mesh array.
			for (int m = 0; m < meshCount; ++m)
			{
				if (!authoredMaterial[m]) continue;
				// The RESOLVED entry, not the positional one. ApplySceneMaterial
				// uses this path as the key it registers the material under, so
				// passing e.materials[m] here named each mesh's material after
				// whatever happened to sit at its own index: two meshes sharing a
				// material got two records, and two meshes whose indices collided
				// on one path shared a record built from the first one's textures.
				ApplySceneMaterial(mi, m, e.materials[resolvedSlot[m]], materials[m]);
			}

			const int gx = i % side, gz = i / side;
			const float ox = tileCopies ? (gx - (side - 1) * 0.5f) * step : 0.f;
			const float oz = tileCopies ? (gz - (side - 1) * 0.5f) * step : 0.f;
			Matrix4x4f xf = Matrix4x4f::CreateFromRollPitchYaw(Vector3f{ modelRotX, 0.f, 0.f }) * e.transform;
			xf.SetPosition(xf.GetPosition() + Vector3f{ ox, 0.f, oz });
			mi.SetTransform(xf);
			models.push_back(mi);
			instanceOffsets.push_back(Vector3f{ ox, 0.f, oz });
			const size_t instanceIndex = AddSceneInstance(e, (int)models.size() - 1, xf);
			RegisterScenePhysics(e, model, xf, instanceIndex);
			RegisterSceneScripts(e, instanceIndex);
			RegisterSceneCamera(e, instanceIndex);
			RegisterSceneParticles(e, instanceIndex);
			RegisterSceneCharacter(e, xf, instanceIndex);

			std::vector<int> op, tr;
			for (int m = 0; m < meshCount; ++m)
			{
				const char* mat = model->GetMaterialName(m).GetString();
				const bool transparent = authoredTransparent[m] || MatchesAny(mat ? mat : "", transparentMatKeys);
				// The same classification controls the raster forward pass and
				// the material record consulted by every inline RayQuery.  Forward
				// alpha blend cannot provide a reliable hit distance/transmittance,
				// so let it composite after DXR instead of treating glass as opaque.
				// Authored .tgmat instances were classified by ApplySceneMaterial;
				// this covers the mesh's own material (legacy name keys).
				if (!authoredMaterial[m])
				{
					using Table = RayTracingMaterialTable;
					const uint32_t meshMaterial = model->GetMeshData(m).rayGeometry.materialIndex;
					Table::SetRayVisibility(meshMaterial, transparent ? Table::kRayTransparent : Table::kRayOpaque);
					if (transparent)
					{
						MaterialParams params = Table::GetMaterialParams(meshMaterial);
						params.shadingModel = (uint32_t)ShadingModel::Glass;
						Table::SetMaterialParams(meshMaterial, params);
					}
				}
				(transparent ? tr : op).push_back(m);
			}
			if (!tr.empty()) anyTransparent = true;
			opaqueMeshes.push_back(std::move(op));
			transparentMeshes.push_back(std::move(tr));
		}
	}
	modelLoadMs = std::chrono::duration<double, std::milli>(
		std::chrono::high_resolution_clock::now() - tLoad0).count();
	if (physics.IsInitialized()) physics.OptimizeBroadPhase();
	if (sceneInstances.empty()) { ERROR_PRINT("bench: no instances created"); return false; }

	// Scene bounds = union of every instance's world-space AABB.
	{
		Vector3f mn{ 1e30f, 1e30f, 1e30f }, mx{ -1e30f, -1e30f, -1e30f };
		for (const ModelInstance& mi : models)
		{
			if (!mi.GetModel()) continue;
			const Ag::BoxSphereBounds& b = mi.GetModel()->GetBounds();
			const Matrix4x4f& w = mi.GetTransform();
			for (int sx = -1; sx <= 1; sx += 2)
			for (int sy = -1; sy <= 1; sy += 2)
			for (int sz = -1; sz <= 1; sz += 2)
			{
				Vector4f c(b.center.x + sx * b.boxExtents.x, b.center.y + sy * b.boxExtents.y, b.center.z + sz * b.boxExtents.z, 1.f);
				Vector4f p = c * w;
				mn = { std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z) };
				mx = { std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z) };
			}
		}
		if (mn.x <= mx.x)
		{
			sceneCenter = (mn + mx) * 0.5f;
			sceneExtents = (mx - mn) * 0.5f;
		}
	}

	// Material-preview debug sphere (Source/Game/data/Primitives/Sphere.fbx).
	if (!debugBallValid)
	{
		if (std::shared_ptr<Model> sph = mf.GetModel("Primitives/Sphere.fbx"))
		{
			debugBall.Init(sph);
			debugBallValid = true;
			debugBallModelRadius = std::max(sph->GetBounds().radius, 0.001f);
			{
				const Vector3f& e = sph->GetBounds().boxExtents;
				debugBallModelExtent = std::max({ e.x, e.y, e.z, 0.001f });
			}
			orbitBalls.clear();
			orbitBalls.resize(kMaxOrbitBalls);
			for (ModelInstance& mi : orbitBalls) mi.Init(sph);
		}
		else ERROR_PRINT("material preview: Primitives/Sphere.fbx not found");
	}
	debugBallPos = sceneCenter - Vector3f{ 0.f, sceneExtents.y * 0.2f, 0.f };

	// Auto-size the orbit path / sphere radius to whatever scene just loaded.
	orbitPathRadius = std::max(sceneExtents.x, sceneExtents.z) * 0.55f;
	orbitBallRadius = std::clamp(orbitPathRadius * 0.10f, 8.f, 400.f);
	orbitHeight = 0.f;

	const float sizeXZ = std::max(sceneExtents.x, sceneExtents.z) * 2.f;
	orbitRadius = std::clamp(std::max(sceneExtents.x, sceneExtents.z) * 0.42f, 150.f, 6000.f);
	if (aEnv && bench.orbitRadius) orbitRadius = std::max(1.f, *bench.orbitRadius);
	flySpeed = std::clamp(sizeXZ * 0.35f, 400.f, 6000.f);
	camPos   = sceneCenter + Vector3f{ 0, sceneExtents.y * 0.1f, -orbitRadius };

	const float sceneRadius = std::sqrt(sceneExtents.x * sceneExtents.x
		+ sceneExtents.y * sceneExtents.y + sceneExtents.z * sceneExtents.z);
	float exposure = 1.0f;
	if (aEnv && bench.exposure) exposure = std::max(0.01f, *bench.exposure);

	const fs::path tgsPath = fs::path(Settings::GameAssetRoot()) / fs::path(sceneName).replace_extension(".tgs");
	LoadLights(tgsPath.string(), sceneRadius, exposure);

	// Start camera: the scene's placed camera file if present, else orbit-derived.
	// A scene name is a path ("Scenes/TEST"), so it cannot go into a file name
	// verbatim: that asked for bench_camera_Scenes/TEST.json, whose directory
	// does not exist, and the F5 save then failed while still reporting success.
	std::string camKey = sceneName;
	for (char& c : camKey) if (c == '/' || c == '\\') c = '_';
	const std::string defCamFile = camKey.empty() ? std::string("bench_camera.json")
	                                              : ("bench_camera_" + camKey + ".json");
	camFile = aEnv ? bench.camFile.value_or(defCamFile) : defCamFile;
	if (aEnv && bench.spinDeg) camSpinDeg = *bench.spinDeg;
	if (aEnv && bench.bobM) camBobM = *bench.bobM;
	if (aEnv && bench.bobPitch) camBobPitch = *bench.bobPitch;
	if (aEnv && bench.bobHold) camBobHoldFrames = *bench.bobHold;
	camLoaded = false;
	if (LoadCamera())
	{
		if (aEnv)
		{
			const std::string mode = bench.camMode.value_or(sceneName.empty() ? "spin" : "fixed");
			if      (mode == "fixed") camMode = CamMode::Fixed;
			else if (mode == "orbit") camMode = CamMode::Orbit;
			else if (mode == "room")  { camMode = CamMode::Orbit; orbitRoom = true; }
			else if (mode == "bob")   camMode = CamMode::Bob;
			else                      camMode = CamMode::Spin;
		}
		INFO_PRINT("bench: camera '%s'", camFile.c_str());
	}
	camera.GetTransform().SetPosition(camPos);
	camera.GetTransform().SetRotation(camRot);
	// Scene bounds have just changed, so the GI probe volume that is derived
	// from them is stale until this runs.
	RecomputeGiVolume();
	return true;
}
