include "../../Premake/common.lua"

-------------------------------------------------------------
local projectname = "GameEditor"
project (projectname)
	location (dirs.projectfiles)
	dependson { "Core", "External", "Application", "Game", "GameMain", "Editor" }
		
	-- WindowedApp so no console window is spawned alongside the game; the log
	-- lives in the in-engine Console panel (grave key) instead. mainCRTStartup
	-- keeps the ordinary int main(argc, argv) entry point and its arguments,
	-- which the bench harness relies on.
	kind "WindowedApp"
	entrypoint "mainCRTStartup"
	language "C++"
	cppdialect "C++20"

	debugdir "%{dirs.bin}"
	targetdir ("%{dirs.bin}")
	targetname("%{prj.name}_%{cfg.buildcfg}")
	objdir ("%{dirs.temp}/%{prj.name}/%{cfg.buildcfg}")

	links {"Core", "External", "Application", "Game", "Editor", "EditorDefaultGraphics", "NRD.lib", "NRI.lib"}
	
	libdirs { 
		dirs.lib, 
		dirs.dependencies,
		dirs.nrd .. "_NRD_SDK/Lib/Release",
		dirs.nrd .. "_NRI_SDK/Lib/Release",
		dirs.nrd .. "_Bin/Release"
	}
	
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
		"source/**.rc",
	}

	-- NRD's NRI needs Agility SDK 619; the system D3D12 runtime is older and the
	-- first denoiser dispatch faults inside NRI without it. main.cpp exports
	-- D3D12SDKVersion/D3D12SDKPath pointing here, so the redistributable has to
	-- sit next to the executable -- see GameMain/premake5.lua, where this was
	-- first diagnosed and fixed.
	postbuildcommands {
		'{MKDIR} "%{dirs.bin}/AgilitySDK"',
		'{COPYFILE} "%{dirs.nrd}/_Bin/Release/AgilitySDK/D3D12Core.dll" "%{dirs.bin}/AgilitySDK/"',
		'{COPYFILE} "%{dirs.nrd}/_Bin/Release/AgilitySDK/d3d12SDKLayers.dll" "%{dirs.bin}/AgilitySDK/"',
	}

	defines
	{
		"AGE_PROJECT_SETTINGS_FILE=\"Game.json\""
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
			"AGE_SYSTEM_WINDOWS" 
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