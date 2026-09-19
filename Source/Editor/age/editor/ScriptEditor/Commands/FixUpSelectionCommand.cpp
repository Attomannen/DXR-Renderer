#include <age/editor/ScriptEditor/Commands/FixupSelectionCommand.h>

#include <age/editor/ScriptEditor/ScriptEditorSelection.h>

using namespace Ag;

void FixupSelectionCommand::ExecuteImpl()
{
	mySelectedLinks = mySelection.mySelectedLinks;
	mySelectedNodes = mySelection.mySelectedNodes;
	myCommand->Execute();
}

void FixupSelectionCommand::UndoImpl()
{
	myCommand->Undo();
	mySelection.mySelectedLinks = mySelectedLinks;
	mySelection.mySelectedNodes = mySelectedNodes;
}