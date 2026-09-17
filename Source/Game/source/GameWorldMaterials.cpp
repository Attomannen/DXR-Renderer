#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"

// GameWorld: material registration for scene instances and the preview spheres.
// Every material goes through RegisterMaterial, so raster and DXR always read
// the same parameters, textures and ray visibility.

uint32_t GameWorld::Impl::RegisterMaterial(const std::string& aName, const MaterialAsset& aMaterial,
	const TextureResource* const* someTextures)
{
	using Table = RayTracingMaterialTable;
	const uint32_t index = Table::GetOrAssignMaterialIndex(StringRegistry::RegisterOrGetString(aName));
	auto srv = [&](int slot) { return someTextures && someTextures[slot] ? someTextures[slot]->GetSrv() : rhi::SrvHandle{}; };
	Table::SetMaterialTextures(index, { srv(0), srv(1), srv(2), srv(3) });
	Table::SetMaterialParams(index, aMaterial.ToParams());
	// "Opaque" with a base colour map can still carry cutout alpha (Sponza's
	// plants are authored that way), so rays keep the alpha test unless the
	// cooker recorded that the map has no alpha.
	const bool provablyOpaque = !aMaterial.IsMasked()
		&& (aMaterial.maps[MaterialAsset::BaseColor].empty() || !aMaterial.baseColorHasAlpha);
	Table::SetRayVisibility(index, aMaterial.IsTransparent() ? Table::kRayTransparent
		: provablyOpaque ? Table::kRayOpaque : Table::kRayMasked);
	return index;
}

void GameWorld::Impl::ApplySceneMaterial(ModelInstance& anInstance, int aMesh, const std::string& aMaterialPath,
	const MaterialAsset& aMaterial)
{
	TextureManager& textures = GraphicsEngine::GetInstance()->GetTextureManager();
	bool allMaps = true;
	for (int slot = 0; slot < 4; ++slot)
	{
		const std::string& map = aMaterial.maps[slot];
		if (map.empty()) { allMaps = false; continue; }
		const TextureSrgbMode mode = aMaterial.MapIsSrgb(slot) ? TextureSrgbMode::ForceSrgbFormat : TextureSrgbMode::ForceNoSrgbFormat;
		if (Texture* texture = textures.GetTexture(map.c_str(), mode)) anInstance.SetTexture(aMesh, slot, texture);
	}

	// Slots the .tgmat leaves empty keep the mesh's own maps, so such a record
	// is only shareable between identical meshes.
	std::string name = "tgmat/" + aMaterialPath;
	if (!allMaps && anInstance.GetModel())
		name += "@" + anInstance.GetModel()->GetPath() + "#" + std::to_string(aMesh);
	anInstance.SetMaterial(aMesh, RegisterMaterial(name, aMaterial, anInstance.GetTextures(aMesh)));
}

void GameWorld::Impl::UpdateDebugMaterials()
{
	if (!debugBallValid) return;

	// Reload the preview's maps only when they change.
	std::string signature = debugMat.emissiveMode;
	for (const std::string& map : debugMat.maps) signature += "|" + map;
	if (signature != debugMatLoadedMaps)
	{
		debugMatLoadedMaps = signature;
		TextureManager& textures = GraphicsEngine::GetInstance()->GetTextureManager();
		const std::shared_ptr<Model> sphere = debugBall.GetModel();
		for (int slot = 0; slot < 4; ++slot)
		{
			const TextureResource* texture = sphere->GetDefaultTextures(0)[slot];
			if (!debugMat.maps[slot].empty())
			{
				const TextureSrgbMode mode = debugMat.MapIsSrgb(slot) ? TextureSrgbMode::ForceSrgbFormat : TextureSrgbMode::ForceNoSrgbFormat;
				if (Texture* loaded = textures.GetTexture(debugMat.maps[slot].c_str(), mode)) texture = loaded;
				else ERROR_PRINT("material preview: cannot load %s", debugMat.maps[slot].c_str());
			}
			for (int mesh = 0; mesh < (int)sphere->GetMeshCount(); ++mesh)
			{
				debugBall.SetTexture(mesh, slot, texture);
				for (ModelInstance& ball : orbitBalls) if (ball.IsValid()) ball.SetTexture(mesh, slot, texture);
			}
		}
	}

	debugMaterialIndex = RegisterMaterial("debug/sphere", debugMat, debugBall.GetTextures(0));
	debugBall.SetMaterialAll(debugMaterialIndex);
	for (ModelInstance& ball : orbitBalls) ball.SetMaterialAll(debugMaterialIndex);

	pillarMaterialIndex = RegisterMaterial("debug/pillar", pillarMat, nullptr);
}

bool GameWorld::Impl::LoadDebugMaterial(const char* aPath)
{
	MaterialAsset loaded;
	if (!aPath || !*aPath || !LoadTgmat(aPath, loaded)) return false;
	debugMat = loaded;
	return true;
}
