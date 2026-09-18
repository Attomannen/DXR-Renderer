#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Jolt wrapper. No Jolt type appears in this header, so only the Physics project
// includes Jolt.
//
// Units: the engine is 1 unit = 1 cm, Jolt works in metres. Everything that crosses
// this interface is in engine units (cm, quaternion x/y/z/w, +Y up) and is converted
// inside PhysicsWorld.cpp. Rotations are quaternions so this module does not have to
// know the engine's Euler order; callers convert with the engine's own Quaternion.
namespace Tga
{
	struct PhysicsBodyId
	{
		uint32_t value = 0xFFFFFFFFu;
		bool IsValid() const { return value != 0xFFFFFFFFu; }
		bool operator==(const PhysicsBodyId& other) const { return value == other.value; }
	};

	struct PhysicsShapeId
	{
		uint32_t value = 0xFFFFFFFFu;
		bool IsValid() const { return value != 0xFFFFFFFFu; }
	};

	struct PhysicsVec3
	{
		float x = 0.f, y = 0.f, z = 0.f;
	};

	struct PhysicsQuat
	{
		float x = 0.f, y = 0.f, z = 0.f, w = 1.f;
	};

	enum class PhysicsMotion : uint8_t
	{
		Static,     // never moves; may be a triangle mesh
		Kinematic,  // moved by the game, pushes dynamic bodies
		Dynamic,    // simulated; needs a convex shape (no triangle mesh)
	};

	enum class PhysicsShapeType : uint8_t
	{
		Box,
		Sphere,
		Capsule,
		ConvexHull,
		TriangleMesh,
	};

	// Vertex / index source for ConvexHull and TriangleMesh. Only read during
	// CreateShape; the world keeps its own copy.
	struct PhysicsMeshData
	{
		const float* positions = nullptr; // x,y,z (engine units) at each stride
		size_t vertexCount = 0;
		size_t positionStrideBytes = 12;
		const uint32_t* indices = nullptr; // triangle list; unused by ConvexHull
		size_t indexCount = 0;
	};

	struct PhysicsShapeDesc
	{
		PhysicsShapeType type = PhysicsShapeType::Box;

		PhysicsVec3 halfExtents{ 50.f, 50.f, 50.f }; // Box, cm
		float radius = 50.f;                         // Sphere / Capsule, cm
		float halfHeight = 50.f;                     // Capsule: half of the cylinder part, along Y, cm

		PhysicsMeshData mesh;                        // ConvexHull / TriangleMesh
		PhysicsVec3 meshScale{ 1.f, 1.f, 1.f };      // applied to mesh shapes

		PhysicsVec3 offset;                          // shape centre relative to the body origin, cm
	};

	struct PhysicsBodyDesc
	{
		PhysicsShapeId shape;
		PhysicsMotion motion = PhysicsMotion::Static;

		PhysicsVec3 position;                        // cm
		PhysicsQuat rotation;

		float mass = 0.f;                            // kg; <= 0 derives it from shape volume
		float friction = 0.5f;
		float restitution = 0.f;
		float gravityFactor = 1.f;
		float linearDamping = 0.05f;
		float angularDamping = 0.05f;

		uint64_t userData = 0;                       // handed back by GetUserData, e.g. an object id
	};

	// Wireframe of the collision near a point, for drawing. Every shape type comes out as
	// triangle edges, so a mesh shows its real triangles and a sphere its tessellation.
	struct PhysicsDebugLines
	{
		enum Kind : uint8_t { Static, Awake, Asleep };
		std::vector<PhysicsVec3> from;
		std::vector<PhysicsVec3> to;
		std::vector<Kind> kind;
		bool truncated = false; // hit the line cap; shrink the radius to see the rest
	};

	class PhysicsWorld
	{
	public:
		PhysicsWorld();
		~PhysicsWorld();
		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

		// Global Jolt setup (allocator, factory, types) is reference counted, so several
		// worlds (game + editor preview) can coexist. Returns false if Jolt fails.
		bool Init();
		void Shutdown();
		bool IsInitialized() const;

		// Advances the simulation by a fixed 1/60 s using an accumulator. Big frame
		// times are clamped to kMaxSubSteps steps so a hitch cannot spiral.
		// Returns the number of steps taken.
		int Update(float deltaSeconds);

		static constexpr float kFixedStep = 1.f / 60.f;
		static constexpr int kMaxSubSteps = 4;

		void SetGravity(const PhysicsVec3& gravityMetersPerSecond2);

		// Shapes are shared: build one per model, create many bodies from it.
		// Returns an invalid id on failure (e.g. degenerate hull, empty mesh).
		PhysicsShapeId CreateShape(const PhysicsShapeDesc& desc);
		void ReleaseShape(PhysicsShapeId shape);

		// Fails (invalid id) for a Dynamic body with a triangle-mesh shape.
		PhysicsBodyId CreateBody(const PhysicsBodyDesc& desc);
		void DestroyBody(PhysicsBodyId body);

		// Call once after adding a lot of static bodies (level load).
		void OptimizeBroadPhase();

		bool GetTransform(PhysicsBodyId body, PhysicsVec3& outPosition, PhysicsQuat& outRotation) const;
		void SetTransform(PhysicsBodyId body, const PhysicsVec3& position, const PhysicsQuat& rotation);
		// Kinematic bodies: reach the target by the next step with a proper velocity.
		void MoveKinematic(PhysicsBodyId body, const PhysicsVec3& position, const PhysicsQuat& rotation, float deltaSeconds);

		bool IsAwake(PhysicsBodyId body) const;
		uint64_t GetUserData(PhysicsBodyId body) const;

		void AddForce(PhysicsBodyId body, const PhysicsVec3& force);
		void AddImpulse(PhysicsBodyId body, const PhysicsVec3& impulse);
		void SetLinearVelocity(PhysicsBodyId body, const PhysicsVec3& velocity);
		PhysicsVec3 GetLinearVelocity(PhysicsBodyId body) const;

		uint32_t GetBodyCount() const;

		// Appends the collision edges of every body within radius (cm) of center.
		void CollectDebugLines(const PhysicsVec3& center, float radius, size_t maxLines, PhysicsDebugLines& out) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> myImpl;
	};

	// Returns the linked Jolt version, e.g. "5.6.0". Also proves the library links.
	const char* GetPhysicsVersion();

	// Creates and destroys a world with one falling box; true if it landed on the floor.
	bool PhysicsSmokeTest();
}
