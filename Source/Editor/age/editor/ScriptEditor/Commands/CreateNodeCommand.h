#pragma once

#include <age/editor/ScriptEditor/ScriptEditorCommand.h>

#include <unordered_map>

namespace Ag
{
	class CreateNodeCommand : public ScriptEditorCommand
	{
		ScriptNodeId	myNodeId;
		CommandNodeData myNodeData;
		std::unordered_map<ScriptPinId, ScriptPin> myPins;

	public:
		CreateNodeCommand(Script& script, ScriptEditorSelection& selection, ScriptNodeTypeId typeId, Ag::Vector2f pos)
			: ScriptEditorCommand(script, selection)
			, myNodeId{ ScriptNodeId::InvalidId }
		{
			myNodeData.pos = pos;
			myNodeData.typeId = typeId;
		}

		// The node this command created (valid after it has run).
		ScriptNodeId GetNodeId() const { return myNodeId; }

		void ExecuteImpl() override;
		void UndoImpl() override;
	};

}