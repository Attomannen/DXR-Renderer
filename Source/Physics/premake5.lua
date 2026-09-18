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
	-- includedirs, not externalincludedirs. Premake emits the latter as
	-- MSBuild's ExternalIncludePath, which only reaches the compiler as
	-- /external:I -- the ordinary include search never sees it, so
	-- #include <Jolt/...> is not found. Jolt's own project uses includedirs
	-- for the same path and builds fine.
	includedirs { dirs.jolt }
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
