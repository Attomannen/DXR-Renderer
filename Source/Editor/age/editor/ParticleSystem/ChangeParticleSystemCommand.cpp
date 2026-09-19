#include "stdafx.h"
#include <age/editor/ParticleSystem/ChangeParticleSystemCommand.h>

#include <age/editor/Editor.h>
#include <age/editor/ParticleSystem/ParticleSystemDocument.h>

using namespace Ag;

ChangeParticleSystemCommand::ChangeParticleSystemCommand(ParticleSystemDocument& aDocument, const Particles::SystemAsset& aNewValue, const Particles::SystemAsset& anOldValue)
	: myDocument(&aDocument)
	, myNewValue(aNewValue)
	, myOldValue(anOldValue)
{
}

void ChangeParticleSystemCommand::Execute()
{
	// The undo stack outlives documents, so the document may be gone.
	if (Editor::GetEditor()->IsDocumentOpen(myDocument))
		myDocument->SetAsset(myNewValue);
}

void ChangeParticleSystemCommand::Undo()
{
	if (Editor::GetEditor()->IsDocumentOpen(myDocument))
		myDocument->SetAsset(myOldValue);
}
