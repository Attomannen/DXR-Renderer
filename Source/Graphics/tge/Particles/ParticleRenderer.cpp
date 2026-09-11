#include "stdafx.h"
#include "ParticleRenderer.h"

#include "ParticleEmitter.h"
#include "tge/drawers/SpriteDrawer.h"
#include "tge/graphics/GraphicsEngine.h"
#include "tge/Graphics/GraphicsStateStack.h"
#include "tge/texture/TextureManager.h"


using namespace Tga;

bool ParticleRenderer::Init(const std::string& aTexturePath)
{
    mySharedData.texture = GraphicsEngine::GetInstance()->GetTextureManager().GetTexture(aTexturePath.c_str());
    return mySharedData.texture != nullptr;
}

void ParticleRenderer::Render(const ParticleEmitter& aEmitter)
{
    const std::vector<Particle>& particles = aEmitter.GetParticles();

    GraphicsStateStack& stack = GraphicsEngine::GetInstance()->GetGraphicsStateStack();

    const Matrix4x4f cam = stack.GetCamera().GetTransform();
    const Vector3f camRight = { cam(1, 1), cam(1, 2), cam(1, 3) };
    const Vector3f camUp    = { cam(2, 1), cam(2, 2), cam(2, 3) };
    const Vector3f camFwd   = { cam(3, 1), cam(3, 2), cam(3, 3) };

    myInstances.clear();
    myInstances.reserve(particles.size());

    for (const auto& particle : particles)
    {
        if (!particle.isAlive)
        {
            continue;
        }

        const float t = particle.age / particle.lifetime;
        const float size = aEmitter.SizeAtAge(t);
        const Vector4f c = aEmitter.ColorAtAge(t);

        // spin the right/up axes around the view direction
        const float cs = std::cos(particle.rotation);
        const float sn = std::sin(particle.rotation);
        const Vector3f R = (camRight * cs + camUp * sn) * size;
        const Vector3f U = (camUp * cs - camRight * sn) * size;
 
        // SpriteDrawer's base quad spans x:[0,1], y:[-1,0]; recenter on the particle
        const Vector3f origin = particle.position - R * 0.5f + U * 0.5f;

        Sprite3DInstanceData instanceData;
        instanceData.transform = Matrix4x4f{
        R.x, R.y, R.z, 0.f,
        U.x, U.y, U.z, 0.f,
        camFwd.x, camFwd.y, camFwd.z, 0.f,
        origin.x, origin.y, origin.z, 1.f};
        instanceData.color = Color{c.x, c.y, c.z, c.w};
        myInstances.push_back(instanceData);
    }
    if (myInstances.empty())
    {
        return;
    }

    stack.Push();
    stack.SetBlendState(myUseAdditiveBlend ? BlendState::AdditiveBlend : BlendState::AlphaBlend);
    stack.SetDepthStencilState(DepthStencilState::ReadOnlyLess);
    stack.SetTransform(Matrix4x4f());

    SpriteDrawer& spriteDrawer = GraphicsEngine::GetInstance()->GetSpriteDrawer();
    {
        // BeginBatch -> PrepareRender -> UpdateGpuStates flushes the states set above.
        SpriteBatchScope batch = spriteDrawer.BeginBatch(mySharedData);
        batch.Draw(myInstances.data(), myInstances.size());
    }
    stack.Pop();
    
}

