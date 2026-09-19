#pragma once

#include <age/particles/ParticleSystem.h>

#include <age/sprite/sprite.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace Ag::Particles
{
	// Draws the sprite renderer of every emitter in a system. Call it during the forward/transparent pass:
	// it reads the current camera off the graphics state stack.
	class ParticleRenderer
	{
	public:
		// aRadianceScale multiplies every sprite's brightness. The game leaves it at 1 (its HDR pipeline is photometric);
		// a low-dynamic-range preview passes 1 / NitsToUnits(reference) so that reference brightness maps to white.
		void Render(const SystemInstance& aSystem, float aRadianceScale = 1.f);

	private:
		const SpriteSharedData& SharedDataFor(const std::string& aTexturePath);

		// One entry per texture path, including paths that failed to load (so they are not retried every frame).
		std::unordered_map<std::string, SpriteSharedData> mySharedData;
		std::vector<Sprite3DInstanceData> myInstances;
		std::vector<uint32_t> myOrder;
		std::vector<float> myDepth;
	};
}
