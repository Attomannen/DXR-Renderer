#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"
#include <age/physics/PhysicsWorld.h>
#include <age/math/Quaternion.h>

// Physics in the game harness.
//
// Scene objects that have collision (the .tgo Model "Collision" setting, or a Collider
// component) become Jolt bodies. Without a Rigidbody they are static level geometry;
// with one they are props that fall when Start is pressed in the Physics tab, and
// Reset puts them back. The material-preview debug sphere can join in, and brings its
// own floor when the scene has no static collision.

using namespace Ag;

namespace
{
	enum class Kind { None, Box, Sphere, Capsule, ConvexHull, TriangleMesh };

	Kind KindFromName(const std::string& name)
	{
		if (name == "Box") return Kind::Box;
		if (name == "Sphere") return Kind::Sphere;
		if (name == "Capsule") return Kind::Capsule;
		if (name == "ConvexHull") return Kind::ConvexHull;
		if (name == "TriangleMesh") return Kind::TriangleMesh;
		return Kind::None;
	}

	// Convex hulls of dense meshes are slow to build and gain nothing from every vertex.
	constexpr size_t kMaxHullPoints = 4000;

	PhysicsVec3 ToPhysics(const Vector3f& v) { return { v.x, v.y, v.z }; }
}

void GameWorld::Impl::ClearScenePhysics()
{
	if (physicsActive)
		ResetPhysicsTest();
	physics.Shutdown();
	sceneCharacters.clear();
	scenePhysicsObjects.clear();
	scenePhysicsShapes.clear();
	scenePhysicsStaticCount = 0;
	physicsAutoStart = GameScene::EnvInt("BENCH_PHYSICS", 0) != 0;
	if (GameScene::EnvInt("BENCH_PHYSICS_WIRE", 0) != 0) showPhysicsWireframe = true;
	physicsLogTimer = 0.f;
	physicsLogCount = 0;
}

