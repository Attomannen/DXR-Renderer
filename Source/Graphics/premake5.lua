include "../../Premake/common.lua"

-------------------------------------------------------------
project "Graphics"
	location (dirs.projectfiles)
	dependson { "External", "Application","Core" }
		
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	
	pchheader "stdafx.h"
	pchsource "stdafx.cpp"
	
	debugdir "%{dirs.bin}"
	targetdir ("%{dirs.bin}")
	targetname("%{prj.name}_%{cfg.buildcfg}")
	objdir ("%{dirs.temp}/%{prj.name}/%{cfg.buildcfg}")

	links {
		"External", 
		"Application", 
		"Core",
		"avcodec.lib",
		"avdevice.lib",
		"avfilter.lib",
		"avformat.lib",
		"avutil.lib",
		"swscale.lib",
		"swresample.lib",
		"NRD.lib"
	}

	includedirs {
		".", 
		dirs.external,
		dirs.external .. "DirectXTex/",
		dirs.external .. "ffmpeg-2.0/",
		dirs.application,
		dirs.core,
		dirs.root .. "NRD/NRD/NRD-4.17.3/_NRD_SDK/Include",
		dirs.root .. "NRD/NRD/NRD-4.17.3/_NRD_SDK/Integration",
		dirs.root .. "NRD/NRD/NRD-4.17.3/_Build/_deps/nri-src/Include",
	}

	files {
		"**.h",
		"**.cpp",
	}

	libdirs { dirs.lib, dirs.dependencies, dirs.root .. "NRD/NRD/NRD-4.17.3/_NRD_SDK/Lib/Release" }

	verify_or_create_settings("Graphics")
	 
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
			"WIN32_LEAN_AND_MEAN",
			"NOMINMAX",	
			"_LIB", 
			"AGE_SYSTEM_WINDOWS",
			"_CRT_SECURE_NO_WARNINGS",
		}
	-- Options to support Live++ editing of code
	filter { "system:windows", "not configurations:Retail" }
		editandcontinue "Off"
		buildoptions { "/Gm-" }
		buildoptions { "/Gy" }
		buildoptions { "/Gw" }
