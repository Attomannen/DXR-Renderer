include "../../Premake/extensions.lua"

workspace "TextureCooker"
	location "../../"
	startproject "TextureCooker"
	architecture "x64"
	toolset "v145"

	configurations {
		"Debug",
		"Release",
		"Retail",
	}

include "../../Premake/common.lua"

group "Engine"
include (dirs.external)

group "Tools"
include "."
