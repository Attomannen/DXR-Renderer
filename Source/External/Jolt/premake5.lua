include "../../../Premake/common.lua"

-- Jolt Physics (jrouwe/JoltPhysics, MIT). Only its Jolt/ source folder is vendored.
-- A project of its own rather than part of External: its headers change layout with
-- the JPH_* defines, and those must match everywhere Jolt is included (see
-- jolt_defines() in Premake/common.lua).
project "Jolt"
	location (dirs.projectfiles)

	kind "StaticLib"
	language "C++"
	cppdialect "C++20"

	targetdir ("%{dirs.lib}")
	targetname("%{prj.name}_%{cfg.buildcfg}")
	objdir ("%{dirs.temp}/%{prj.name}/%{cfg.buildcfg}")

	includedirs { "." }

	files {
		"Jolt/**.h",
		"Jolt/**.inl",
		"Jolt/**.cpp",
	}

	jolt_defines()
	flags { "MultiProcessorCompile" }

	filter "configurations:Debug"
		defines { "_DEBUG" }
		runtime "Debug"
		symbols "on"
	filter "configurations:Release"
		defines { "_RELEASE" }
		runtime "Release"
		optimize "on"
	filter "configurations:Retail"
		defines { "_RETAIL" }
		runtime "Release"
		optimize "on"

	filter "system:windows"
		staticruntime "off"
		systemversion "latest"
		defines { "WIN32", "_LIB" }
