#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"
#include <tge/physics/PhysicsWorld.h>

// Physics test on the material-preview debug sphere: Start drops it under gravity onto a
// floor plane, Reset puts it back where it began. The level has no colliders yet, so the
// floor is a plane at the bottom of the scene bounds (adjustable).

void GameWorld::Impl::StartPhysicsTest()
{
	if (!debugBallValid || physicsActive)
		return;

	// Start from where the sphere is drawn right now.
	Vector3f start = debugBallPos;
	if (debugBallFollowCam)
	{
		const Matrix4x4f camXf = camera.GetTransform();
		start = camXf.GetPosition() + camXf.GetForward() * (debugBallRadius * 5.f);
	}

	if (!physics.Init())
	{
		ERROR_PRINT("physics: Jolt failed to initialise");
		return;
	}
	physics.SetGravity({ 0.f, -physicsGravity, 0.f });

	const float floorY = sceneCenter.y - sceneExtents.y + physicsFloorOffset;
	const float floorHalf = std::max(std::max(sceneExtents.x, sceneExtents.z) * 4.f, 10000.f);

	Tga::PhysicsShapeDesc floorShape;
	floorShape.type = Tga::PhysicsShapeType::Box;
	floorShape.halfExtents = { floorHalf, 50.f, floorHalf };
	Tga::PhysicsBodyDesc floor;
	floor.shape = physics.CreateShape(floorShape);
	floor.motion = Tga::PhysicsMotion::Static;
	floor.position = { sceneCenter.x, floorY - 50.f, sceneCenter.z }; // top face at floorY
	floor.friction = physicsFriction;
	floor.restitution = physicsRestitution;
	physics.CreateBody(floor);

	Tga::PhysicsShapeDesc ballShape;
	ballShape.type = Tga::PhysicsShapeType::Sphere;
	ballShape.radius = debugBallRadius;
	Tga::PhysicsBodyDesc ball;
	ball.shape = physics.CreateShape(ballShape);
	ball.motion = Tga::PhysicsMotion::Dynamic;
	ball.position = { start.x, start.y, start.z };
	ball.friction = physicsFriction;
	ball.restitution = physicsRestitution;
	ball.mass = physicsMass;
	physicsBall = physics.CreateBody(ball);
	physics.OptimizeBroadPhase();
	if (!physicsBall.IsValid())
	{
		ERROR_PRINT("physics: could not create the sphere body");
		physics.Shutdown();
		return;
	}

	physicsSavedFollowCam = debugBallFollowCam;
	physicsSavedShowBall = showDebugBall;
	physicsStartPos = start;
	debugBallFollowCam = false; // the physics owns the position now
	debugBallPos = start;
	showDebugBall = true;
	physicsActive = true;
}

void GameWorld::Impl::ResetPhysicsTest()
{
	if (!physicsActive)
		return;

	physics.Shutdown(); // removes the bodies and the floor
	physicsBall = {};
	physicsActive = false;

	debugBallPos = physicsStartPos;
	debugBallFollowCam = physicsSavedFollowCam;
	showDebugBall = physicsSavedShowBall;
}

void GameWorld::Impl::UpdatePhysicsTest(float deltaSeconds)
{
	if (!physicsActive)
		return;

	physics.Update(deltaSeconds);

	Tga::PhysicsVec3 position;
	Tga::PhysicsQuat rotation;
	if (physics.GetTransform(physicsBall, position, rotation))
		debugBallPos = { position.x, position.y, position.z };
}

void GameWorld::Impl::DrawPhysicsTab()
{
	if (!debugBallValid)
	{
		ImGui::TextDisabled("The debug sphere (Primitives/Sphere.fbx) is not loaded.");
		return;
	}

	ImGui::TextWrapped("Drops the material-preview debug sphere onto a floor at the bottom of the scene. "
		"Move the camera where you want it, then press Start; Reset brings it back.");
	ImGui::Separator();

	if (!physicsActive)
	{
		if (ImGui::Button("Start physics"))
			StartPhysicsTest();
	}
	else
	{
		if (ImGui::Button("Reset"))
			ResetPhysicsTest();
		ImGui::SameLine();
		if (ImGui::Button("Launch up"))
			physics.SetLinearVelocity(physicsBall, { 0.f, 600.f, 0.f }); // 6 m/s up
		ImGui::SameLine();
		ImGui::TextDisabled("%s", physics.IsAwake(physicsBall) ? "moving" : "at rest");
	}

	ImGui::Separator();
	ImGui::BeginDisabled(physicsActive); // applied when the simulation is started
	ImGui::SliderFloat("Sphere radius", &debugBallRadius, 5.f, 400.f, "%.0f cm");
	ImGui::SliderFloat("Mass (0 = from volume)", &physicsMass, 0.f, 500.f, "%.1f kg");
	ImGui::SliderFloat("Bounciness", &physicsRestitution, 0.f, 1.f, "%.2f");
	ImGui::SliderFloat("Friction", &physicsFriction, 0.f, 2.f, "%.2f");
	ImGui::SliderFloat("Floor height offset", &physicsFloorOffset, -2000.f, 2000.f, "%.0f cm");
	ImGui::EndDisabled();
	if (ImGui::SliderFloat("Gravity", &physicsGravity, 0.f, 30.f, "%.2f m/s\xC2\xB2") && physicsActive)
		physics.SetGravity({ 0.f, -physicsGravity, 0.f });

	if (physicsActive)
	{
		ImGui::Separator();
		ImGui::Text("Sphere position: %.0f, %.0f, %.0f", debugBallPos.x, debugBallPos.y, debugBallPos.z);
		ImGui::Text("Started at:      %.0f, %.0f, %.0f", physicsStartPos.x, physicsStartPos.y, physicsStartPos.z);
	}
}
