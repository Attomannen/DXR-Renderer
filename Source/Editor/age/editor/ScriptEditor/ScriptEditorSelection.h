#pragma once

#include <vector>
#include <age/script/ScriptCommon.h>

namespace Ag
{

	struct ScriptEditorSelection
	{
		std::vector<ScriptNodeId> mySelectedNodes;
		std::vector<ScriptLinkId> mySelectedLinks;
	};
} // namespace Ag