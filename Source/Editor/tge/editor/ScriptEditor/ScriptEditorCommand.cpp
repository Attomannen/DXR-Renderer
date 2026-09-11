#include <tge/editor/ScriptEditor/ScriptEditorCommand.h>

#include <tge/script/Script.h>

using namespace Tga;

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