#pragma once

#include <age/editor/CommandManager/AbstractCommand.h>
#include <vector>

namespace Ag
{
	class SceneCommandBase : public AbstractCommand
	{
	public:
		virtual void GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const = 0;
	};
}
