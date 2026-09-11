#pragma once

#include <tge/editor/ScriptEditor/ScriptEditorCommand.h>

#include <unordered_map>

namespace Tga
{
	class DestroyNodeAndLinksCommand : public ScriptEditorCommand
	{
		bool myIsFirstTime = true;
		std::unordered_map<ScriptNodeId, CommandNodeData> myNodes;
		std::unordered_map<ScriptLinkId, ScriptLink> myLinks;
		std::unordered_map<ScriptPinId, ScriptPin> myPins;

	public:
		DestroyNodeAndLinksCommand(Script& script, ScriptEditorSelection& selection)
			: ScriptEditorCommand(script, selection)
		{}

		void Add(ScriptNodeId nodeId);
		void Add(ScriptLinkId linkId);

		void ExecuteImpl() override;
		void UndoImpl() override;
	};
}