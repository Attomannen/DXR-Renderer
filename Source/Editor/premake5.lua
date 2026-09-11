
include "../../Premake/common.lua"
local projectname = "Editor"

project (projectname)
    dependson {"External", "Application"}

	location "%{dirs.projectfiles}"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"

	debugdir "%{dirs.bin}"
	targetdir ("%{dirs.bin}")
	targetname("%{prj.name}_%{cfg.buildcfg}")
	objdir ("%{dirs.temp}/%{prj.name}/%{cfg.buildcfg}")

	files {
		"**.h",
		"**.cpp",
		"**.hlsl",
		"**.hlsli",
	}

	includedirs {
		".",
		dirs.application,
		dirs.core,
		dirs.scene_script_core,
		dirs.external,
		dirs.external .. "ffmpeg-2.0/",
		dirs.external .. "spdlog/include/",
		dirs.external .. "imgui/",
		dirs.external .. "ImGuizmo"
	}

	shaderincludedirs {
		dirs.application,
		dirs.core,
		dirs.scene_script_core,
		dirs.external,
		dirs.external .. "imgui/",
		dirs.external .. "ImGuizmo",
		"."
	}

	libdirs { 
		"%{dirs.dependencies}",
		"%{dirs.lib}"
	}

	links {
		"Application",
		"Core",
		"SceneScriptCore",
		"External",
		"Comdlg32.lib"
	}

	defines {"_CONSOLE"}
	
	filter "configurations:Debug"
		defines {"_DEBUG"}
		runtime "Debug"
		symbols "on"
		
	filter "configurations:Release"
		defines "_RELEASE"
		runtime "Release"
		optimize "on"

	filter "configurations:Retail"
		defines "_Retail"
		runtime "Release"
		optimize "on"

	systemversion "latest"
	
	filter "system:windows"
		symbols "On"		
		systemversion "latest"
		warnings "Extra"
		-- sdlchecks "true"
		--conformanceMode "On"
		--buildoptions { "/STACK:8000000" }
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