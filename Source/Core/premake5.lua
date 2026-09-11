include "../../Premake/common.lua"

-------------------------------------------------------------
project "Core"
	location (dirs.projectfiles)
	dependson { "External" }
		
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	
	pchheader "stdafx.h"
	pchsource "stdafx.cpp"
	
	debugdir "%{dirs.bin}"
	targetdir ("%{dirs.bin}")
	targetname("%{prj.name}_%{cfg.buildcfg}")
	objdir ("%{dirs.temp}/%{prj.name}/%{cfg.buildcfg}")

	links {"External"}

	includedirs { 
		".", 
		dirs.external, 
		dirs.external .. "imgui/",
		}

	files {
		"**.h",
		"**.cpp",
	}

	libdirs { dirs.lib, dirs.dependencies }

	verify_or_create_settings("Core")
	flags { "MultiProcessorCompile" }
	 
	filter "configurations:Debug"
		defines {"_DEBUG"}
		runtime "Debug"
		symbols "on"
		files {"tools/**"}
		includedirs {"tools/"}
		warnings "Extra"
		flags { 
		--	"FatalWarnings", -- would be both compile and lib, the original didn't set lib
			"FatalCompileWarnings",
		}
	filter "configurations:Release"
		defines "_RELEASE"
		runtime "Release"
		optimize "on"
		files {"tools/**"}
		includedirs {"tools/"}
		warnings "Extra"
		flags { 
		--	"FatalWarnings", -- would be both compile and lib, the original didn't set lib
			"FatalCompileWarnings",
		}
	filter "configurations:Retail"
		defines "_RETAIL"
		runtime "Release"
		optimize "on"

	filter "system:windows"
		staticruntime "off"
		systemversion "latest"
		
		defines {
			"WIN32",
			"_LIB", 
			"TGE_SYSTEM_WINDOWS",
			"_CRT_SECURE_NO_WARNINGS", 
		}
	-- Options to support Live++ editing of code
	filter { "system:windows", "not configurations:Retail" }
		editandcontinue "Off"
		buildoptions { "/Gm-" }
		buildoptions { "/Gy" }
		buildoptions { "/Gw" }
