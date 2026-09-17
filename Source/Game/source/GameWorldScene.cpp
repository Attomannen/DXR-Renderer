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
	std::ifstream lightingFile(fs::path(Settings::GameAssetRoot()) / fs::path(sceneName).replace_extension(".tgs"));
	if (lightingFile) {
		try {
			json document; lightingFile >> document;
			if (document.contains("lighting")) {
				const auto& lighting = document["lighting"];
				sunPitch = lighting.value("sunPitch", sunPitch);
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

	models.clear();
	opaqueMeshes.clear();
	transparentMeshes.clear();
	instanceOffsets.clear();
	pointLights.clear();
	lightExtra.clear();
	anyTransparent = false;

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
		for (int m = 0; m < meshCount && m < (int)e.materials.size(); ++m)
		{
			if (e.materials[m].empty()) continue;
			if (!LoadTgmat(fs::path(Settings::GameAssetRoot()) / e.materials[m], materials[m])) continue;
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

			for (int m = 0; m < meshCount && m < (int)e.materials.size(); ++m)
			{
				if (!authoredMaterial[m]) continue;
				ApplySceneMaterial(mi, m, e.materials[m], materials[m]);
			}

			const int gx = i % side, gz = i / side;
			const float ox = tileCopies ? (gx - (side - 1) * 0.5f) * step : 0.f;
			const float oz = tileCopies ? (gz - (side - 1) * 0.5f) * step : 0.f;
			Matrix4x4f xf = Matrix4x4f::CreateFromRollPitchYaw(Vector3f{ modelRotX, 0.f, 0.f }) * e.transform;
			xf.SetPosition(xf.GetPosition() + Vector3f{ ox, 0.f, oz });
			mi.SetTransform(xf);
			models.push_back(mi);
			instanceOffsets.push_back(Vector3f{ ox, 0.f, oz });

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
	if (models.empty()) { ERROR_PRINT("bench: no instances created"); return false; }

	// Scene bounds = union of every instance's world-space AABB.
	{
		Vector3f mn{ 1e30f, 1e30f, 1e30f }, mx{ -1e30f, -1e30f, -1e30f };
		for (const ModelInstance& mi : models)
		{
			if (!mi.GetModel()) continue;
			const Tga::BoxSphereBounds& b = mi.GetModel()->GetBounds();
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
	camLoaded = false;
	if (LoadCamera())
	{
		if (aEnv)
		{
			const std::string mode = bench.camMode.value_or(sceneName.empty() ? "spin" : "fixed");
			if      (mode == "fixed") camMode = CamMode::Fixed;
			else if (mode == "orbit") camMode = CamMode::Orbit;
			else if (mode == "room")  { camMode = CamMode::Orbit; orbitRoom = true; }
			else                      camMode = CamMode::Spin;
		}
		INFO_PRINT("bench: camera '%s'", camFile.c_str());
	}
	camera.GetTransform().SetPosition(camPos);
	camera.GetTransform().SetRotation(camRot);
	return true;
}
