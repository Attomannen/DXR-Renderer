#pragma once

#include <age/editor/ScriptEditor/ScriptEditorCommand.h>
#include <age/editor/ScriptEditor/Commands/DestroyNodeAndLinksCommand.h>

namespace Ag
{
	class CreateLinkCommand : public ScriptEditorCommand
	{
		DestroyNodeAndLinksCommand myDestroyNodeAndLinksCommand;
		ScriptLinkId myLinkId;
		ScriptLink myLinkData;
	public:
		CreateLinkCommand(Script& script, ScriptEditorSelection& selection, ScriptLink linkData)
			: ScriptEditorCommand(script, selection)
			, myDestroyNodeAndLinksCommand(script, selection)
			, myLinkId{ ScriptLinkId::InvalidId }
			, myLinkData(linkData)
		{}

		void ExecuteImpl() override;
		void UndoImpl() override;
	};
}