void GameWorld::Impl::RegisterScenePhysics(const GameScene::SceneEntry& entry, const std::shared_ptr<Model>& model,
	const Matrix4x4f& worldTransform, size_t instanceIndex)
{
	const GameScene::SceneEntryPhysics& p = entry.physics;
	if (!p.Any() || !model)
		return;

	// What shape, and how does the object move?
	const std::string motion = p.hasBody ? p.motion : "Static";
	const bool isStatic = motion == "Static";
	const bool isDynamic = motion == "Dynamic";

	Kind kind = Kind::None;
	if (p.hasCollider)
		kind = p.colliderShape == "Auto" ? Kind::None : KindFromName(p.colliderShape);
	const bool fromModel = kind == Kind::None; // Auto, or no explicit primitive
	if (fromModel)
	{
		kind = KindFromName(p.hasCollider && p.colliderShape != "Auto" ? p.colliderShape : p.modelCollision);
		if (kind == Kind::None && (p.modelCollision == "Auto" || p.colliderShape == "Auto"))
			kind = isStatic ? Kind::TriangleMesh : Kind::ConvexHull;
	}
	if (kind == Kind::None)
		return;
	if (kind == Kind::TriangleMesh && !isStatic)
		kind = Kind::ConvexHull; // a mesh has no volume to simulate

	Vector3f position, scale;
	Quaternionf rotation;
	worldTransform.DecomposeMatrix(position, rotation, scale);
	scale = { std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) };

	if (!physics.Init())
		return;
	physics.SetGravity({ 0.f, -physicsGravity, 0.f });

	PhysicsBodyDesc body;
	body.motion = isStatic ? PhysicsMotion::Static : isDynamic ? PhysicsMotion::Dynamic : PhysicsMotion::Kinematic;
	body.position = ToPhysics(position);
	body.rotation = { rotation.X, rotation.Y, rotation.Z, rotation.W };
	body.mass = p.mass;
	body.friction = p.friction;
	body.restitution = p.restitution;
	body.gravityFactor = p.gravityFactor;
	body.linearDamping = p.linearDamping;
	body.angularDamping = p.angularDamping;
	body.userData = instanceIndex + 1; // 0 means "none" in contact events
	body.isSensor = p.hasCollider && p.isTrigger;

	// Shapes are shared between identical objects (same model, kind and scale).
	char key[512];
	std::snprintf(key, sizeof(key), "%s|%d|%.3f,%.3f,%.3f|%.1f,%.1f,%.1f,%.1f|%.1f,%.1f,%.1f", entry.fbx.c_str(), (int)kind,
		scale.x, scale.y, scale.z, p.radius, p.halfHeight, p.halfExtents.x, p.halfExtents.y,
		p.offset.x, p.offset.y, p.offset.z);
	std::string shapeKey = key;
	if (kind == Kind::TriangleMesh)
		shapeKey += "|" + std::to_string(instanceIndex); // baked into world space, so never shared

	PhysicsShapeId shape;
	if (auto it = scenePhysicsShapes.find(shapeKey); it != scenePhysicsShapes.end())
	{
		shape = it->second;
	}
	else
	{
		PhysicsShapeDesc desc;
		const Ag::BoxSphereBounds& bounds = model->GetBounds();
		const float maxXZ = std::max(scale.x, scale.z);

		switch (kind)
		{
		case Kind::Box:
			desc.type = PhysicsShapeType::Box;
			if (fromModel)
			{
				desc.halfExtents = { bounds.boxExtents.x * scale.x, bounds.boxExtents.y * scale.y, bounds.boxExtents.z * scale.z };
				desc.offset = { bounds.center.x * scale.x, bounds.center.y * scale.y, bounds.center.z * scale.z };
			}
			else
			{
				desc.halfExtents = { p.halfExtents.x * scale.x, p.halfExtents.y * scale.y, p.halfExtents.z * scale.z };
				desc.offset = { p.offset.x * scale.x, p.offset.y * scale.y, p.offset.z * scale.z };
			}
			break;
		case Kind::Sphere:
			desc.type = PhysicsShapeType::Sphere;
			desc.radius = p.radius * std::max(maxXZ, scale.y);
			desc.offset = { p.offset.x * scale.x, p.offset.y * scale.y, p.offset.z * scale.z };
			break;
		case Kind::Capsule:
			desc.type = PhysicsShapeType::Capsule;
			desc.radius = p.radius * maxXZ;
			desc.halfHeight = p.halfHeight * scale.y;
			desc.offset = { p.offset.x * scale.x, p.offset.y * scale.y, p.offset.z * scale.z };
			break;
		case Kind::ConvexHull:
		case Kind::TriangleMesh:
		{
			CollisionGeometry geometry;
			if (!ModelFactory::GetInstance().GetCollisionGeometry(StringRegistry::RegisterOrGetString(entry.fbx), geometry))
			{
				ERROR_PRINT("physics: no collision geometry for '%s' (skinned model, or its mesh cache is missing)", entry.fbx.c_str());
				return;
			}

			std::vector<float> points;
			if (kind == Kind::TriangleMesh)
			{
				// Static level geometry: bake the full world transform into the vertices so
				// rotation, non-uniform scale and mirroring all come out exactly as rendered.
				points.resize(geometry.positions.size());
				const Matrix4x4f& m = worldTransform;
				for (size_t v = 0; v + 2 < geometry.positions.size(); v += 3)
				{
					const float x = geometry.positions[v], y = geometry.positions[v + 1], z = geometry.positions[v + 2];
					points[v]     = x * m(1, 1) + y * m(2, 1) + z * m(3, 1) + m(4, 1);
					points[v + 1] = x * m(1, 2) + y * m(2, 2) + z * m(3, 2) + m(4, 2);
					points[v + 2] = x * m(1, 3) + y * m(2, 3) + z * m(3, 3) + m(4, 3);
				}
				desc.type = PhysicsShapeType::TriangleMesh;
				desc.mesh.indices = geometry.indices.data();
				desc.mesh.indexCount = geometry.indices.size();
				body.position = {};
				body.rotation = {};
			}
			else
			{
				const size_t count = geometry.positions.size() / 3;
				const size_t stride = std::max<size_t>(1, count / kMaxHullPoints);
				for (size_t v = 0; v < count; v += stride)
					points.insert(points.end(), geometry.positions.begin() + v * 3, geometry.positions.begin() + v * 3 + 3);
				desc.type = PhysicsShapeType::ConvexHull;
				desc.meshScale = ToPhysics(scale);
			}
			desc.mesh.positions = points.data();
			desc.mesh.vertexCount = points.size() / 3;

			const auto t0 = std::chrono::steady_clock::now();
			shape = physics.CreateShape(desc);
			if (kind == Kind::TriangleMesh)
				INFO_PRINT("physics: triangle mesh '%s', %zu triangles, %.0f ms", entry.fbx.c_str(),
					geometry.indices.size() / 3,
					std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
			break;
		}
		case Kind::None:
			return;
		}

		if (kind != Kind::ConvexHull && kind != Kind::TriangleMesh)
			shape = physics.CreateShape(desc);
		if (!shape.IsValid())
		{
			ERROR_PRINT("physics: could not build the collision shape for '%s'", entry.fbx.c_str());
			return;
		}
		scenePhysicsShapes.emplace(shapeKey, shape);
	}
	body.shape = shape;

	ScenePhysicsObject object;
	object.instance = instanceIndex;
	object.desc = body;
	object.startTransform = worldTransform;
	object.scale = scale;
	object.dynamic = !isStatic;
	if (isStatic)
	{
		object.body = physics.CreateBody(body);
		++scenePhysicsStaticCount;
	}
	scenePhysicsObjects.push_back(object);
}

