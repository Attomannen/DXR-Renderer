#pragma once

#include <tge/editor/CommandManager/AbstractCommand.h>
#include <vector>

namespace Tga
{
	class SceneCommandBase : public AbstractCommand
	{
	public:
		virtual void GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const = 0;
	};
}
