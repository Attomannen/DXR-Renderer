#pragma once

#include <tge/script/ScriptCommon.h>

#include <memory>
#include <tge/math/Vector3.h>
#include <tge/editor/CommandManager/AbstractCommand.h>

namespace Tga
{
	class Script;
	struct ScriptEditorSelection;
	class ScriptNodeBase;

	struct CommandNodeData
	{
		ScriptNodeTypeId typeId;
		StringId instanceName;
		Tga::Vector2f pos;
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
} // namespace Tga