void GameWorld::Impl::StartPhysicsTest()
{
	if (physicsActive)
		return;
	if (!physics.Init())
	{
		ERROR_PRINT("physics: Jolt failed to initialise");
		return;
	}
	physics.SetGravity({ 0.f, -physicsGravity, 0.f });

	// Props start falling from where the scene put them.
	for (ScenePhysicsObject& object : scenePhysicsObjects)
		if (object.dynamic)
			object.body = physics.CreateBody(object.desc);

	for (SceneCharacterObject& character : sceneCharacters)
		character.id = physics.CreateCharacter(character.desc);

	if (physicsIncludeBall && debugBallValid)
	{
		// Start from where the sphere is drawn right now.
		Vector3f start = debugBallPos;
		if (debugBallFollowCam)
		{
			const Matrix4x4f camXf = camera.GetTransform();
			start = camXf.GetPosition() + camXf.GetForward() * (debugBallRadius * 5.f);
		}

		// The scene's own colliders are the floor; only add a plane when it has none.
		if (scenePhysicsStaticCount == 0)
		{
			const float floorY = sceneCenter.y - sceneExtents.y + physicsFloorOffset;
			const float floorHalf = std::max(std::max(sceneExtents.x, sceneExtents.z) * 4.f, 10000.f);

			PhysicsShapeDesc floorShape;
			floorShape.type = PhysicsShapeType::Box;
			floorShape.halfExtents = { floorHalf, 50.f, floorHalf };
			PhysicsBodyDesc floor;
			floor.shape = physics.CreateShape(floorShape);
			floor.motion = PhysicsMotion::Static;
			floor.position = { sceneCenter.x, floorY - 50.f, sceneCenter.z }; // top face at floorY
			floor.friction = physicsFriction;
			floor.restitution = physicsRestitution;
			physicsFloor = physics.CreateBody(floor);
		}

		PhysicsShapeDesc ballShape;
		ballShape.type = PhysicsShapeType::Sphere;
		// The sphere is drawn scaled so the model's bounds radius equals debugBallRadius, but that
		// radius is the box diagonal; the surface is at the box half-size.
		ballShape.radius = debugBallRadius * (debugBallModelExtent / debugBallModelRadius);
		PhysicsBodyDesc ball;
		ball.shape = physics.CreateShape(ballShape);
		ball.motion = PhysicsMotion::Dynamic;
		ball.position = ToPhysics(start);
		ball.friction = physicsFriction;
		ball.restitution = physicsRestitution;
		ball.mass = physicsMass;
		physicsBall = physics.CreateBody(ball);

		physicsSavedFollowCam = debugBallFollowCam;
		physicsSavedShowBall = showDebugBall;
		physicsStartPos = start;
		debugBallFollowCam = false; // the physics owns the position now
		debugBallPos = start;
		showDebugBall = true;
	}

	physics.OptimizeBroadPhase();
	physicsActive = true;
}

void GameWorld::Impl::ResetPhysicsTest()
{
	if (!physicsActive)
		return;
	physicsActive = false;

	for (ScenePhysicsObject& object : scenePhysicsObjects)
	{
		if (!object.dynamic)
			continue;
		if (object.body.IsValid())
			physics.DestroyBody(object.body);
		object.body = {};
		if (object.instance < models.size())
			SetInstanceTransform(object.instance, object.startTransform);
	}

	for (SceneCharacterObject& character : sceneCharacters)
	{
		if (character.id.IsValid())
			physics.DestroyCharacter(character.id);
		character.id = {};
		if (character.instance < models.size())
			SetInstanceTransform(character.instance, character.startTransform);
	}

	if (physicsBall.IsValid())
	{
		physics.DestroyBody(physicsBall);
		physicsBall = {};
		debugBallPos = physicsStartPos;
		debugBallFollowCam = physicsSavedFollowCam;
		showDebugBall = physicsSavedShowBall;
	}
	if (physicsFloor.IsValid())
	{
		physics.DestroyBody(physicsFloor);
		physicsFloor = {};
	}
}

