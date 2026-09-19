#include "stdafx.h"
#include "ParticleSystem.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace Ag::Particles
{
	namespace
	{
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kDegToRad = kPi / 180.f;
		constexpr float kMaxStep = 0.1f;        // longest frame we simulate; a bigger hitch is clipped
		constexpr float kSubStep = 1.f / 30.f;  // longest single simulation step

		float Saturate(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

		Vector3f RandomUnitVector(Rng& aRng)
		{
			const float z = aRng.Range(-1.f, 1.f);
			const float phi = aRng.Range(0.f, 2.f * kPi);
			const float r = std::sqrt(std::max(0.f, 1.f - z * z));
			return Vector3f(r * std::cos(phi), z, r * std::sin(phi));
		}

		Vector3f Normalized(const Vector3f& v, const Vector3f& aFallback)
		{
			const float length = v.Length();
			return length > 1e-6f ? v * (1.f / length) : aFallback;
		}

		// ---- noise (for the curl force): hash based value noise, smooth and deterministic

		float Hash3(int x, int y, int z)
		{
			uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
			h = (h ^ (h >> 13)) * 1274126177u;
			h ^= h >> 16;
			return (float)(h & 0xFFFFFFu) / 8388608.f - 1.f;   // [-1, 1)
		}

		float Smooth(float t) { return t * t * (3.f - 2.f * t); }

		float Noise3(float x, float y, float z)
		{
			const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
			const int ix = (int)fx, iy = (int)fy, iz = (int)fz;
			const float tx = Smooth(x - fx), ty = Smooth(y - fy), tz = Smooth(z - fz);
			float corners[2][2][2];
			for (int dz = 0; dz < 2; ++dz)
				for (int dy = 0; dy < 2; ++dy)
					for (int dx = 0; dx < 2; ++dx)
						corners[dz][dy][dx] = Hash3(ix + dx, iy + dy, iz + dz);
			float along[2][2];
			for (int dz = 0; dz < 2; ++dz)
				for (int dy = 0; dy < 2; ++dy)
					along[dz][dy] = corners[dz][dy][0] + (corners[dz][dy][1] - corners[dz][dy][0]) * tx;
			float plane[2];
			for (int dz = 0; dz < 2; ++dz)
				plane[dz] = along[dz][0] + (along[dz][1] - along[dz][0]) * ty;
			return plane[0] + (plane[1] - plane[0]) * tz;
		}

		// Three independent noise fields sampled with fixed offsets.
		float Field(int aComponent, const Vector3f& p)
		{
			static const float offsets[3][3] = { { 0.f, 0.f, 0.f }, { 31.4f, 17.3f, 5.5f }, { -12.7f, 8.1f, 23.9f } };
			return Noise3(p.x + offsets[aComponent][0], p.y + offsets[aComponent][1], p.z + offsets[aComponent][2]);
		}

		Vector3f CurlNoise(const Vector3f& p)
		{
			constexpr float e = 0.1f;
			const auto d = [&](int aComponent, int anAxis)
			{
				Vector3f hi = p, lo = p;
				if (anAxis == 0) { hi.x += e; lo.x -= e; }
				else if (anAxis == 1) { hi.y += e; lo.y -= e; }
				else { hi.z += e; lo.z -= e; }
				return (Field(aComponent, hi) - Field(aComponent, lo)) / (2.f * e);
			};
			return Vector3f(d(2, 1) - d(1, 2), d(0, 2) - d(2, 0), d(1, 0) - d(0, 1));
		}

		// Reads a module's parameters, replacing bound ones by the current User Parameter value.
		class Reader
		{
		public:
			Reader(const ModuleInstance& aModule, const std::vector<UserParameter>& someUser) : myModule(aModule), myUser(someUser) {}

			float Float(size_t i) const
			{
				const ParamValue& p = Param(i);
				if (const UserParameter* user = Bound(p)) return user->value[0];
				return p.v[0];
			}
			bool Bool(size_t i) const { return Param(i).v[0] > 0.5f; }
			int Enum(size_t i) const { return (int)Param(i).v[0]; }
			Vector3f Vec3(size_t i) const
			{
				const ParamValue& p = Param(i);
				if (const UserParameter* user = Bound(p)) return Vector3f(user->value[0], user->value[1], user->value[2]);
				return Vector3f(p.v[0], p.v[1], p.v[2]);
			}
			Vector4f Color(size_t i) const
			{
				const ParamValue& p = Param(i);
				if (const UserParameter* user = Bound(p)) return Vector4f(user->value[0], user->value[1], user->value[2], user->value[3]);
				return Vector4f(p.v[0], p.v[1], p.v[2], p.v[3]);
			}
			float Range(size_t i, Rng& aRng) const
			{
				const ParamValue& p = Param(i);
				return aRng.Range(p.v[0], p.v[1]);
			}
			Vector3f Vec3Range(size_t i, Rng& aRng) const
			{
				const ParamValue& p = Param(i);
				return Vector3f(aRng.Range(p.v[0], p.v[3]), aRng.Range(p.v[1], p.v[4]), aRng.Range(p.v[2], p.v[5]));
			}
			const Curve& GetCurve(size_t i) const { return Param(i).curve; }

		private:
			const ParamValue& Param(size_t i) const
			{
				static const ParamValue kEmpty{};
				return i < myModule.params.size() ? myModule.params[i] : kEmpty;
			}
			const UserParameter* Bound(const ParamValue& p) const
			{
				if (p.binding.empty())
					return nullptr;
				for (const UserParameter& user : myUser)
					if (user.name == p.binding)
						return &user;
				return nullptr;
			}

			const ModuleInstance& myModule;
			const std::vector<UserParameter>& myUser;
		};
	}

	// ---------------------------------------------------------------- buffers

	size_t ParticleBuffers::Add()
	{
		position.push_back(Vector3f(0.f, 0.f, 0.f));
		velocity.push_back(Vector3f(0.f, 0.f, 0.f));
		force.push_back(Vector3f(0.f, 0.f, 0.f));
		color.push_back(Vector4f(1.f, 1.f, 1.f, 1.f));
		baseColor.push_back(Vector4f(1.f, 1.f, 1.f, 1.f));
		age.push_back(0.f);
		lifetime.push_back(1.f);
		size.push_back(1.f);
		baseSize.push_back(1.f);
		rotation.push_back(0.f);
		spin.push_back(0.f);
		random.push_back(0.f);
		frame.push_back(0.f);
		return age.size() - 1;
	}

	void ParticleBuffers::RemoveSwap(size_t anIndex)
	{
		const size_t last = age.size() - 1;
		const auto remove = [&](auto& aVector)
		{
			aVector[anIndex] = aVector[last];
			aVector.pop_back();
		};
		remove(position); remove(velocity); remove(force); remove(color); remove(baseColor);
		remove(age); remove(lifetime); remove(size); remove(baseSize);
		remove(rotation); remove(spin); remove(random); remove(frame);
	}

	void ParticleBuffers::Clear()
	{
		position.clear(); velocity.clear(); force.clear(); color.clear(); baseColor.clear();
		age.clear(); lifetime.clear(); size.clear(); baseSize.clear();
		rotation.clear(); spin.clear(); random.clear(); frame.clear();
	}

	// ---------------------------------------------------------------- system

	Vector3f SystemInstance::TransformPoint(const Matrix4x4f& m, const Vector3f& p)
	{
		return Vector3f(
			p.x * m(1, 1) + p.y * m(2, 1) + p.z * m(3, 1) + m(4, 1),
			p.x * m(1, 2) + p.y * m(2, 2) + p.z * m(3, 2) + m(4, 2),
			p.x * m(1, 3) + p.y * m(2, 3) + p.z * m(3, 3) + m(4, 3));
	}

	Vector3f SystemInstance::TransformVector(const Matrix4x4f& m, const Vector3f& v)
	{
		return Vector3f(
			v.x * m(1, 1) + v.y * m(2, 1) + v.z * m(3, 1),
			v.x * m(1, 2) + v.y * m(2, 2) + v.z * m(3, 2),
			v.x * m(1, 3) + v.y * m(2, 3) + v.z * m(3, 3));
	}

	SystemInstance::SystemInstance(const SystemAsset& anAsset)
	{
		SetAsset(anAsset);
	}

	void SystemInstance::SetAsset(const SystemAsset& anAsset)
	{
		myAsset = anAsset;
		mySeed = myAsset.seed != 0 ? myAsset.seed : (std::random_device{}() | 1u);

		// Keep values the game already set for parameters that still exist.
		std::vector<UserParameter> previous = std::move(myUser);
		myUser = myAsset.userParameters;
		for (UserParameter& user : myUser)
			for (const UserParameter& old : previous)
				if (old.name == user.name && old.type == user.type)
					for (int c = 0; c < 4; ++c) user.value[c] = old.value[c];

		Rebuild();
	}

	void SystemInstance::UpdateAsset(const SystemAsset& anAsset)
	{
		if (anAsset.emitters.size() != myEmitters.size())
		{
			SetAsset(anAsset);
			return;
		}
		myAsset = anAsset;
		myUser = myAsset.userParameters;
		for (size_t i = 0; i < myEmitters.size(); ++i)
		{
			const size_t updateModules = myAsset.emitters[i].emitterUpdate.size();
			myEmitters[i].accumulators.resize(updateModules, 0.f);
			myEmitters[i].burstFired.resize(updateModules, 0);
		}
	}

	void SystemInstance::Rebuild()
	{
		myEmitters.clear();
		myEmitters.resize(myAsset.emitters.size());
		for (size_t i = 0; i < myEmitters.size(); ++i)
		{
			const EmitterAsset& asset = myAsset.emitters[i];
			EmitterRuntime& runtime = myEmitters[i];
			runtime.rng = Rng((uint64_t)mySeed * 2654435761ULL + i * 7919ULL + 1ULL);
			runtime.delayLeft = asset.startDelay;
			runtime.loopAge = 0.f;
			runtime.spawning = true;
			runtime.accumulators.assign(asset.emitterUpdate.size(), 0.f);
			runtime.burstFired.assign(asset.emitterUpdate.size(), 0);
			runtime.hasLastPosition = false;
			runtime.particles.Clear();
		}
	}

	void SystemInstance::Activate()
	{
		myActive = true;
		for (size_t i = 0; i < myEmitters.size(); ++i)
		{
			EmitterRuntime& runtime = myEmitters[i];
			runtime.spawning = true;
			runtime.loopAge = 0.f;
			runtime.delayLeft = myAsset.emitters[i].startDelay;
			std::fill(runtime.burstFired.begin(), runtime.burstFired.end(), (char)0);
		}
	}

	void SystemInstance::Deactivate()
	{
		myActive = false;
		for (EmitterRuntime& runtime : myEmitters)
			runtime.spawning = false;
	}

	void SystemInstance::Reset()
	{
		Rebuild();
		myActive = true;
	}

	void SystemInstance::Burst(int aCount)
	{
		for (size_t i = 0; i < myEmitters.size(); ++i)
			if (myAsset.emitters[i].enabled)
				SpawnParticles(myAsset.emitters[i], myEmitters[i], aCount);
	}

	bool SystemInstance::IsComplete() const
	{
		if (myActive)
		{
			// A non-looping system that has played all of its emitters counts as done.
			for (const EmitterRuntime& runtime : myEmitters)
				if (runtime.spawning)
					return false;
		}
		return AliveCount() == 0;
	}

	size_t SystemInstance::AliveCount() const
	{
		size_t count = 0;
		for (const EmitterRuntime& runtime : myEmitters)
			count += runtime.particles.Count();
		return count;
	}

	UserParameter& SystemInstance::FindOrAddUser(const std::string& aName, ParamType aType)
	{
		for (UserParameter& user : myUser)
			if (user.name == aName)
				return user;
		UserParameter user;
		user.name = aName;
		user.type = aType;
		myUser.push_back(user);
		return myUser.back();
	}

	void SystemInstance::SetFloat(const std::string& aName, float aValue)
	{
		FindOrAddUser(aName, ParamType::Float).value[0] = aValue;
	}

	void SystemInstance::SetVector(const std::string& aName, const Vector3f& aValue)
	{
		UserParameter& user = FindOrAddUser(aName, ParamType::Vec3);
		user.value[0] = aValue.x; user.value[1] = aValue.y; user.value[2] = aValue.z;
	}

	void SystemInstance::SetColor(const std::string& aName, const Vector4f& aValue)
	{
		UserParameter& user = FindOrAddUser(aName, ParamType::Color);
		user.value[0] = aValue.x; user.value[1] = aValue.y; user.value[2] = aValue.z; user.value[3] = aValue.w;
	}

	void SystemInstance::Update(float aDeltaSeconds)
	{
		const float delta = std::min(aDeltaSeconds, kMaxStep);
		if (delta <= 0.f)
			return;

		// Several small steps keep fast particles and collisions stable when the frame is long.
		const int steps = std::max(1, (int)std::ceil(delta / kSubStep));
		const float step = delta / (float)steps;
		for (int s = 0; s < steps; ++s)
			for (size_t i = 0; i < myEmitters.size(); ++i)
				StepEmitter(i, step);
	}

	void SystemInstance::StepEmitter(size_t anEmitterIndex, float aDelta)
	{
		const EmitterAsset& asset = myAsset.emitters[anEmitterIndex];
		EmitterRuntime& runtime = myEmitters[anEmitterIndex];

		UpdateParticles(asset, runtime, aDelta);

		const Vector3f worldPosition(myTransform(4, 1), myTransform(4, 2), myTransform(4, 3));
		const float travelled = runtime.hasLastPosition ? (worldPosition - runtime.lastPosition).Length() : 0.f;
		runtime.lastPosition = worldPosition;
		runtime.hasLastPosition = true;

		if (!asset.enabled || !runtime.spawning)
			return;
		if (runtime.delayLeft > 0.f)
		{
			runtime.delayLeft -= aDelta;
			return;
		}

		runtime.loopAge += aDelta;

		int toSpawn = 0;
		for (size_t m = 0; m < asset.emitterUpdate.size(); ++m)
		{
			const ModuleInstance& module = asset.emitterUpdate[m];
			if (!module.enabled)
				continue;
			const Reader reader(module, myUser);
			switch (module.type)
			{
			case ModuleType::SpawnRate:
				runtime.accumulators[m] += reader.Float(0) * aDelta;
				break;
			case ModuleType::SpawnPerUnit:
				runtime.accumulators[m] += reader.Float(0) * travelled;
				break;
			case ModuleType::SpawnBurst:
				if (!runtime.burstFired[m] && runtime.loopAge >= reader.Float(0))
				{
					runtime.burstFired[m] = 1;
					toSpawn += std::max(0, (int)std::lround(reader.Range(1, runtime.rng)));
				}
				break;
			default:
				break;
			}
			const int whole = (int)std::floor(runtime.accumulators[m]);
			if (whole > 0)
			{
				toSpawn += whole;
				runtime.accumulators[m] -= (float)whole;
			}
		}
		SpawnParticles(asset, runtime, toSpawn);

		if (runtime.loopAge >= asset.loopDuration)
		{
			if (asset.looping)
			{
				runtime.loopAge -= asset.loopDuration;
				std::fill(runtime.burstFired.begin(), runtime.burstFired.end(), (char)0);
			}
			else
			{
				runtime.spawning = false;
			}
		}
	}

	void SystemInstance::UpdateParticles(const EmitterAsset& anAsset, EmitterRuntime& aRuntime, float aDelta)
	{
		ParticleBuffers& b = aRuntime.particles;
		const size_t count = b.Count();
		if (count == 0)
			return;

		std::fill(b.force.begin(), b.force.end(), Vector3f(0.f, 0.f, 0.f));

		// Forces first, so they act on this step's velocity.
		for (const ModuleInstance& module : anAsset.particleUpdate)
		{
			if (!module.enabled)
				continue;
			const Reader r(module, myUser);
			switch (module.type)
			{
			case ModuleType::GravityForce:
			{
				const Vector3f g = r.Vec3(0);
				for (size_t i = 0; i < count; ++i) b.force[i] += g;
				break;
			}
			case ModuleType::AccelerationForce:
			{
				const Vector3f a = r.Vec3(0);
				for (size_t i = 0; i < count; ++i) b.force[i] += a;
				break;
			}
			case ModuleType::DragForce:
			{
				// Never remove more than the whole velocity in one step.
				const float drag = std::min(r.Float(0), 1.f / aDelta);
				for (size_t i = 0; i < count; ++i) b.force[i] -= b.velocity[i] * drag;
				break;
			}
			case ModuleType::CurlNoiseForce:
			{
				const float strength = r.Float(0), frequency = r.Float(1);
				for (size_t i = 0; i < count; ++i) b.force[i] += CurlNoise(b.position[i] * frequency) * strength;
				break;
			}
			case ModuleType::VortexForce:
			{
				const Vector3f axis = Normalized(r.Vec3(0), Vector3f(0.f, 1.f, 0.f));
				const Vector3f center = r.Vec3(1);
				const float strength = r.Float(2), pull = r.Float(3);
				for (size_t i = 0; i < count; ++i)
				{
					const Vector3f offset = b.position[i] - center;
					const Vector3f radial = offset - axis * axis.Dot(offset);
					b.force[i] += Normalized(axis.Cross(offset), Vector3f(0.f, 0.f, 0.f)) * strength;
					b.force[i] -= Normalized(radial, Vector3f(0.f, 0.f, 0.f)) * pull;
				}
				break;
			}
			default:
				break;
			}
		}

		// Solve: integrate forces into velocity, velocity into position, and age the particle.
		for (size_t i = 0; i < count; ++i)
		{
			b.velocity[i] += b.force[i] * aDelta;
			b.position[i] += b.velocity[i] * aDelta;
			b.rotation[i] += b.spin[i] * aDelta;
			b.age[i] += aDelta;
		}

		// Modules that look at where the particle ended up or how old it now is.
		for (const ModuleInstance& module : anAsset.particleUpdate)
		{
			if (!module.enabled)
				continue;
			const Reader r(module, myUser);
			switch (module.type)
			{
			case ModuleType::ScaleColor:
			{
				const Curve& curve = r.GetCurve(0);
				for (size_t i = 0; i < count; ++i)
				{
					float out[4];
					curve.Evaluate(Saturate(b.age[i] / b.lifetime[i]), 4, out);
					b.color[i] = Vector4f(b.baseColor[i].x * out[0], b.baseColor[i].y * out[1], b.baseColor[i].z * out[2], b.baseColor[i].w * out[3]);
				}
				break;
			}
			case ModuleType::ScaleSize:
			{
				const Curve& curve = r.GetCurve(0);
				for (size_t i = 0; i < count; ++i)
				{
					float out[1];
					curve.Evaluate(Saturate(b.age[i] / b.lifetime[i]), 1, out);
					b.size[i] = b.baseSize[i] * out[0];
				}
				break;
			}
			case ModuleType::SubUVAnimation:
			{
				const float frames = (float)(anAsset.renderer.flipbookColumns * anAsset.renderer.flipbookRows);
				const int mode = r.Enum(0);
				const float fps = r.Float(1);
				for (size_t i = 0; i < count; ++i)
				{
					if (mode == 0) b.frame[i] = Saturate(b.age[i] / b.lifetime[i]) * frames;
					else if (mode == 1) b.frame[i] = b.random[i] * frames;
					else b.frame[i] = std::fmod(b.age[i] * fps, frames);
				}
				break;
			}
			case ModuleType::PlaneCollision:
			{
				const float height = r.Float(0), restitution = r.Float(1), friction = r.Float(2);
				const bool killOnHit = r.Bool(3);
				for (size_t i = 0; i < count; ++i)
				{
					if (b.position[i].y >= height || b.velocity[i].y >= 0.f)
						continue;
					b.position[i].y = height;
					if (killOnHit)
					{
						b.age[i] = b.lifetime[i];
						continue;
					}
					b.velocity[i].y = -b.velocity[i].y * restitution;
					b.velocity[i].x *= 1.f - friction;
					b.velocity[i].z *= 1.f - friction;
				}
				break;
			}
			case ModuleType::KillBelowHeight:
			{
				const float height = r.Float(0);
				for (size_t i = 0; i < count; ++i)
					if (b.position[i].y < height)
						b.age[i] = b.lifetime[i];
				break;
			}
			default:
				break;
			}
		}

		// Remove dead particles (walking backwards keeps the swap-removal valid).
		for (size_t i = b.Count(); i-- > 0;)
			if (b.age[i] >= b.lifetime[i])
				b.RemoveSwap(i);
	}

	void SystemInstance::SpawnParticles(const EmitterAsset& anAsset, EmitterRuntime& aRuntime, int aCount)
	{
		ParticleBuffers& b = aRuntime.particles;
		Rng& rng = aRuntime.rng;

		for (int n = 0; n < aCount; ++n)
		{
			if ((int)b.Count() >= anAsset.maxParticles)
				return;

			const size_t i = b.Add();
			b.random[i] = rng.Float01();

			for (const ModuleInstance& module : anAsset.particleSpawn)
			{
				if (!module.enabled)
					continue;
				const Reader r(module, myUser);
				switch (module.type)
				{
				case ModuleType::InitializeParticle:
				{
					b.lifetime[i] = std::max(0.01f, r.Range(0, rng));
					b.size[i] = b.baseSize[i] = r.Range(1, rng);
					b.color[i] = b.baseColor[i] = r.Color(2);
					b.rotation[i] = r.Range(3, rng) * kDegToRad;
					b.spin[i] = r.Range(4, rng) * kDegToRad;
					break;
				}
				case ModuleType::ShapeLocation:
				{
					const int shape = r.Enum(0);
					const Vector3f extents = r.Vec3(1);
					const bool surface = r.Bool(2);
					Vector3f p(0.f, 0.f, 0.f);
					if (shape == 1)   // sphere
					{
						const float radius = surface ? 1.f : std::cbrt(rng.Float01());
						p = RandomUnitVector(rng) * (extents.x * radius);
					}
					else if (shape == 2)   // box
					{
						p = Vector3f(rng.Range(-1.f, 1.f) * extents.x, rng.Range(-1.f, 1.f) * extents.y, rng.Range(-1.f, 1.f) * extents.z);
						if (surface)
						{
							const int axis = (int)(rng.Float01() * 3.f) % 3;
							const float sign = rng.Float01() < 0.5f ? -1.f : 1.f;
							if (axis == 0) p.x = sign * extents.x;
							else if (axis == 1) p.y = sign * extents.y;
							else p.z = sign * extents.z;
						}
					}
					else if (shape == 3 || shape == 4)   // disc or cylinder
					{
						const float angle = rng.Range(0.f, 2.f * kPi);
						const float radius = extents.x * (surface ? 1.f : std::sqrt(rng.Float01()));
						p = Vector3f(std::cos(angle) * radius, shape == 4 ? rng.Range(-1.f, 1.f) * extents.y : 0.f, std::sin(angle) * radius);
					}
					b.position[i] = p + r.Vec3(3);
					break;
				}
				case ModuleType::AddVelocity:
					b.velocity[i] += r.Vec3Range(0, rng);
					break;
				case ModuleType::AddVelocityInCone:
				{
					const Vector3f axis = Normalized(r.Vec3(0), Vector3f(0.f, 1.f, 0.f));
					const Vector3f helper = std::fabs(axis.y) < 0.99f ? Vector3f(0.f, 1.f, 0.f) : Vector3f(1.f, 0.f, 0.f);
					const Vector3f side = Normalized(axis.Cross(helper), Vector3f(1.f, 0.f, 0.f));
					const Vector3f up = axis.Cross(side);

					// Uniform over the cap of the sphere the cone cuts out.
					const float cosMax = std::cos(r.Float(1) * kDegToRad);
					const float z = rng.Range(cosMax, 1.f);
					const float phi = rng.Range(0.f, 2.f * kPi);
					const float ring = std::sqrt(std::max(0.f, 1.f - z * z));
					const Vector3f direction = side * (ring * std::cos(phi)) + up * (ring * std::sin(phi)) + axis * z;
					b.velocity[i] += direction * r.Range(2, rng);
					break;
				}
				case ModuleType::AddVelocityFromPoint:
				{
					const Vector3f away = Normalized(b.position[i] - r.Vec3(0), RandomUnitVector(rng));
					b.velocity[i] += away * r.Range(1, rng);
					break;
				}
				default:
					break;
				}
			}

			// Spawn modules work in the emitter's own space; a world-space emitter is placed where the system is.
			if (anAsset.space == SimulationSpace::World)
			{
				b.position[i] = TransformPoint(myTransform, b.position[i]);
				b.velocity[i] = TransformVector(myTransform, b.velocity[i]);
			}
		}
	}
}
