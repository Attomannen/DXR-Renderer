#pragma once

#include <age/particles/ParticleAsset.h>

#include <age/math/Matrix4x4.h>
#include <age/math/Vector3.h>
#include <age/math/Vector4.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Ag::Particles
{
	// Small deterministic PCG32 generator: the same seed always gives the same effect.
	struct Rng
	{
		uint64_t state = 0x853c49e6748fea9bULL;

		explicit Rng(uint64_t aSeed = 1) { state = aSeed * 6364136223846793005ULL + 1442695040888963407ULL; Next(); }

		uint32_t Next()
		{
			const uint64_t old = state;
			state = old * 6364136223846793005ULL + 1442695040888963407ULL;
			const uint32_t xorShifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
			const uint32_t rotation = (uint32_t)(old >> 59u);
			return (xorShifted >> rotation) | (xorShifted << ((32u - rotation) & 31u));
		}
		float Float01() { return (float)(Next() >> 8) * (1.f / 16777216.f); }
		float Range(float aMin, float aMax) { return aMin + (aMax - aMin) * Float01(); }
	};

	// Structure-of-arrays storage: every attribute of every live particle. Particles are removed by
	// swapping the last one into the gap, so there are never holes.
	struct ParticleBuffers
	{
		std::vector<Vector3f> position;
		std::vector<Vector3f> velocity;
		std::vector<Vector3f> force;      // accumulated by the force modules each step, then integrated
		std::vector<Vector4f> color;
		std::vector<Vector4f> baseColor;  // colour at spawn, before Scale Color
		std::vector<float> age;
		std::vector<float> lifetime;
		std::vector<float> size;
		std::vector<float> baseSize;      // size at spawn, before Scale Sprite Size
		std::vector<float> rotation;      // radians
		std::vector<float> spin;          // radians per second
		std::vector<float> random;        // 0..1, fixed for the particle's life
		std::vector<float> frame;         // flipbook frame

		size_t Count() const { return age.size(); }
		size_t Add();
		void RemoveSwap(size_t anIndex);
		void Clear();
	};

	struct EmitterRuntime
	{
		ParticleBuffers particles;
		Rng rng;
		float delayLeft = 0.f;
		float loopAge = 0.f;
		bool spawning = true;
		std::vector<float> accumulators;   // one per Emitter Update module (fractional particles carried over)
		std::vector<char> burstFired;      // one per Emitter Update module
		Vector3f lastPosition;
		bool hasLastPosition = false;
	};

	class SystemInstance
	{
	public:
		explicit SystemInstance(const SystemAsset& anAsset);

		// Replaces the asset (the editor calls this after every edit). Running particles are cleared.
		void SetAsset(const SystemAsset& anAsset);
		// Swaps in an edited asset without killing the particles that are alive, so an editor can tweak values while
		// the effect keeps playing. Restarts only when the emitters themselves were added or removed.
		void UpdateAsset(const SystemAsset& anAsset);
		const SystemAsset& GetAsset() const { return myAsset; }

		void Update(float aDeltaSeconds);

		// Where the system is in the world. World-space emitters spawn there; Local-space emitters move with it.
		void SetTransform(const Matrix4x4f& aTransform) { myTransform = aTransform; }
		const Matrix4x4f& GetTransform() const { return myTransform; }

		// Activate starts spawning again (optionally from scratch); Deactivate stops spawning but lets live
		// particles finish, like Niagara's Deactivate. Reset kills everything and restarts.
		void Activate();
		void Deactivate();
		void Reset();
		// Spawns Count particles from every enabled emitter right now, on top of what the emitters spawn on their own.
		void Burst(int aCount);
		bool IsActive() const { return myActive; }
		// Not active and nothing left alive.
		bool IsComplete() const;
		size_t AliveCount() const;

		// User Parameters: modules whose parameter is bound to this name read the value every step.
		void SetFloat(const std::string& aName, float aValue);
		void SetVector(const std::string& aName, const Vector3f& aValue);
		void SetColor(const std::string& aName, const Vector4f& aValue);

		size_t EmitterCount() const { return myEmitters.size(); }
		const EmitterRuntime& GetEmitter(size_t anIndex) const { return myEmitters[anIndex]; }

		static Vector3f TransformPoint(const Matrix4x4f& aMatrix, const Vector3f& aPoint);
		static Vector3f TransformVector(const Matrix4x4f& aMatrix, const Vector3f& aVector);

	private:
		void Rebuild();
		void StepEmitter(size_t anEmitterIndex, float aDelta);
		void UpdateParticles(const EmitterAsset& anAsset, EmitterRuntime& aRuntime, float aDelta);
		void SpawnParticles(const EmitterAsset& anAsset, EmitterRuntime& aRuntime, int aCount);
		UserParameter& FindOrAddUser(const std::string& aName, ParamType aType);

		SystemAsset myAsset;
		std::vector<UserParameter> myUser;
		std::vector<EmitterRuntime> myEmitters;
		Matrix4x4f myTransform;
		uint32_t mySeed = 1;
		bool myActive = true;
	};
}