void GameWorld::Impl::UpdatePhysicsTest(float deltaSeconds)
{
	if (physicsAutoStart && (!scenePhysicsObjects.empty() || !sceneCharacters.empty()))
	{
		physicsAutoStart = false;
		physicsIncludeBall = false;
		StartPhysicsTest();
	}
	if (!physicsActive)
		return;

	physics.Update(deltaSeconds);
	DispatchContactEvents();

	PhysicsVec3 position;
	PhysicsQuat rotation;
	for (const ScenePhysicsObject& object : scenePhysicsObjects)
	{
		if (!object.dynamic || !object.body.IsValid() || object.instance >= models.size())
			continue;
		if (!physics.GetTransform(object.body, position, rotation))
			continue;
		Matrix4x4f xf = Matrix4x4f::CreateFromScale(object.scale) *
			Matrix4x4f::CreateFromRotation(Quaternionf(rotation.w, rotation.x, rotation.y, rotation.z));
		xf.SetPosition({ position.x, position.y, position.z });
		SetInstanceTransform(object.instance, xf);
	}

	for (const SceneCharacterObject& character : sceneCharacters)
	{
		PhysicsVec3 feet;
		if (!character.id.IsValid() || character.instance >= models.size() || !physics.GetCharacterPosition(character.id, feet))
			continue;
		// Only the position comes from physics; the script owns which way it faces.
		Matrix4x4f transform = GetInstanceTransform(character.instance);
		transform.SetPosition({ feet.x, feet.y, feet.z });
		SetInstanceTransform(character.instance, transform);
	}

	physicsLogTimer += deltaSeconds;
	if (physicsLogTimer >= 1.f && physicsLogCount < 8)
	{
		physicsLogTimer = 0.f;
		++physicsLogCount;
		for (const SceneCharacterObject& character : sceneCharacters)
		{
			PhysicsVec3 feet;
			if (physics.GetCharacterPosition(character.id, feet))
				INFO_PRINT("physics: character at %.1f, %.1f, %.1f (%s)", feet.x, feet.y, feet.z, physics.IsCharacterOnGround(character.id) ? "on ground" : "in air");
		}
		for (const ScenePhysicsObject& object : scenePhysicsObjects)
			if (object.dynamic && physics.GetTransform(object.body, position, rotation))
			{
				INFO_PRINT("physics: prop %zu at %.1f, %.1f, %.1f (%s)", object.instance, position.x, position.y, position.z,
					physics.IsAwake(object.body) ? "moving" : "at rest");
				break;
			}
	}

	if (physicsBall.IsValid() && physics.GetTransform(physicsBall, position, rotation))
		debugBallPos = { position.x, position.y, position.z };
}

void GameWorld::Impl::DrawPhysicsOverlay()
{
	if (!showPhysicsWireframe || !physics.IsInitialized())
		return;

	const Matrix4x4f camXf = camera.GetTransform();
	const Vector3f eye = camXf.GetPosition();
	physicsWireLines = {};
	physics.CollectDebugLines(ToPhysics(eye), physicsWireRadius, 120000, physicsWireLines);

	const Matrix4x4f viewProj = Matrix4x4f::GetFastInverse(camXf) * camera.GetProjection();
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	ImDrawList* draw = ImGui::GetBackgroundDrawList();

	auto project = [&](const PhysicsVec3& p, Vector4f& clip) { clip = Vector4f(p.x, p.y, p.z, 1.f) * viewProj; };
	const ImU32 colors[3] = { IM_COL32(70, 230, 100, 200), IM_COL32(255, 160, 50, 230), IM_COL32(110, 150, 255, 200) };

	for (size_t i = 0; i < physicsWireLines.from.size(); ++i)
	{
		Vector4f a, b;
		project(physicsWireLines.from[i], a);
		project(physicsWireLines.to[i], b);

		// Clip against the near plane (w = 1) so lines crossing behind the camera still draw.
		if (a.w <= 1.f && b.w <= 1.f)
			continue;
		if (a.w <= 1.f) { const float t = (1.f - a.w) / (b.w - a.w); a = a + (b - a) * t; }
		else if (b.w <= 1.f) { const float t = (1.f - b.w) / (a.w - b.w); b = b + (a - b) * t; }

		const ImVec2 sa((a.x / a.w * 0.5f + 0.5f) * size.x, (1.f - (a.y / a.w * 0.5f + 0.5f)) * size.y);
		const ImVec2 sb((b.x / b.w * 0.5f + 0.5f) * size.x, (1.f - (b.y / b.w * 0.5f + 0.5f)) * size.y);
		draw->AddLine(sa, sb, colors[physicsWireLines.kind[i]], 1.f);
	}

	if (physicsWireLines.truncated)
		draw->AddText(ImVec2(12.f, size.y - 28.f), IM_COL32(255, 200, 80, 255), "Physics wireframe: line limit reached, lower the radius");
}

