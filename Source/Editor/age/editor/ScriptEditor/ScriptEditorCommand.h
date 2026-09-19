#pragma once

#include <age/script/ScriptCommon.h>

#include <memory>
#include <age/math/Vector3.h>
#include <age/editor/CommandManager/AbstractCommand.h>

namespace Ag
{
	class Script;
	struct ScriptEditorSelection;
	class ScriptNodeBase;

	struct CommandNodeData
	{
		ScriptNodeTypeId typeId;
		StringId instanceName;
		Ag::Vector2f pos;
		std::unique_ptr<ScriptNodeBase> node;
	};

	class ScriptEditorCommand : public AbstractCommand
	{
	protected:
		Script& myScript;
		ScriptEditorSelection& mySelection;
		int mySequenceNumber = -1;
	public:
		ScriptEditorCommand(Script& script, ScriptEditorSelection& selection)
			: myScript(script)
			, mySelection(selection)
		{}

		void Execute() override final;
		void Undo() override final;

		virtual void ExecuteImpl() = 0;
		virtual void UndoImpl() = 0;
	};
} // namespace Ag