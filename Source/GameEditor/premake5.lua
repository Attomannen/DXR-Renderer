include "../../Premake/common.lua"

-------------------------------------------------------------
local projectname = "GameEditor"
project (projectname)
	location (dirs.projectfiles)
	dependson { "Core", "External", "Application", "Game", "GameMain", "Editor" }
		
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"

	debugdir "%{dirs.bin}"
	targetdir ("%{dirs.bin}")
	targetname("%{prj.name}_%{cfg.buildcfg}")
	objdir ("%{dirs.temp}/%{prj.name}/%{cfg.buildcfg}")

	links {"Core", "External", "Application", "Game", "Editor", "EditorDefaultGraphics" }
	includedirs { 
		dirs.core,
		dirs.external, 
		dirs.external .. "spdlog/include",
		dirs.application, 
		dirs.editor_default_graphics, 
		dirs.game .. "/source", 
		dirs.editor,	
	}


	files {
		"source/**.h",
		"source/**.cpp",
	}

	libdirs { dirs.lib, dirs.dependencies }

	defines
	{
		"TGE_PROJECT_SETTINGS_FILE=\"Game.json\""
	}

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
		linkoptions { "/FUNCTIONPADMIN" }
		linkoptions { "/OPT:NOREF" }
		linkoptions { "/OPT:NOICF" }
		linkoptions { "/DEBUG:FULL" }