----------------------------------------------------------------------------
-- the dirs table is a listing of absolute paths, since we generate projects
-- and files it makes a lot of sense to make them absolute to avoid problems
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
dirs = {}
dirs["root"] 			= os.realpath("../")
dirs["bin"]				= os.realpath(dirs.root .. "Bin/")
dirs["temp"]			= os.realpath(dirs.root .. "Temp/")
dirs["lib"]				= os.realpath(dirs.root .. "Lib/")
dirs["projectfiles"]	= os.realpath(dirs.root .. "Local/")
dirs["source"] 			= os.realpath(dirs.root .. "Source/")
dirs["dependencies"]	= os.realpath(dirs.root .. "Dependencies/")
dirs["external"]		= os.realpath(dirs.root .. "Source/External/")
dirs["application"]				= os.realpath(dirs.root .. "Source/Application")
dirs["core"]					= os.realpath(dirs.root .. "Source/Core")
dirs["editor_default_graphics"]	= os.realpath(dirs.root .. "Source/EditorDefaultGraphics")
dirs["editor"]					= os.realpath(dirs.root .. "Source/Editor")
dirs["graphics"]				= os.realpath(dirs.root .. "Source/Graphics")
dirs["scene_script_core"]		= os.realpath(dirs.root .. "Source/SceneScriptCore")
dirs["settings"]		= os.realpath(dirs.root .. "Bin/settings/")
dirs["engine_assets"] 	= os.realpath(dirs.root .. "EngineAssets/")
dirs["game"]			= os.realpath(dirs.root .. "Source/Game/")
dirs["gamemain"]		= os.realpath(dirs.root .. "Source/GameMain")
dirs["texture_cooker"]	= os.realpath(dirs.root .. "Source/TextureCooker")
dirs["jolt"]			= os.realpath(dirs.root .. "Source/External/Jolt")
dirs["physics"]			= os.realpath(dirs.root .. "Source/Physics")
dirs["cooked_assets"]	= os.realpath(dirs.root .. "Bin/CookedAssets/")
dirs["shader_dir"] 		= os.realpath(dirs.root .. "Bin/CookedAssets/Shaders/")

-----------------------------------------------------------------------
-- Jolt's headers change layout with these macros, so the SAME set must be
-- defined when building Jolt and when compiling anything that includes its
-- headers (Jolt checks this at startup and refuses to run on a mismatch).
-- Call it inside a project. Only the Physics project includes Jolt headers,
-- so nothing else in the engine needs it.
function jolt_defines()
	defines { "JPH_USE_SSE4_1", "JPH_USE_SSE4_2" }
	filter "configurations:Debug"
		defines { "JPH_ENABLE_ASSERTS" }
	-- Without NDEBUG (which this engine's Release does not define) Jolt turns on
	-- its slow debug-only checks; JPH_NO_DEBUG is its explicit switch for that.
	filter "configurations:not Debug"
		defines { "JPH_NO_DEBUG" }
	filter {}
end

application_settings = os.realpath(dirs.settings .. "/ApplicationSettings.json")

-----------------------------------------------------------------------
-- These should be more or less equivalent with Application WindowConfiguration struct
-- Some of it can't be set like this, things like callbacks.
function default_settings()
	return {
		assets_path = {
			engine = path.getrelative(dirs.bin, dirs.engine_assets) .. "/",
			game = path.getrelative(dirs.bin, os.realpath("./data/")) .. "/",
			cooked = path.getrelative(dirs.bin, dirs.cooked_assets) .. "/"
		},

		window_settings = {
			window_size = {w=1600, h=900},
			render_size = {w=1600, h=900},
			target_size = {w=1600, h=900},
			title = "TGE - Never give up on your dreams!",
	 		clear_color = {r=0.0, g=0.2, b=0.25, a=1.0},

			keep_aspect_ratio = true,
			aspect_ratio = 1.7,

	 		start_in_fullscreen = false,
			start_maximized = false,
			borderless = false
		},
		enable_vsync = true,
	}
end

if not os.isdir (dirs.bin) then
	os.mkdir (dirs.bin)
end

if not os.isdir(dirs.settings) then 
	os.mkdir (dirs.settings)
end

---------------------------------------------------------------------------
-- Utility function to create individual project-settings any game project, 
-- tutorial or similar is responsible for running:
-- Tga::LoadSettings("game_name.json"); at startup
function verify_or_create_settings(game_name)
	local settings_filename = game_name .. ".json"
	defines { 'TGE_PROJECT_SETTINGS_FILE="' .. settings_filename .. '"' }
	local game_settings = dirs["settings"] .. settings_filename
	
	local settings = default_settings()
	if os.isfile(game_settings) then
		local old_settings = json.decode(io.readfile(game_settings))
		for k,v in pairs(old_settings) do
			settings[k] = v
		end

		settings.assets_path.engine = path.getrelative(dirs.bin, dirs.engine_assets) .. "/"
		settings.assets_path.game = path.getrelative(dirs.bin, os.realpath("./data/")) .. "/"
		-- settings.assets_path.application = path.translate(dirs.application_assets, "/")
		-- settings.assets_path.game = path.translate(os.realpath("./data/"), "/")
	end

	io.writefile(
		game_settings,
		json.encode(settings)
	)
end
