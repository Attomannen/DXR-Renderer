include "../../Premake/extensions.lua"
-- Included up here, not below the workspace block: the workspace needs
-- dirs.solutions for its location.
include "../../Premake/common.lua"

workspace "GameEditor"
	location (dirs.solutions)
	startproject "GameEditor"
	architecture "x64"
	toolset "v145"

	configurations {
		"Debug",
		"Release",
		"Retail"
	}


include (dirs.game)
include (dirs.gamemain)
include "."

group "Engine"
include (dirs.external)
include (dirs.jolt)
include (dirs.physics)
include (dirs.application)
include (dirs.graphics)
include (dirs.core)
include (dirs.scene_script_core)

group "Editor"
include (dirs.editor)
include (dirs.editor_default_graphics)

group "Tools"
include (dirs.texture_cooker)
