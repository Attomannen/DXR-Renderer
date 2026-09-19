#include <age/editor/ScriptEditor/ScriptEditorCommand.h>

#include <age/script/Script.h>

using namespace Ag;

void ScriptEditorCommand::Execute()
{
	mySequenceNumber = myScript.GetSequenceNumber();
	ExecuteImpl();
};

void ScriptEditorCommand::Undo()
{
	UndoImpl();
	myScript.SetSequenceNumber(mySequenceNumber);
};