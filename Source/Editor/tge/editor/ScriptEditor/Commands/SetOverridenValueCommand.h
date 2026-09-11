#pragma once

#include <tge/editor/ScriptEditor/ScriptEditorCommand.h>

namespace Tga
{
	class SetOverridenValueCommand : public ScriptEditorCommand
	{
		ScriptPinId myPinId;
		Property myNewData;
		Property myOldData;

	public:
		SetOverridenValueCommand(Script& script, ScriptEditorSelection& selection, ScriptPinId pinId, Property data)
			: ScriptEditorCommand(script, selection)
			, myPinId(pinId)
			, myNewData(data)
		{}

		void ExecuteImpl() override;
		void UndoImpl() override;
	};
} // namespace Tga