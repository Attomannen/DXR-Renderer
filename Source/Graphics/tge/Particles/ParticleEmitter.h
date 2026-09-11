#pragma once
//#include "tge/error/ErrorManager.h"
#include "tge/math/Vector3.h"
#include "tge/math/Vector4.h"

namespace Tga
{
    struct ParticleEmitterSettings
    {
        std::string texturePath = "Textures/smoke.dds";

        //spawning
        float spawnRate = 30.f; //particles per sec
        int maxParticles = 512; //pool size
        float spawnRadius = 0.0f; 

        //lifetime
        float lifetime = 1.5f;
        float lifetimeVariance = 1.5f;

        //motion
        Vector3f startVelocity = Vector3f(0.f, 1.f, 0.f);
        float velocityVariance = 0.5f;
        float spawnConeAngle = 30.f; //degrees of spread around startVelocity
        Vector3f acceleration = Vector3f(0.f, 0.5f, 0.f); //buoyancy/gravity
        float drag = 0.f;

        //appearance
        Vector4f startColor = Vector4f(0.6f, 0.6f, 0.6f, 0.f);
        Vector4f midColor = Vector4f(0.5f, 0.5f, 0.5f, 0.4f);
        Vector4f endColor = Vector4f(0.3f, 0.3f, 0.3f, 0.f);

        float startSize = 0.5f;
        float endSize = 3.f;

        //spin in radians
        float startSpin = 0.f;
        float spinSpeed = 0.f;
        float spinVariance = 0.f;
    };

    inline ParticleEmitterSettings MakeSmokeSettings()
    {
        ParticleEmitterSettings s;
        s.texturePath = "Textures/PNG/Black_Smoke/blackSmoke00.dds";
        s.spawnRate = 15.f;
        s.lifetime = 2.5f;  s.lifetimeVariance = 0.5f;
        s.startVelocity = { 0.f, 90.f, 0.f };   // 90 units/sec up
        s.velocityVariance = 20.f;  s.spawnConeAngle = 10.f;
        s.acceleration = { 0.f, 30.f, 0.f };
        s.startSize = 150.f;  s.endSize = 300.f;
        s.spinSpeed = 0.4f;  s.spinVariance = 0.6f;
        s.startColor = { 1.f, 1.f, 1.f, 0.f };
        s.midColor = { 1.f, 1.f, 1.f, 1.f };
        s.endColor = { 1.f, 1.f, 1.f, 0.f };
        return s;
    }

    inline ParticleEmitterSettings MakeFogSettings()
    {
        ParticleEmitterSettings s;
        s.texturePath = "Textures/PNG/White_Puff/whitePuff00.dds"; // same soft texture works
        s.spawnRate = 8.f;
        s.lifetime = 6.0f;   s.lifetimeVariance = 1.5f;
        s.startVelocity = { 0.1f, 0.f, 0.f };  // barely moves, drifts sideways
        s.velocityVariance= 0.15f;  s.spawnConeAngle = 80.f;
        s.acceleration = { 0.f, 0.f, 0.f };   // no rise
        s.startSize = 3000.f;  s.endSize = 5000.f;
        s.startColor = { .7f,.7f,.75f, 0.0f };
        s.midColor = { .7f,.7f,.75f, 0.18f };   // low alpha = diffuse
        s.endColor = { .7f,.7f,.75f, 0.0f };
        s.spinSpeed = 0.05f; s.spinVariance = 0.1f;
        return s;
    }
    

    struct Particle
    {
        Vector3f position;
        Vector3f velocity;
        float age = 0.f;
        float lifetime = 1.f;
        float rotation = 0.f;
        float spinSpeed = 0.f;
        float seed = 0.f; //0..1 per particle randomness
        bool isAlive = false;
    };

    class ParticleEmitter
    {
    public:
        explicit ParticleEmitter(const ParticleEmitterSettings& aSettings);

        void Update(float aDeltaTime, const Vector3f& anEmittingPosition);

        void Burst(int count, const Vector3f& aWorldPos);

        void SetEmitting(bool aIsEmitting) {myIsEmitting = aIsEmitting;}
        bool GetEmitting() {return myIsEmitting;}
        size_t AliveCount() const;

        const std::vector<Particle>& GetParticles() const {return myParticles;}
        const ParticleEmitterSettings&  GetSettings()  const { return mySettings; }

        // Sample appearance at normalized age t in [0,1].
        Vector4f ColorAtAge(float t) const; // RGBA
        float    SizeAtAge(float t) const;

    private:
        void Spawn(const Vector3f& aWorldPos);
        
        ParticleEmitterSettings mySettings;
        std::vector<Particle> myParticles;
        float mySpawnAccumulator = 0.f;
        bool myIsEmitting = true;
    };

}
