#include "PhysicsWorld.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

namespace Ag
{
	const char* GetPhysicsVersion()
	{
		static char version[32] = {};
		if (!version[0])
			snprintf(version, sizeof(version), "%d.%d.%d", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH);
		return version;
	}

	namespace
	{
		constexpr float kCmToM = 0.01f;
		constexpr float kMToCm = 100.f;

		namespace Layers
		{
			constexpr JPH::ObjectLayer Static = 0;
			constexpr JPH::ObjectLayer Moving = 1;
		}

		namespace BroadLayers
		{
			constexpr JPH::BroadPhaseLayer Static(0);
			constexpr JPH::BroadPhaseLayer Moving(1);
			constexpr JPH::uint Count = 2;
		}

		class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
		{
		public:
			JPH::uint GetNumBroadPhaseLayers() const override { return BroadLayers::Count; }
			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
			{
				return layer == Layers::Static ? BroadLayers::Static : BroadLayers::Moving;
			}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
			{
				return layer == BroadLayers::Static ? "STATIC" : "MOVING";
			}
#endif
		};

		class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override
			{
				// Static things never need to collide with each other.
				return layer == Layers::Moving || broad == BroadLayers::Moving;
			}
		};

		class ObjectPairs final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
			{
				return a == Layers::Moving || b == Layers::Moving;
			}
		};

		// Called by Jolt from its job threads whenever two bodies start touching.
		class ContactCollector final : public JPH::ContactListener
		{
		public:
			void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&, JPH::ContactSettings&) override
			{
				PhysicsContactEvent event;
				event.userDataA = a.GetUserData();
				event.userDataB = b.GetUserData();
				event.trigger = a.IsSensor() || b.IsSensor();
				if (event.userDataA == 0 && event.userDataB == 0)
					return;
				std::lock_guard<std::mutex> lock(myMutex);
				myEvents.push_back(event);
			}

			void Take(std::vector<PhysicsContactEvent>& out)
			{
				std::lock_guard<std::mutex> lock(myMutex);
				out.insert(out.end(), myEvents.begin(), myEvents.end());
				myEvents.clear();
			}

		private:
			std::mutex myMutex;
			std::vector<PhysicsContactEvent> myEvents;
		};

		int gJoltRefCount = 0;

		void AcquireJolt()
		{
			if (gJoltRefCount == 0)
			{
				JPH::RegisterDefaultAllocator();
				JPH::Factory::sInstance = new JPH::Factory();
				JPH::RegisterTypes();
			}
			++gJoltRefCount;
		}

		void ReleaseJolt()
		{
			if (--gJoltRefCount == 0)
			{
				JPH::UnregisterTypes();
				delete JPH::Factory::sInstance;
				JPH::Factory::sInstance = nullptr;
			}
		}

		JPH::Vec3 ToJolt(const PhysicsVec3& v) { return JPH::Vec3(v.x * kCmToM, v.y * kCmToM, v.z * kCmToM); }
		PhysicsVec3 ToEngine(JPH::Vec3Arg v) { return { v.GetX() * kMToCm, v.GetY() * kMToCm, v.GetZ() * kMToCm }; }
		JPH::Quat ToJolt(const PhysicsQuat& q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
		PhysicsQuat ToEngine(JPH::QuatArg q) { return { q.GetX(), q.GetY(), q.GetZ(), q.GetW() }; }

		JPH::Vec3 ReadPosition(const PhysicsMeshData& mesh, size_t i, const PhysicsVec3& scale)
		{
			const float* p = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(mesh.positions) + i * mesh.positionStrideBytes);
			return JPH::Vec3(p[0] * scale.x * kCmToM, p[1] * scale.y * kCmToM, p[2] * scale.z * kCmToM);
		}

		JPH::Ref<JPH::Shape> BuildShape(const PhysicsShapeDesc& d)
		{
			JPH::ShapeSettings::ShapeResult result;
			switch (d.type)
			{
			case PhysicsShapeType::Box:
			{
				const JPH::Vec3 half = ToJolt(d.halfExtents);
				const float minHalf = std::min({ half.GetX(), half.GetY(), half.GetZ() });
				if (minHalf <= 0.f)
					return nullptr;
				// Box asserts half extent >= convex radius, so shrink the radius for thin boxes.
				JPH::BoxShapeSettings settings(half, std::min(JPH::cDefaultConvexRadius, minHalf));
				result = settings.Create();
				break;
			}
			case PhysicsShapeType::Sphere:
			{
				if (d.radius <= 0.f)
					return nullptr;
				JPH::SphereShapeSettings settings(d.radius * kCmToM);
				result = settings.Create();
				break;
			}
			case PhysicsShapeType::Capsule:
			{
				if (d.radius <= 0.f || d.halfHeight <= 0.f)
					return nullptr;
				JPH::CapsuleShapeSettings settings(d.halfHeight * kCmToM, d.radius * kCmToM);
				result = settings.Create();
				break;
			}
			case PhysicsShapeType::ConvexHull:
			{
				if (!d.mesh.positions || d.mesh.vertexCount < 4)
					return nullptr;
				JPH::Array<JPH::Vec3> points;
				points.reserve(d.mesh.vertexCount);
				for (size_t i = 0; i < d.mesh.vertexCount; ++i)
					points.push_back(ReadPosition(d.mesh, i, d.meshScale));
				JPH::ConvexHullShapeSettings settings(points);
				result = settings.Create();
				break;
			}
			case PhysicsShapeType::TriangleMesh:
			{
				if (!d.mesh.positions || !d.mesh.indices || d.mesh.vertexCount < 3 || d.mesh.indexCount < 3)
					return nullptr;
				JPH::VertexList vertices;
				vertices.reserve(d.mesh.vertexCount);
				for (size_t i = 0; i < d.mesh.vertexCount; ++i)
				{
					const JPH::Vec3 v = ReadPosition(d.mesh, i, d.meshScale);
					vertices.push_back(JPH::Float3(v.GetX(), v.GetY(), v.GetZ()));
				}
				JPH::IndexedTriangleList triangles;
				triangles.reserve(d.mesh.indexCount / 3);
				for (size_t i = 0; i + 2 < d.mesh.indexCount; i += 3)
				{
					const uint32_t a = d.mesh.indices[i], b = d.mesh.indices[i + 1], c = d.mesh.indices[i + 2];
					if (a >= d.mesh.vertexCount || b >= d.mesh.vertexCount || c >= d.mesh.vertexCount)
						return nullptr;
					triangles.push_back(JPH::IndexedTriangle(a, b, c));
				}
				JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
				result = settings.Create();
				break;
			}
			}

			if (result.HasError())
				return nullptr;

			JPH::Ref<JPH::Shape> shape = result.Get();
			if (d.offset.x != 0.f || d.offset.y != 0.f || d.offset.z != 0.f)
			{
				JPH::RotatedTranslatedShapeSettings offsetSettings(ToJolt(d.offset), JPH::Quat::sIdentity(), shape);
				JPH::ShapeSettings::ShapeResult offsetResult = offsetSettings.Create();
				if (offsetResult.HasError())
					return nullptr;
				shape = offsetResult.Get();
			}
			return shape;
		}
	}

	struct PhysicsWorld::Impl
	{
		BroadPhaseLayers broadPhaseLayers;
		ObjectVsBroadPhase objectVsBroadPhase;
		ObjectPairs objectPairs;
		ContactCollector contacts;

		std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
		std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
		std::unique_ptr<JPH::PhysicsSystem> system;

		std::vector<JPH::RefConst<JPH::Shape>> shapes; // released slots are null
		std::vector<uint32_t> freeShapeSlots;

		// Declared after `system`, so characters are released before it.
		struct Character
		{
			JPH::Ref<JPH::CharacterVirtual> character;
			JPH::Vec3 horizontal = JPH::Vec3::sZero(); // wanted sideways velocity, m/s
			float vertical = 0.f;                       // up velocity, m/s
			float stepHeight = 0.4f;                    // m
		};
		std::vector<Character> characters;              // destroyed slots have a null character

		void UpdateCharacters(float dt)
		{
			const JPH::Vec3 gravity = system->GetGravity();
			for (Character& c : characters)
			{
				if (!c.character)
					continue;

				const bool grounded = c.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
				if (grounded && c.vertical < 0.f)
					c.vertical = 0.f;                   // standing: no build-up of fall speed
				else
					c.vertical += gravity.GetY() * dt;

				c.character->SetLinearVelocity(c.horizontal + JPH::Vec3(0.f, c.vertical, 0.f));

				JPH::CharacterVirtual::ExtendedUpdateSettings settings;
				settings.mStickToFloorStepDown = JPH::Vec3(0.f, -c.stepHeight, 0.f);
				settings.mWalkStairsStepUp = JPH::Vec3(0.f, c.stepHeight, 0.f);

				c.character->ExtendedUpdate(dt, gravity, settings,
					JPH::DefaultBroadPhaseLayerFilter(objectVsBroadPhase, Layers::Moving),
					JPH::DefaultObjectLayerFilter(objectPairs, Layers::Moving),
					JPH::BodyFilter(), JPH::ShapeFilter(), *tempAllocator);

				// Up speed after the sweep: a ceiling hit zeroes it, ground contact resets it.
				c.vertical = c.character->GetLinearVelocity().GetY();
			}
		}

		float accumulator = 0.f;

		JPH::BodyInterface& Bodies() { return system->GetBodyInterface(); }
		const JPH::BodyInterface& Bodies() const { return system->GetBodyInterface(); }
	};

	PhysicsWorld::PhysicsWorld() = default;

	PhysicsWorld::~PhysicsWorld()
	{
		Shutdown();
	}

	bool PhysicsWorld::Init()
	{
		if (myImpl)
			return true;

		AcquireJolt();
		myImpl = std::make_unique<Impl>();
		Impl& s = *myImpl;

		const unsigned hardware = std::thread::hardware_concurrency();
		const int workers = static_cast<int>(hardware > 1 ? hardware - 1 : 1u);

		s.tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16u * 1024u * 1024u);
		s.jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workers);
		s.system = std::make_unique<JPH::PhysicsSystem>();

		constexpr JPH::uint maxBodies = 65536;
		constexpr JPH::uint maxBodyPairs = 65536;
		constexpr JPH::uint maxContactConstraints = 20480;
		s.system->Init(maxBodies, 0, maxBodyPairs, maxContactConstraints, s.broadPhaseLayers, s.objectVsBroadPhase, s.objectPairs);
		s.system->SetGravity(JPH::Vec3(0.f, -9.81f, 0.f));
		s.system->SetContactListener(&s.contacts);
		return true;
	}

	void PhysicsWorld::Shutdown()
	{
		if (!myImpl)
			return;

		// Bodies and shapes go with the system, and all of it before Jolt's global teardown.
		myImpl.reset();
		ReleaseJolt();
	}

	bool PhysicsWorld::IsInitialized() const
	{
		return myImpl != nullptr;
	}

	int PhysicsWorld::Update(float deltaSeconds)
	{
		if (!myImpl)
			return 0;
		Impl& s = *myImpl;

		s.accumulator += std::max(0.f, deltaSeconds);
		int steps = 0;
		while (s.accumulator >= kFixedStep && steps < kMaxSubSteps)
		{
			s.UpdateCharacters(kFixedStep);
			s.system->Update(kFixedStep, 1, s.tempAllocator.get(), s.jobSystem.get());
			s.accumulator -= kFixedStep;
			++steps;
		}
		if (s.accumulator >= kFixedStep)
			s.accumulator = 0.f; // hit the step cap: drop the backlog rather than spiral
		return steps;
	}

	void PhysicsWorld::SetGravity(const PhysicsVec3& gravity)
	{
		if (myImpl)
			myImpl->system->SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
	}

	PhysicsShapeId PhysicsWorld::CreateShape(const PhysicsShapeDesc& desc)
	{
		if (!myImpl)
			return {};
		Impl& s = *myImpl;

		JPH::Ref<JPH::Shape> shape = BuildShape(desc);
		if (!shape)
			return {};

		uint32_t slot;
		if (!s.freeShapeSlots.empty())
		{
			slot = s.freeShapeSlots.back();
			s.freeShapeSlots.pop_back();
			s.shapes[slot] = shape;
		}
		else
		{
			slot = static_cast<uint32_t>(s.shapes.size());
			s.shapes.push_back(shape);
		}
		return { slot };
	}

	void PhysicsWorld::ReleaseShape(PhysicsShapeId shape)
	{
		if (!myImpl || !shape.IsValid() || shape.value >= myImpl->shapes.size() || !myImpl->shapes[shape.value])
			return;
		// Bodies that still use it keep it alive through their own reference.
		myImpl->shapes[shape.value] = nullptr;
		myImpl->freeShapeSlots.push_back(shape.value);
	}

	PhysicsBodyId PhysicsWorld::CreateBody(const PhysicsBodyDesc& desc)
	{
		if (!myImpl || !desc.shape.IsValid() || desc.shape.value >= myImpl->shapes.size())
			return {};
		Impl& s = *myImpl;

		const JPH::RefConst<JPH::Shape>& shape = s.shapes[desc.shape.value];
		if (!shape)
			return {};
		if (desc.motion == PhysicsMotion::Dynamic && shape->GetType() == JPH::EShapeType::Mesh)
			return {}; // a triangle mesh has no volume, so it cannot be simulated

		const JPH::EMotionType motion =
			desc.motion == PhysicsMotion::Static ? JPH::EMotionType::Static :
			desc.motion == PhysicsMotion::Kinematic ? JPH::EMotionType::Kinematic :
			JPH::EMotionType::Dynamic;
		const JPH::ObjectLayer layer = desc.motion == PhysicsMotion::Static ? Layers::Static : Layers::Moving;

		JPH::BodyCreationSettings settings(shape, JPH::RVec3(ToJolt(desc.position)), ToJolt(desc.rotation), motion, layer);
		settings.mFriction = desc.friction;
		settings.mRestitution = desc.restitution;
		settings.mGravityFactor = desc.gravityFactor;
		settings.mLinearDamping = desc.linearDamping;
		settings.mAngularDamping = desc.angularDamping;
		settings.mUserData = desc.userData;
		settings.mIsSensor = desc.isSensor;
		if (desc.motion == PhysicsMotion::Dynamic && desc.mass > 0.f)
		{
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			settings.mMassPropertiesOverride.mMass = desc.mass;
		}

		const JPH::BodyID id = s.Bodies().CreateAndAddBody(
			settings, desc.motion == PhysicsMotion::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
		if (id.IsInvalid())
			return {};
		return { id.GetIndexAndSequenceNumber() };
	}

	void PhysicsWorld::DestroyBody(PhysicsBodyId body)
	{
		if (!myImpl || !body.IsValid())
			return;
		JPH::BodyInterface& bodies = myImpl->Bodies();
		const JPH::BodyID id(body.value);
		bodies.RemoveBody(id);
		bodies.DestroyBody(id);
	}

	void PhysicsWorld::OptimizeBroadPhase()
	{
		if (myImpl)
			myImpl->system->OptimizeBroadPhase();
	}

	bool PhysicsWorld::GetTransform(PhysicsBodyId body, PhysicsVec3& outPosition, PhysicsQuat& outRotation) const
	{
		if (!myImpl || !body.IsValid())
			return false;
		JPH::RVec3 position;
		JPH::Quat rotation;
		myImpl->Bodies().GetPositionAndRotation(JPH::BodyID(body.value), position, rotation);
		outPosition = ToEngine(JPH::Vec3(position));
		outRotation = ToEngine(rotation);
		return true;
	}

	void PhysicsWorld::SetTransform(PhysicsBodyId body, const PhysicsVec3& position, const PhysicsQuat& rotation)
	{
		if (!myImpl || !body.IsValid())
			return;
		myImpl->Bodies().SetPositionAndRotation(JPH::BodyID(body.value), JPH::RVec3(ToJolt(position)), ToJolt(rotation), JPH::EActivation::Activate);
	}

	void PhysicsWorld::MoveKinematic(PhysicsBodyId body, const PhysicsVec3& position, const PhysicsQuat& rotation, float deltaSeconds)
	{
		if (!myImpl || !body.IsValid())
			return;
		myImpl->Bodies().MoveKinematic(JPH::BodyID(body.value), JPH::RVec3(ToJolt(position)), ToJolt(rotation), std::max(deltaSeconds, 1e-4f));
	}

	bool PhysicsWorld::IsAwake(PhysicsBodyId body) const
	{
		return myImpl && body.IsValid() && myImpl->Bodies().IsActive(JPH::BodyID(body.value));
	}

	uint64_t PhysicsWorld::GetUserData(PhysicsBodyId body) const
	{
		if (!myImpl || !body.IsValid())
			return 0;
		return myImpl->Bodies().GetUserData(JPH::BodyID(body.value));
	}

	void PhysicsWorld::AddForce(PhysicsBodyId body, const PhysicsVec3& force)
	{
		// Newtons (kg * m/s^2): the vector is not a length, so it is not scaled.
		if (myImpl && body.IsValid())
			myImpl->Bodies().AddForce(JPH::BodyID(body.value), JPH::Vec3(force.x, force.y, force.z));
	}

	void PhysicsWorld::AddImpulse(PhysicsBodyId body, const PhysicsVec3& impulse)
	{
		if (myImpl && body.IsValid())
			myImpl->Bodies().AddImpulse(JPH::BodyID(body.value), JPH::Vec3(impulse.x, impulse.y, impulse.z));
	}

	void PhysicsWorld::SetLinearVelocity(PhysicsBodyId body, const PhysicsVec3& velocity)
	{
		if (myImpl && body.IsValid())
			myImpl->Bodies().SetLinearVelocity(JPH::BodyID(body.value), ToJolt(velocity));
	}

	PhysicsVec3 PhysicsWorld::GetLinearVelocity(PhysicsBodyId body) const
	{
		if (!myImpl || !body.IsValid())
			return {};
		return ToEngine(myImpl->Bodies().GetLinearVelocity(JPH::BodyID(body.value)));
	}

	void PhysicsWorld::TakeContactEvents(std::vector<PhysicsContactEvent>& out)
	{
		if (myImpl)
			myImpl->contacts.Take(out);
	}

	uint32_t PhysicsWorld::GetBodyCount() const
	{
		return myImpl ? myImpl->system->GetNumBodies() : 0;
	}

	PhysicsCharacterId PhysicsWorld::CreateCharacter(const PhysicsCharacterDesc& desc)
	{
		if (!myImpl)
			return {};
		Impl& s = *myImpl;

		const float radius = std::max(desc.radius, 1.f) * kCmToM;
		const float height = std::max(desc.height, desc.radius * 2.f + 1.f) * kCmToM;
		const float cylinderHalf = (height - 2.f * radius) * 0.5f;

		// The capsule is lifted so the character's position is at its feet.
		JPH::RotatedTranslatedShapeSettings shapeSettings(JPH::Vec3(0.f, cylinderHalf + radius, 0.f), JPH::Quat::sIdentity(),
			new JPH::CapsuleShape(cylinderHalf, radius));
		const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
		if (shape.HasError())
			return {};

		JPH::CharacterVirtualSettings settings;
		settings.mShape = shape.Get();
		// A body of the same shape follows the character, so triggers and other bodies can see it.
		settings.mInnerBodyShape = shape.Get();
		settings.mInnerBodyLayer = Layers::Moving;
		settings.mUp = JPH::Vec3::sAxisY();
		settings.mMass = desc.mass;
		settings.mMaxSlopeAngle = JPH::DegreesToRadians(desc.maxSlopeDegrees);
		// Only the lower part of the capsule counts as ground, so a wall against the side never supports it.
		settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);

		Impl::Character character;
		character.character = new JPH::CharacterVirtual(&settings, JPH::RVec3(ToJolt(desc.position)), JPH::Quat::sIdentity(), desc.userData, s.system.get());
		character.stepHeight = desc.stepHeight * kCmToM;

		for (size_t i = 0; i < s.characters.size(); ++i)
		{
			if (!s.characters[i].character)
			{
				s.characters[i] = std::move(character);
				return { (uint32_t)i };
			}
		}
		s.characters.push_back(std::move(character));
		return { (uint32_t)(s.characters.size() - 1) };
	}

	void PhysicsWorld::DestroyCharacter(PhysicsCharacterId character)
	{
		if (myImpl && character.IsValid() && character.value < myImpl->characters.size())
			myImpl->characters[character.value] = {};
	}

	void PhysicsWorld::SetCharacterMove(PhysicsCharacterId character, const PhysicsVec3& velocity)
	{
		if (myImpl && character.IsValid() && character.value < myImpl->characters.size())
			myImpl->characters[character.value].horizontal = JPH::Vec3(velocity.x * kCmToM, 0.f, velocity.z * kCmToM);
	}

	void PhysicsWorld::CharacterJump(PhysicsCharacterId character, float speed)
	{
		if (!myImpl || !character.IsValid() || character.value >= myImpl->characters.size())
			return;
		Impl::Character& c = myImpl->characters[character.value];
		if (c.character && c.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround)
			c.vertical = speed * kCmToM;
	}

	void PhysicsWorld::SetCharacterPosition(PhysicsCharacterId character, const PhysicsVec3& feet)
	{
		if (myImpl && character.IsValid() && character.value < myImpl->characters.size() && myImpl->characters[character.value].character)
		{
			myImpl->characters[character.value].character->SetPosition(JPH::RVec3(ToJolt(feet)));
			myImpl->characters[character.value].vertical = 0.f;
		}
	}

	bool PhysicsWorld::GetCharacterPosition(PhysicsCharacterId character, PhysicsVec3& outFeet) const
	{
		if (!myImpl || !character.IsValid() || character.value >= myImpl->characters.size() || !myImpl->characters[character.value].character)
			return false;
		outFeet = ToEngine(JPH::Vec3(myImpl->characters[character.value].character->GetPosition()));
		return true;
	}

	bool PhysicsWorld::IsCharacterOnGround(PhysicsCharacterId character) const
	{
		if (!myImpl || !character.IsValid() || character.value >= myImpl->characters.size() || !myImpl->characters[character.value].character)
			return false;
		return myImpl->characters[character.value].character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
	}

	void PhysicsWorld::CollectDebugLines(const PhysicsVec3& center, float radius, size_t maxLines, PhysicsDebugLines& out) const
	{
		if (!myImpl)
			return;
		const Impl& s = *myImpl;

		const JPH::Vec3 c = ToJolt(center);
		const float r = radius * kCmToM;
		const JPH::AABox region(c - JPH::Vec3::sReplicate(r), c + JPH::Vec3::sReplicate(r));

		// Everything to draw: the bodies, then the characters (which are not bodies).
		struct Item { JPH::TransformedShape shape; PhysicsDebugLines::Kind kind; };
		std::vector<Item> items;

		JPH::BodyIDVector ids;
		s.system->GetBodies(ids);
		for (const JPH::BodyID& id : ids)
		{
			PhysicsDebugLines::Kind kind = PhysicsDebugLines::Static;
			if (s.Bodies().GetMotionType(id) != JPH::EMotionType::Static)
				kind = s.Bodies().IsActive(id) ? PhysicsDebugLines::Awake : PhysicsDebugLines::Asleep;
			items.push_back({ s.system->GetBodyInterface().GetTransformedShape(id), kind });
		}
		for (const Impl::Character& character : s.characters)
			if (character.character)
				items.push_back({ character.character->GetTransformedShape(), PhysicsDebugLines::Awake });

		for (const Item& item : items)
		{
			const JPH::TransformedShape& shape = item.shape;
			const PhysicsDebugLines::Kind kind = item.kind;
			if (!shape.mShape || !shape.GetWorldSpaceBounds().Overlaps(region))
				continue;

			JPH::Shape::GetTrianglesContext context;
			shape.GetTrianglesStart(context, region, JPH::RVec3(c));

			constexpr int kBatch = 64;
			JPH::Float3 triangles[kBatch * 3];
			for (;;)
			{
				const int count = shape.GetTrianglesNext(context, kBatch, triangles);
				if (count == 0)
					break;
				for (int t = 0; t < count; ++t)
				{
					for (int e = 0; e < 3; ++e)
					{
						if (out.from.size() >= maxLines)
						{
							out.truncated = true;
							return;
						}
						const JPH::Float3& a = triangles[t * 3 + e];
						const JPH::Float3& b = triangles[t * 3 + (e + 1) % 3];
						out.from.push_back(ToEngine(c + JPH::Vec3(a)));
						out.to.push_back(ToEngine(c + JPH::Vec3(b)));
						out.kind.push_back(kind);
					}
				}
			}
		}
	}

	bool PhysicsSmokeTest()
	{
		PhysicsWorld world;
		if (!world.Init())
			return false;

		PhysicsShapeDesc floorShape;
		floorShape.type = PhysicsShapeType::Box;
		floorShape.halfExtents = { 1000.f, 10.f, 1000.f };
		PhysicsShapeDesc boxShape;
		boxShape.type = PhysicsShapeType::Box;
		boxShape.halfExtents = { 50.f, 50.f, 50.f };

		PhysicsBodyDesc floor;
		floor.shape = world.CreateShape(floorShape);
		floor.motion = PhysicsMotion::Static;
		floor.position = { 0.f, -10.f, 0.f }; // top face at y = 0
		PhysicsBodyDesc box;
		box.shape = world.CreateShape(boxShape);
		box.motion = PhysicsMotion::Dynamic;
		box.position = { 0.f, 500.f, 0.f };

		if (!world.CreateBody(floor).IsValid())
			return false;
		const PhysicsBodyId falling = world.CreateBody(box);
		if (!falling.IsValid())
			return false;
		world.OptimizeBroadPhase();

		for (int i = 0; i < 180; ++i) // three simulated seconds
			world.Update(PhysicsWorld::kFixedStep);

		PhysicsVec3 position;
		PhysicsQuat rotation;
		if (!world.GetTransform(falling, position, rotation))
			return false;
		// Resting on the floor puts the box centre half a box height above y = 0.
		return position.y > 45.f && position.y < 55.f;
	}
}
