#pragma once

#include <vector>
#include <tge/script/ScriptCommon.h>

namespace Tga
{

	struct ScriptEditorSelection
	{
		std::vector<ScriptNodeId> mySelectedNodes;
		std::vector<ScriptLinkId> mySelectedLinks;
	};
} // namespace Tga