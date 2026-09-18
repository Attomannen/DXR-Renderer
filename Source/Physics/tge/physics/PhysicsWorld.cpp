#include "PhysicsWorld.h"

#include <algorithm>
#include <cstdio>
#include <thread>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

namespace Tga
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

		std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
		std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
		std::unique_ptr<JPH::PhysicsSystem> system;

		std::vector<JPH::RefConst<JPH::Shape>> shapes; // released slots are null
		std::vector<uint32_t> freeShapeSlots;

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

	uint32_t PhysicsWorld::GetBodyCount() const
	{
		return myImpl ? myImpl->system->GetNumBodies() : 0;
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
