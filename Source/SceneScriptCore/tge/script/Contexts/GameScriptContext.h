#pragma once

#include <tge/script/Contexts/ScriptUpdateContext.h>
#include <tge/math/Vector.h>

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

		// True when the object has a physics body that is currently simulated.
		virtual bool HasPhysicsBody() const = 0;
		virtual void AddImpulse(const Vector3f& impulse) = 0;
	};
}
