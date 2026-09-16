include "../../Premake/common.lua"

-------------------------------------------------------------
-- TextureCooker: source images -> TGA-standard packed DDS
--   _c  BC7 sRGB      RGB=BaseColor  A=Opacity
--   _m  BC7 linear    R=AO  G=Roughness  B=Metalness
--   _n  BC5 linear    R=Normal.X  G=Normal.Y
--   _fx BC7 linear    R=Emissive mask  G=Height/Displacement
--   bare .hdr -> mipped R16G16B16A16_FLOAT environment DDS
-------------------------------------------------------------
project "TextureCooker"
	location (dirs.projectfiles)
	dependson { "External" }

	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"

	debugdir "%{dirs.bin}"
	targetdir ("%{dirs.bin}")
	targetname("%{prj.name}_%{cfg.buildcfg}")
	objdir ("%{dirs.temp}/%{prj.name}/%{cfg.buildcfg}")

	files {
		"source/**.h",
		"source/**.cpp",
	}

	includedirs {
		dirs.external,
		dirs.external .. "DirectXTex/",
		dirs.external .. "DirectXTex/DirectXTex/",
		"source/",
	}

	libdirs {
		"%{dirs.dependencies}",
		"%{dirs.lib}",
	}

	links {
		"External",
		"d3d11",
		"DXGI",
		"dxguid",
		"windowscodecs",
		"ole32",
		"uuid",
	}

	-- Third-party heavy headers (DirectXTex / ufbx): don't let their warnings break us.
	warnings "Off"
	systemversion "latest"

	defines {
		"_CONSOLE",
		"NOMINMAX",
		"WIN32_LEAN_AND_MEAN",
		"_WIN32_WINNT=0x0601",
	}

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
		defines { "WIN32", "TGE_SYSTEM_WINDOWS" }
