#include <age/editor/Material/ChangeMaterialCommand.h>

#include <age/editor/Editor.h>
#include <age/editor/Material/MaterialDocument.h>

using namespace Ag;

ChangeMaterialCommand::ChangeMaterialCommand(MaterialDocument& aDocument, const MaterialAsset& aNewValue, const MaterialAsset& aOldValue)
	: myDocument(&aDocument)
	, myNewValue(aNewValue)
	, myOldValue(aOldValue)
{
}

void ChangeMaterialCommand::Execute()
{
	// The undo stack is never pruned when a document closes -- see
	// Editor::IsDocumentOpen's comment -- so this command can outlive the
	// document it was built for.
	if (Editor::GetEditor()->IsDocumentOpen(myDocument))
		myDocument->SetMaterialAsset(myNewValue);
}

void ChangeMaterialCommand::Undo()
{
	if (Editor::GetEditor()->IsDocumentOpen(myDocument))
		myDocument->SetMaterialAsset(myOldValue);
}
