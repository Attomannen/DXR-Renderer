#pragma once

#include <tge/editor/CommandManager/AbstractCommand.h>
#include <tge/editor/Material/MaterialAsset.h>

namespace Tga
{
	class MaterialDocument;

	// MaterialDocument isn't a Scene/SceneObject, so this derives directly
	// from AbstractCommand rather than SceneCommandBase (whose
	// GetModifiedObjects is scene-dirty-tracking machinery that doesn't
	// apply here -- MaterialDocument tracks its own dirty state through
	// Document::myUndoStackSize, driven by OnAction, same as SceneDocument).
	//
	// One command captures the *whole* MaterialAsset before/after rather
	// than a single field: DrawProperties() has ~20 heterogeneous widgets
	// (colours, floats, combo-selected strings, texture paths) sharing one
	// combined `changed` flag, not per-field tracking, so a whole-struct
	// snapshot is the natural fit -- it costs a couple of small copies per
	// edit, not per frame, and needs no new field-tag enum every time a
	// property is added to MaterialAsset.
	class ChangeMaterialCommand : public AbstractCommand
	{
	public:
		ChangeMaterialCommand(MaterialDocument& aDocument, const MaterialAsset& aNewValue, const MaterialAsset& aOldValue);

		void Execute() override;
		void Undo() override;
		const char* GetName() const override { return "Edit Material"; }

	private:
		MaterialDocument* myDocument;
		MaterialAsset myNewValue;
		MaterialAsset myOldValue;
	};
}
