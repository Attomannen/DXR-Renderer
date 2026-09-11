include "../../Premake/common.lua"

-------------------------------------------------------------
local projectname = "GameMain"
project (projectname)
	location (dirs.projectfiles)
	dependson { "External", "Application" }
		
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"

	debugdir "%{dirs.bin}"
	targetdir ("%{dirs.bin}")
	targetname("%{prj.name}_%{cfg.buildcfg}")
	objdir ("%{dirs.temp}/%{prj.name}/%{cfg.buildcfg}")

	links {"External", "Application", "Game"}

	includedirs { 
		dirs.external, 
		dirs.external .. "ffmpeg-2.0/",
		dirs.application, 
		dirs.game .. "/source"
	}

	files {
		"source/**.h",
		"source/**.cpp",
	}

	libdirs { dirs.lib, dirs.dependencies }
	
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