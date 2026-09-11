#pragma once

#include <tge/editor/ScriptEditor/ScriptEditorCommand.h>
#include <string_view>

namespace Tga
{
	class CreateScriptCommand : public ScriptEditorCommand
	{
	public:
		CreateScriptCommand(const std::string_view& name, Script& script, ScriptEditorSelection& selection)
			: ScriptEditorCommand(script, selection), myName(name)
		{}

		void ExecuteImpl() override;
		void UndoImpl() override;

	private:
		std::string myName;
	};
}