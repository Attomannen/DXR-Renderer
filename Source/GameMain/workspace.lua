include "../../Premake/extensions.lua"
-- Included up here, not below the workspace block: the workspace needs
-- dirs.solutions for its location.
include "../../Premake/common.lua"

workspace "Game"
	location (dirs.solutions)
	startproject "GameMain"
	architecture "x64"
	toolset "v145"

	configurations {
		"Debug",
		"Release",
		"Retail"
	}


include (dirs.game)
include "."

group "Engine"
include (dirs.external)
include (dirs.jolt)
include (dirs.physics)
include (dirs.application)
include (dirs.graphics)
include (dirs.core)
include (dirs.scene_script_core)

group "Tools"
include (dirs.texture_cooker)

