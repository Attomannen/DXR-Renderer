include "../../Premake/extensions.lua"
-- Included up here, not below the workspace block: the workspace needs
-- dirs.solutions for its location.
include "../../Premake/common.lua"

workspace "TextureCooker"
	location (dirs.solutions)
	startproject "TextureCooker"
	architecture "x64"
	toolset "v145"

	configurations {
		"Debug",
		"Release",
		"Retail",
	}


group "Engine"
include (dirs.external)

group "Tools"
include "."
