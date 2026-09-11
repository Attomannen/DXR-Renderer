#pragma once

#include <tge/editor/ScriptEditor/ScriptEditorCommand.h>

#include <unordered_map>

namespace Tga
{
	class CreateNodeCommand : public ScriptEditorCommand
	{
		ScriptNodeId	myNodeId;
		CommandNodeData myNodeData;
		std::unordered_map<ScriptPinId, ScriptPin> myPins;

	public:
		CreateNodeCommand(Script& script, ScriptEditorSelection& selection, ScriptNodeTypeId typeId, Tga::Vector2f pos)
			: ScriptEditorCommand(script, selection)
			, myNodeId{ ScriptNodeId::InvalidId }
		{
			myNodeData.pos = pos;
			myNodeData.typeId = typeId;
		}

		void ExecuteImpl() override;
		void UndoImpl() override;
	};

}