void GameWorld::Impl::DrawPhysicsTab()
{
	int dynamicProps = 0;
	for (const ScenePhysicsObject& object : scenePhysicsObjects)
		dynamicProps += object.dynamic ? 1 : 0;

	ImGui::Text("Scene collision: %d static, %d prop(s)", scenePhysicsStaticCount, dynamicProps);
	{
		ImGui::Text("Scripts: %zu object(s) run an event graph", sceneScripts.size());
		ImGui::SameLine();
		ImGui::Checkbox("Run scripts", &scriptsEnabled);
	}
	ImGui::TextWrapped("Objects get collision from the .tgo Model 'Collision' setting or a Collider component; "
		"add a Rigidbody to make one fall. Start drops everything, Reset puts it back.");
	ImGui::Checkbox("Show collision", &showPhysicsWireframe);
	if (showPhysicsWireframe)
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.f);
		ImGui::SliderFloat("Radius", &physicsWireRadius, 2.f, 200.f, "%.1f m", ImGuiSliderFlags_Logarithmic);
	}
	ImGui::Separator();

	if (!physicsActive)
	{
		if (ImGui::Button("Start physics"))
			StartPhysicsTest();
		ImGui::SameLine();
		ImGui::BeginDisabled(!debugBallValid);
		ImGui::Checkbox("Drop the debug sphere", &physicsIncludeBall);
		ImGui::EndDisabled();
	}
	else
	{
		if (ImGui::Button("Reset"))
			ResetPhysicsTest();
		if (physicsBall.IsValid())
		{
			ImGui::SameLine();
			if (ImGui::Button("Launch sphere up"))
				physics.SetLinearVelocity(physicsBall, { 0.f, 600.f, 0.f }); // 6 m/s up
			ImGui::SameLine();
			ImGui::TextDisabled("%s", physics.IsAwake(physicsBall) ? "moving" : "at rest");
		}
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Debug sphere");
	ImGui::BeginDisabled(physicsActive); // applied when the simulation is started
	ImGui::SliderFloat("Sphere radius", &debugBallRadius, 0.05f, 4.f, "%.2f m");
	ImGui::SliderFloat("Mass (0 = from volume)", &physicsMass, 0.f, 500.f, "%.1f kg");
	ImGui::SliderFloat("Bounciness", &physicsRestitution, 0.f, 1.f, "%.2f");
	ImGui::SliderFloat("Friction", &physicsFriction, 0.f, 2.f, "%.2f");
	ImGui::SliderFloat("Floor height offset", &physicsFloorOffset, -20.f, 20.f, "%.2f m");
	ImGui::EndDisabled();
	if (scenePhysicsStaticCount > 0)
		ImGui::TextDisabled("The scene's static collision is the floor; the offset is unused.");
	ImGui::Separator();
	if (ImGui::SliderFloat("Gravity", &physicsGravity, 0.f, 30.f, "%.2f m/s\xC2\xB2") && physics.IsInitialized())
		physics.SetGravity({ 0.f, -physicsGravity, 0.f });

	if (physicsBall.IsValid())
	{
		ImGui::Separator();
		ImGui::Text("Sphere position: %.0f, %.0f, %.0f", debugBallPos.x, debugBallPos.y, debugBallPos.z);
		ImGui::Text("Started at:      %.0f, %.0f, %.0f", physicsStartPos.x, physicsStartPos.y, physicsStartPos.z);
	}
}
