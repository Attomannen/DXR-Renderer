include "../../Premake/common.lua"

-- Jolt wrapper. Jolt types stay inside this project: public headers are Jolt-free.
project "Physics"
	location (dirs.projectfiles)
	dependson { "Jolt" }

	kind "StaticLib"
	language "C++"
	cppdialect "C++20"

	targetdir ("%{dirs.lib}")
	targetname("%{prj.name}_%{cfg.buildcfg}")
	objdir ("%{dirs.temp}/%{prj.name}/%{cfg.buildcfg}")

	includedirs { "." }
	externalincludedirs { dirs.jolt }
	links { "Jolt" }
	libdirs { dirs.lib }

	files {
		"**.h",
		"**.cpp",
	}

	jolt_defines()
	warnings "Extra"
	externalwarnings "Off"
	flags { "MultiProcessorCompile", "FatalCompileWarnings" }

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
		defines { "WIN32", "_LIB", "_CRT_SECURE_NO_WARNINGS" }
