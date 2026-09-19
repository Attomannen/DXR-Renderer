#pragma once

#include <age/scene/SceneObjectDefinition.h>

namespace Ag
{
	struct ScriptUpdateContext
	{
		// ScriptUpdateContext has a virtual destructor to allow it to be derived and the derived class recovered via dynamic_cast
		// This can be used to provide additional information to script nodes, like information about which object/entity the script runs on
		virtual ~ScriptUpdateContext() = default;

		std::unordered_map<StringId, Property>* dynamicProperties;
		const std::unordered_map<StringId, Property>* staticProperties;
		float deltaTime;
		float timeSeconds = 0.f;   // seconds since the game started (0 where there is no game clock)
		int frameNumber;
	};

} // namespace Ag