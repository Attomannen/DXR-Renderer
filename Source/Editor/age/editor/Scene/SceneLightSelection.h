#pragma once

namespace Ag
{
	// The scene's sun + ambient (Scene::GetSunYaw() etc.) aren't SceneObjects --
	// there's no light scene-object type yet -- so they can't go through the
	// normal SceneSelection system, which is keyed to real scene-object ids.
	// This is the same minimal free-function-singleton shape as ActiveScene.h,
	// just for "which of the two fixed lighting pseudo-entries (if either) is
	// selected in the hierarchy panel right now". Selecting one of these should
	// clear the normal SceneSelection, and vice versa -- callers on both sides
	// are responsible for that (SceneObjectList.cpp does it at the selection
	// sites).
	enum class SceneLightSelection
	{
		None,
		Sun,
		Ambient,
		World,   // the level's own settings (its Game Mode)
	};

	SceneLightSelection GetSelectedSceneLight();
	void SetSelectedSceneLight(SceneLightSelection aSelection);
}
