#pragma once

#include <tge/script/Contexts/ScriptUpdateContext.h>
#include <tge/math/Vector.h>
#include <tge/math/vector2.h>

namespace Tga
{
	// The update context of a script running on an object in the game. The game
	// implements it; nodes reach the object (and its physics body) through it, so the
	// node library stays independent of the game and the physics module.
	// Distances are engine units (cm); forces and impulses are in kg, m and s, like Jolt.
	struct GameScriptContext : ScriptUpdateContext
	{
		virtual Vector3f GetLocation() const = 0;
		virtual void SetLocation(const Vector3f& location) = 0;

		// Rotation as Euler degrees (X, Y = yaw, Z), the same values a scene file stores.
		// Scale and location are kept.
		virtual void SetRotation(const Vector3f& eulerDegrees) = 0;
		virtual Vector3f GetForward() const = 0;
		virtual Vector3f GetRight() const = 0;
		virtual Vector3f GetUp() const = 0;

		// True when the object has a physics body that is currently simulated.
		virtual bool HasPhysicsBody() const = 0;
		virtual void AddImpulse(const Vector3f& impulse) = 0;
		// The object's Camera component. While a camera is active the game looks through
		// it instead of the free-fly camera. Pitch is degrees, positive looks down.
		virtual bool HasCamera() const = 0;
		virtual void SetCameraActive(bool active) = 0;
		virtual void SetCameraPitch(float degrees) = 0;
		virtual float GetCameraPitch() const = 0;
		virtual void SetCameraFov(float degrees) = 0;
		virtual Vector3f GetCameraForward() const = 0;

		// cm/s. Zero / ignored without a simulated body.
		virtual Vector3f GetVelocity() const = 0;
		virtual void SetVelocity(const Vector3f& velocity) = 0;

		// Keys are Windows virtual-key codes (see KeyNameToCode in the node library).
		// All false / zero while the debug UI has the keyboard or mouse.
		virtual bool IsKeyDown(int keyCode) const = 0;
		virtual bool WasKeyPressed(int keyCode) const = 0;
		virtual Vector2f GetMouseDelta() const = 0;
	};
}
