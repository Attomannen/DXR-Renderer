include "../../Premake/extensions.lua"

workspace "Game"
	location "../../"
	startproject "GameMain"
	architecture "x64"
	toolset "v145"

	configurations {
		"Debug",
		"Release",
		"Retail"
	}

-- include for common stuff 
include "../../Premake/common.lua"

include (dirs.game)
include "."

group "Engine"
include (dirs.external)
include (dirs.application)
include (dirs.graphics)
include (dirs.core)
include (dirs.scene_script_core)

group "Tools"
include (dirs.texture_cooker)

