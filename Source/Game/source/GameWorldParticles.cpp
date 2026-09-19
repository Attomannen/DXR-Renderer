#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"

void GameWorld::Impl::ClearSceneParticles()
{
	sceneParticles.clear();
}

void GameWorld::Impl::RegisterSceneParticles(const GameScene::SceneEntry& entry, size_t instanceIndex)
{
	if (!entry.particles.has)
		return;

	const std::string resolved = Settings::ResolveAssetPath(entry.particles.path);
	Particles::SystemAsset asset;
	if (resolved.empty() || !asset.Load(resolved))
	{
		ERROR_PRINT("particles: cannot load '%s'", entry.particles.path.c_str());
		return;
	}

	SceneParticleObject object;
	object.instance = instanceIndex;
	object.system = std::make_unique<Particles::SystemInstance>(asset);
	object.system->SetTransform(GetInstanceTransform(instanceIndex));
	if (!entry.particles.activateOnStart)
		object.system->Deactivate();
	sceneParticles.push_back(std::move(object));
}

void GameWorld::Impl::UpdateSceneParticles(float deltaSeconds)
{
	for (SceneParticleObject& object : sceneParticles)
	{
		object.system->SetTransform(GetInstanceTransform(object.instance));
		object.system->Update(deltaSeconds);
	}
}

void GameWorld::Impl::DrawSceneParticles()
{
	for (const SceneParticleObject& object : sceneParticles)
		particleRenderer.Render(*object.system);
}
