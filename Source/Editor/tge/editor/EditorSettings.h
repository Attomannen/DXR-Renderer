#pragma once

#include <string>

namespace Tga
{
	// Per-USER editor preferences -- distinct from tge/settings/settings.h,
	// which is per-PROJECT data (asset roots, window size) loaded once at
	// startup and never written back by the editor. This is small, editor-
	// only, and changes constantly as someone works (grid toggle, snap
	// amounts), so it gets its own file and its own save-on-change calls
	// instead of being folded into the project settings.
	//
	// Lives at <exe folder>/settings/EditorUserSettings.json, next to the
	// project settings file (Tga::LoadSettings), for the same reason that
	// file lives there: this is a single-user local dev tool with no
	// roaming-profile requirement, so there is no reason to diverge from
	// the convention already established for the project-level file.
	struct EditorSettings
	{
		bool viewportGridVisible = true;
		bool viewportCollisionVisible = false;

		// Gizmos::Snap's fields, duplicated flat rather than reusing that
		// type: Gizmos.h would need to include this header to seed a new
		// instance's snap settings, and this header including Gizmos.h back
		// would make the two headers circular.
		bool snapPosEnabled = false;
		bool snapRotEnabled = false;
		bool snapScaleEnabled = false;
		float snapPosAmount = 100.f;
		float snapRotAmount = 45.f;
		float snapScaleAmount = 0.1f;

		// FBX conversion: the values last used in the Convert dialog. They apply
		// to any model that has no .tgm sidecar of its own. Folders are names
		// relative to the FBX's own folder, so they carry over between models.
		bool fbxNormalsOpenGL = false;       // DirectX unless told otherwise
		bool fbxFlipGreen = false;
		bool fbxRecursive = true;
		std::string fbxSourceFolder = "Textures/Source";
		std::string fbxCookedFolder = "Textures";
		std::string fbxMaterialFolder = "Materials";

		static EditorSettings& Get();

		// Called once from Editor::Init(). Missing/corrupt file -> defaults,
		// same tolerant-load convention FbxConvert.cpp uses.
		static void Load();
		// Called from each UI site that changes a tracked setting (the View
		// menu's grid toggle, Gizmos' snap widgets) right after the edit,
		// not on a timer or on exit -- there is no "close the editor
		// cleanly" guarantee (crashes, task-killed processes), so waiting
		// to save would lose changes the same way process-local statics
		// already did.
		static void Save();
	};
}
