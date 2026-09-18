#include <tge/editor/Material/ChangeMaterialCommand.h>

#include <tge/editor/Editor.h>
#include <tge/editor/Material/MaterialDocument.h>

using namespace Tga;

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
