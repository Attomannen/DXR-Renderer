#include <tge/editor/ScriptEditor/Commands/FixupSelectionCommand.h>

#include <tge/editor/ScriptEditor/ScriptEditorSelection.h>

using namespace Tga;

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