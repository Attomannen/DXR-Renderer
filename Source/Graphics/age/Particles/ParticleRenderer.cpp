#include "stdafx.h"
#include "ParticleRenderer.h"

#include "age/drawers/SpriteDrawer.h"
#include "age/graphics/GraphicsEngine.h"
#include "age/Graphics/GraphicsStateStack.h"
#include "age/texture/TextureManager.h"
#include <age/math/Photometry.h>

#include <algorithm>
#include <cmath>

namespace Ag::Particles
{
	namespace
	{
		Vector3f Normalized(const Vector3f& v, const Vector3f& aFallback)
		{
			const float length = v.Length();
			return length > 1e-6f ? v * (1.f / length) : aFallback;
		}
	}

	const SpriteSharedData& ParticleRenderer::SharedDataFor(const std::string& aTexturePath)
	{
		auto it = mySharedData.find(aTexturePath);
		if (it != mySharedData.end())
			return it->second;

		SpriteSharedData data;
		if (!aTexturePath.empty())
			data.texture = GraphicsEngine::GetInstance()->GetTextureManager().GetTexture(aTexturePath.c_str());
		return mySharedData.emplace(aTexturePath, data).first->second;
	}

	void ParticleRenderer::Render(const SystemInstance& aSystem)
	{
		GraphicsStateStack& stack = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
		SpriteDrawer& spriteDrawer = GraphicsEngine::GetInstance()->GetSpriteDrawer();

		const Matrix4x4f cam = stack.GetCamera().GetTransform();
		const Vector3f camRight(cam(1, 1), cam(1, 2), cam(1, 3));
		const Vector3f camUp(cam(2, 1), cam(2, 2), cam(2, 3));
		const Vector3f camForward(cam(3, 1), cam(3, 2), cam(3, 3));
		const Vector3f camPosition(cam(4, 1), cam(4, 2), cam(4, 3));
		const Matrix4x4f& systemTransform = aSystem.GetTransform();

		for (size_t e = 0; e < aSystem.EmitterCount(); ++e)
		{
			const EmitterAsset& asset = aSystem.GetAsset().emitters[e];
			const RendererSettings& settings = asset.renderer;
			const ParticleBuffers& particles = aSystem.GetEmitter(e).particles;
			const size_t count = particles.Count();
			if (!asset.enabled || !settings.enabled || count == 0)
				continue;

			const SpriteSharedData& shared = SharedDataFor(settings.texture);
			if (!shared.texture)
				continue;

			const bool local = asset.space == SimulationSpace::Local;
			const auto worldPosition = [&](size_t i) { return local ? SystemInstance::TransformPoint(systemTransform, particles.position[i]) : particles.position[i]; };
			const auto worldVelocity = [&](size_t i) { return local ? SystemInstance::TransformVector(systemTransform, particles.velocity[i]) : particles.velocity[i]; };

			// Back to front, so overlapping soft sprites blend in the right order.
			myOrder.resize(count);
			for (size_t i = 0; i < count; ++i) myOrder[i] = (uint32_t)i;
			if (settings.sort == SortMode::ViewDepth && settings.blend == BlendMode::Alpha)
			{
				myDepth.resize(count);
				for (size_t i = 0; i < count; ++i) myDepth[i] = (worldPosition(i) - camPosition).Dot(camForward);
				std::sort(myOrder.begin(), myOrder.end(), [this](uint32_t a, uint32_t b) { return myDepth[a] > myDepth[b]; });
			}

			const float brightness = Photometry::NitsToUnits(settings.brightness);
			const int columns = std::max(1, settings.flipbookColumns);
			const int rows = std::max(1, settings.flipbookRows);
			const int frames = columns * rows;

			myInstances.clear();
			myInstances.reserve(count);
			for (const uint32_t i : myOrder)
			{
				const float size = particles.size[i];
				const Vector3f position = worldPosition(i);
				Vector3f right, up;

				switch (settings.facing)
				{
				case FacingMode::VelocityAligned:
				{
					const Vector3f velocity = worldVelocity(i);
					const float speed = velocity.Length();
					const Vector3f along = Normalized(velocity, camUp);
					right = Normalized(along.Cross(camForward), camRight) * size;
					up = along * (size * (1.f + settings.velocityStretch * speed));
					break;
				}
				case FacingMode::VerticalBillboard:
				{
					right = Normalized(Vector3f(0.f, 1.f, 0.f).Cross(camForward), camRight) * size;
					up = Vector3f(0.f, size, 0.f);
					break;
				}
				default:
				{
					// Spin the right/up axes around the view direction.
					const float cs = std::cos(particles.rotation[i]);
					const float sn = std::sin(particles.rotation[i]);
					right = (camRight * cs + camUp * sn) * size;
					up = (camUp * cs - camRight * sn) * size;
					break;
				}
				}

				// The sprite quad spans x:[0,1], y:[-1,0]; recentre it on the particle.
				const Vector3f origin = position - right * 0.5f + up * 0.5f;

				Sprite3DInstanceData instance;
				instance.transform = Matrix4x4f{
					right.x, right.y, right.z, 0.f,
					up.x, up.y, up.z, 0.f,
					camForward.x, camForward.y, camForward.z, 0.f,
					origin.x, origin.y, origin.z, 1.f };
				const Vector4f c = particles.color[i];
				// The sprite shader decodes colours from sRGB, so hand it the encoded value of the linear radiance we want.
				instance.color = Color::FromLinear(c.x * brightness, c.y * brightness, c.z * brightness, c.w);
				if (frames > 1)
				{
					const int frame = std::clamp((int)std::floor(particles.frame[i]), 0, frames - 1);
					instance.uvScale = Vector2f(1.f / (float)columns, 1.f / (float)rows);
					instance.uv = Vector2f((float)(frame % columns) / (float)columns, (float)(frame / columns) / (float)rows);
				}
				myInstances.push_back(instance);
			}

			stack.Push();
			stack.SetBlendState(settings.blend == BlendMode::Additive ? BlendState::AdditiveBlend : BlendState::AlphaBlend);
			stack.SetDepthStencilState(DepthStencilState::ReadOnlyLess);
			stack.SetRasterizerState(RasterizerState::NoFaceCulling);   // the forward pass culls back faces; a billboard has no "back"
			stack.SetTransform(Matrix4x4f());
			{
				// BeginBatch -> PrepareRender -> UpdateGpuStates flushes the states set above.
				SpriteBatchScope batch = spriteDrawer.BeginBatch(shared);
				batch.Draw(myInstances.data(), myInstances.size());
			}
			stack.Pop();
		}
	}
}
