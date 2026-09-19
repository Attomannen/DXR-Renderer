#pragma once

#include <age/editor/ScriptEditor/ScriptEditorCommand.h>

#include <unordered_map>

namespace Ag
{
	class MoveNodesCommand : public ScriptEditorCommand
	{
		ScriptNodeId myNode;
		std::unordered_map<ScriptNodeId, Vector2f> myFromPositions;
		std::unordered_map<ScriptNodeId, Vector2f> myToPositions;
	public:
		MoveNodesCommand(Script& script, ScriptEditorSelection& selection)
			: ScriptEditorCommand(script, selection)
		{}

		void SetPosition(ScriptNodeId id, Ag::Vector2f oldPosition, Ag::Vector2f newPosition)
		{
			if (myFromPositions.find(id) == myFromPositions.end())
				myFromPositions[id] = oldPosition;

			myToPositions[id] = newPosition;
		}

		void ExecuteImpl() override;
		void UndoImpl() override;
	};

} // namespace Ag