#pragma once

#include <age/editor/CommandManager/AbstractCommand.h>
#include <age/particles/ParticleAsset.h>

namespace Ag
{
	class ParticleSystemDocument;

	// One command holds the whole system before and after an edit. A stack edit (reorder, add, remove) touches
	// several fields at once, and a snapshot of a small asset is cheaper than tracking each one.
	class ChangeParticleSystemCommand : public AbstractCommand
	{
	public:
		ChangeParticleSystemCommand(ParticleSystemDocument& aDocument, const Particles::SystemAsset& aNewValue, const Particles::SystemAsset& anOldValue);

		void Execute() override;
		void Undo() override;
		const char* GetName() const override { return "Edit Particle System"; }

	private:
		ParticleSystemDocument* myDocument;
		Particles::SystemAsset myNewValue;
		Particles::SystemAsset myOldValue;
	};
}
