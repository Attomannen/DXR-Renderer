#include "stdafx.h"
#include "ParticleEmitter.h"
#include <random>

#include "tge/math/FMath.h"

using namespace Tga;

namespace
{
    std::mt19937& RandomGen()
    {
        static std::mt19937 gen{ std::random_device{}()};
        return gen;
    }

    float RandomRange(float min, float max)
    {
        std::uniform_real_distribution<float> distribution(min, max);
        return distribution(RandomGen());
    }
    constexpr float kPi = 3.14159265358979323846f;
}

ParticleEmitter::ParticleEmitter(const ParticleEmitterSettings& aSettings) : mySettings(aSettings)
{
    if (mySettings.maxParticles < 1)
    {
        mySettings.maxParticles = 1;
    }

    myParticles.resize(mySettings.maxParticles);
}

void ParticleEmitter::Update(float aDeltaTime, const Vector3f& aPosition)
{
    if (aDeltaTime <= 0)
    {
        return;
    }

    const Vector3f acceleration = mySettings.acceleration;
    const float dragFactoor = std::max(0.f, 1.f - mySettings.drag * aDeltaTime);

    for (Particle& particle : myParticles)
    {
        if (!particle.isAlive)
        {
            continue;
        }

        particle.age += aDeltaTime;
        if (particle.age >= particle.lifetime)
        {
            particle.isAlive = false;
            continue;
        }

        particle.velocity += acceleration * aDeltaTime;

        if (mySettings.drag > 0.f)
        {
            particle.velocity = particle.velocity * dragFactoor;
        }

        particle.position += particle.velocity * aDeltaTime;
        particle.rotation += particle.spinSpeed * aDeltaTime;
        
    }

    //continuous emissions
    if (myIsEmitting && mySettings.spawnRate > 0.f)
    {
        mySpawnAccumulator += mySettings.spawnRate * aDeltaTime;

        // cap per-frame spawns so a big hitch can't spawn thousands at once
        int budget = mySettings.maxParticles;
        while (mySpawnAccumulator >= 1.f && budget-- > 0)
        {
            Spawn(aPosition);
            mySpawnAccumulator -= 1.f;
        }
    }
}

void ParticleEmitter::Burst(int count, const Vector3f& aWorldPos)
{
    for (int i = 0; i < count; ++i)
    {
        Spawn(aWorldPos);
    }
}

Vector4f ParticleEmitter::ColorAtAge(float t) const
{
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    if (t < 0.5f)
    {
        return FMath::Lerp(mySettings.startColor, mySettings.midColor, t * 2.f);
    }
    return FMath::Lerp(mySettings.midColor, mySettings.endColor, (t - 0.5f) * 2.f);
}

float ParticleEmitter::SizeAtAge(float t) const
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return mySettings.startSize + (mySettings.endSize - mySettings.startSize) * t;
}

size_t ParticleEmitter::AliveCount() const
{
    size_t n = 0;
    for (const Particle& p : myParticles)
    {
        if (p.isAlive)
        {
            ++n;
        }
    }
    return n;
}

void ParticleEmitter::Spawn(const Vector3f& aWorldPos)
{
    //find free spot
    Particle* slot = nullptr;
    for (Particle& particle : myParticles)
    {
        if (!particle.isAlive)
        {
            slot = &particle;
            break;
        }
    }
    if (!slot)
    {
        return;
    }

    Particle& particle = *slot;
    particle.isAlive = true;
    particle.age = 0.f;
    particle.lifetime = std::max(0.01f, mySettings.lifetime + RandomRange(-1.f, 1.f) * mySettings.lifetimeVariance);
    particle.position = aWorldPos;
    if (mySettings.spawnRadius > 0.0f)
    {
        const float a = RandomRange(0.0f, 2.0f * kPi);
        const float r = RandomRange(0.0f, mySettings.spawnRadius);
        particle.position.x += std::cos(a) * r;
        particle.position.z += std::sin(a) * r;  // spread in the ground plane; smoke rises in Y
    }
    particle.rotation = mySettings.startSpin + RandomRange(-1.f, 1.f) * mySettings.spinVariance;
    particle.seed = RandomRange(0.f, 1.f);

    // velocity: pick a direction inside a cone around startVelocity
    const Vector3f base = mySettings.startVelocity;
    const float speed = base.Length();
    const Vector3f axis = speed > 1e-5f ? base * (1.f / speed) : Vector3f(0.f, 1.f, 0.f);

    // orthonormal basis around the cone axis
    const Vector3f helper = std::fabs(axis.y) < 0.99f ? Vector3f(0.0f, 1.0f, 0.0f) : Vector3f(1.0f, 0.0f, 0.0f);
    const Vector3f side = axis.Cross(helper);
    const Vector3f up = axis.Cross(side);

    //uniform sample within cone half angle
    const float coneRadians = mySettings.spawnConeAngle * (kPi / 180.f);
    const float cosMax = std::cos(coneRadians);
    const float z = RandomRange(cosMax, 1.f);
    const float phi = RandomRange(0.f, 2.f * kPi);
    const float r = std::sqrt(std::max(0.f, 1.f - z * z));

    const Vector3f dir = side * (r * std::cos(phi)) + up * (r * std::sin(phi)) + axis * z;
    const float s = speed + RandomRange(-1.f, 1.f) * mySettings.velocityVariance;

    particle.velocity = dir * s;
}






