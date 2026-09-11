include "../../Premake/common.lua"

-------------------------------------------------------------
project "SceneScriptCore"
	location (dirs.projectfiles)
	dependson { "External", "Core" }
		
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	
	pchheader "stdafx.h"
	pchsource "stdafx.cpp"
	
	debugdir "%{dirs.bin}"
	targetdir ("%{dirs.bin}")
	targetname("%{prj.name}_%{cfg.buildcfg}")
	objdir ("%{dirs.temp}/%{prj.name}/%{cfg.buildcfg}")

	links {"External", "Core" }

	includedirs { ".", dirs.external, dirs.core }

	files {
		"**.h",
		"**.cpp",
	}

	libdirs { dirs.lib, dirs.dependencies }

	verify_or_create_settings("SceneScriptCore")
	 
	filter "configurations:Debug"
		defines {"_DEBUG"}
		runtime "Debug"
		symbols "on"
		files {"tools/**"}
		includedirs {"tools/"}
	filter "configurations:Release"
		defines "_RELEASE"
		runtime "Release"
		optimize "on"
		files {"tools/**"}
		includedirs {"tools/"}
	filter "configurations:Retail"
		defines "_RETAIL"
		runtime "Release"
		optimize "on"

	filter "system:windows"
--		kind "StaticLib"
		staticruntime "off"
		symbols "On"		
		systemversion "latest"
		warnings "Extra"
		--conformanceMode "On"
		--buildoptions { "/permissive" }
		flags { 
		--	"FatalWarnings", -- would be both compile and lib, the original didn't set lib
			"FatalCompileWarnings",
			"MultiProcessorCompile"
		}
		
		defines {
			"WIN32",
			"_LIB", 
			"TGE_SYSTEM_WINDOWS" 
		}
	-- Options to support Live++ editing of code
	filter { "system:windows", "not configurations:Retail" }
		editandcontinue "Off"
		buildoptions { "/Gm-" }
		buildoptions { "/Gy" }
		buildoptions { "/Gw" }
