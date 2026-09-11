#pragma once
#include "tge/sprite/sprite.h"

namespace Tga
{
    class ParticleEmitter;
    
    class ParticleRenderer
    {
    public:
        bool Init(const std::string& aTexturePath);

        // false = alpha blend (smoke/fog), true = additive (fire/sparks/magic).
        void SetAdditiveBlend(bool isAdditive) { myUseAdditiveBlend = isAdditive; }

        // Call during the 3D render pass. Reads the current camera off the
        // state stack to face the quads at the viewer.
        void Render(const ParticleEmitter& aEmitter);

    private:
        SpriteSharedData mySharedData;
        std::vector<Sprite3DInstanceData> myInstances;
        bool myUseAdditiveBlend = false;
    };
}

