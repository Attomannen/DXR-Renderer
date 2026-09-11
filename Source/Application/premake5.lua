include "../../Premake/common.lua"

project "Application"
	location (dirs.projectfiles)

	language "C++"
	cppdialect "C++20"

	targetdir (dirs.lib)
	targetname("%{prj.name}_%{cfg.buildcfg}")
	objdir ("%{dirs.temp}/%{prj.name}/%{cfg.buildcfg}")

	pchheader "stdafx.h"
	pchsource "stdafx.cpp"

	files {
		"**.h",
		"**.cpp",
		"**.hlsl",
		"**.hlsli",
	}
	links {"External", "Core"}

	includedirs {
		".",
		dirs.external,
		dirs.external .. "imgui/",
		dirs.external .. "DirectXTex/",
		dirs.external .. "ffmpeg-2.0/",
		dirs.external .. "spdlog/include/",
		dirs.core,
	}
	libdirs { dirs.dependencies }

	flags { 
		"MultiProcessorCompile"
	}
	filter "configurations:Debug"
		defines {"_DEBUG"}
		runtime "Debug"
		symbols "on"
		warnings "Extra"
		flags { 
		--	"FatalWarnings", -- would be both compile and lib, the original didn't set lib
			"FatalCompileWarnings",
		}
	filter "configurations:Release"
		defines "_RELEASE"
		runtime "Release"
		optimize "on"
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
		kind "StaticLib"
		staticruntime "off"
		systemversion "latest"
		sdlchecks "true"
		links {
			"DXGI",
		}

		defines {
			"WIN32",
			"_CRT_SECURE_NO_WARNINGS", 
			"_LIB", 
			"_WIN32_WINNT=0x0601",
			"TGE_SYSTEM_WINDOWS" 
		}

	shadermodel("5.0")

	-- Options to support Live++ editing of code
	filter { "system:windows", "not configurations:Retail" }
		editandcontinue "Off"
		buildoptions { "/Gm-" }
		buildoptions { "/Gy" }
		buildoptions { "/Gw" }

	filter("files:**.hlsl")
		flags("ExcludeFromBuild")
		shaderobjectfileoutput(dirs.shader_dir.."%{file.basename}"..".cso")

	filter("files:**PS.hlsl")
		removeflags("ExcludeFromBuild")
		shadertype("Pixel")

	filter("files:**VS.hlsl")
		removeflags("ExcludeFromBuild")
		shadertype("Vertex")

	filter("files:**GS.hlsl")
		removeflags("ExcludeFromBuild")
		shadertype("Geometry")

	-- Warnings as errors
	shaderoptions({"/WX"})
