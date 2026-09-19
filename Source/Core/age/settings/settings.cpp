#include <stdafx.h>
#include "settings.h"

#include <filesystem>

#include <nlohmann/json.hpp>
#include <fstream>

#include <age/util/StringCast.h>
#include <age/stringRegistry/StringRegistry.h>

namespace fs = std::filesystem;

namespace Ag
{
	namespace Settings
	{
		static std::string locEngineAssetsPath;
		static std::string locGameAssetsPath;
		static std::string locCookedAssetsPath;

		static std::string locExecutableFolderPath;
		// The settings file this run loaded, so it can be written back.
		static std::string locSettingsFilepath;

		static ApplicationConfiguration locWindowParams;
	}
}


const std::string& Ag::Settings::EngineAssetRoot()
{
	return Ag::Settings::locEngineAssetsPath;
}
const std::string& Ag::Settings::GameAssetRoot()
{
	return Ag::Settings::locGameAssetsPath;
}
const std::string& Ag::Settings::CookedAssetRoot()
{
	return Ag::Settings::locCookedAssetsPath;
}

Ag::ApplicationConfiguration& Ag::Settings::GetApplicationConfiguration()
{
	return locWindowParams;
}

bool Ag::Settings::ResolveAssetPath(std::string_view anAsset, FilePathStream& outResolvedPath)
{
	outResolvedPath.Clear();
	if (anAsset.empty())
		return false;

	if (!locExecutableFolderPath.empty())
	{
		outResolvedPath << locExecutableFolderPath << "/data/" << anAsset;
		outResolvedPath.NormalizePath();
		if (fs::exists(outResolvedPath.GetData()))
		{
			return true;
	}
		outResolvedPath.Clear();
	}

	if (!locGameAssetsPath.empty())
	{
		outResolvedPath << locGameAssetsPath << "/" << anAsset;
		outResolvedPath.NormalizePath();
		if (fs::exists(outResolvedPath.GetData()))
		{
			return true;
	}
		outResolvedPath.Clear();
	}

	if (!locEngineAssetsPath.empty())
	{
		outResolvedPath << locEngineAssetsPath << "/" << anAsset;
		outResolvedPath.NormalizePath();
		if (fs::exists(outResolvedPath.GetData()))
		{
			outResolvedPath = outResolvedPath;
			return true;
		}
		outResolvedPath.Clear();
	}

	return false;
	}

std::string Ag::Settings::ResolveAssetPath(std::string_view anAsset)
{
	FilePathStream resolved;
	if (ResolveAssetPath(anAsset, resolved))
	{
		return std::string(resolved.GetStringView());
	}
	return "";
}

bool Ag::Settings::ResolveEngineAssetPath(std::string_view anAsset, FilePathStream& outResolvedPath)
{
	outResolvedPath.Clear();

	if (!locExecutableFolderPath.empty())
	{
		outResolvedPath << locExecutableFolderPath << "/data/" << anAsset;
		outResolvedPath.NormalizePath();
		if (fs::exists(outResolvedPath.GetData()))
			return true;

		outResolvedPath.Clear();
	}
	if (!locEngineAssetsPath.empty())
	{
		outResolvedPath << locEngineAssetsPath << "/" << anAsset;
		outResolvedPath.NormalizePath();
		if (fs::exists(outResolvedPath.GetData()))
			return true;

		outResolvedPath.Clear();
	}
	return false;
	}

std::string Ag::Settings::ResolveEngineAssetPath(std::string_view anAsset)
{
	FilePathStream resolved;
	if (ResolveEngineAssetPath(anAsset, resolved))
	{
		return std::string(resolved.GetStringView());
	}
	return "";
}

bool Ag::Settings::ResolveGameAssetPath(std::string_view anAsset, FilePathStream& outResolvedPath)
{
	outResolvedPath.Clear();

	if (!locExecutableFolderPath.empty())
	{
		outResolvedPath << locExecutableFolderPath << "/data/" << anAsset;
		outResolvedPath.NormalizePath();
		if (fs::exists(outResolvedPath.GetData()))
			return true;

		outResolvedPath.Clear();
	}
	if (!locGameAssetsPath.empty())
	{
		outResolvedPath << locGameAssetsPath << "/" << anAsset;
		outResolvedPath.NormalizePath();
		if (fs::exists(outResolvedPath.GetData()))
			return true;

		outResolvedPath.Clear();
	}
	return false;
}

std::string Ag::Settings::ResolveGameAssetPath(std::string_view anAsset)
{
	FilePathStream resolved;
	if (ResolveGameAssetPath(anAsset, resolved))
	{
		return std::string(resolved.GetStringView());
	}
	return "";

}
bool Ag::Settings::ResolveCookedAssetPath(std::string_view anAsset, FilePathStream& outResolvedPath)
{
	outResolvedPath.Clear();

	if (!locExecutableFolderPath.empty())
{
		outResolvedPath << locExecutableFolderPath << "/CookedAssets/" << anAsset;
		outResolvedPath.NormalizePath();
		if (fs::exists(outResolvedPath.GetData()))
			return true;

		outResolvedPath.Clear();
	}
	if (!locCookedAssetsPath.empty())
	{
		outResolvedPath << locCookedAssetsPath << "/" << anAsset;
		outResolvedPath.NormalizePath();
		if (fs::exists(outResolvedPath.GetData()))
			return true;

		outResolvedPath.Clear();
	}
	return false;
}

std::string Ag::Settings::ResolveCookedAssetPath(std::string_view anAsset)
	{
	FilePathStream resolved;
	if (ResolveCookedAssetPath(anAsset, resolved))
	{
		return std::string(resolved.GetStringView());
	}
	return "";
}

bool Ag::LoadSettings(const std::string& aProjectName)
{
	using namespace Settings;
	using namespace nlohmann;

	WCHAR executablePathWString[MAX_PATH]{ 0 };
	// Game settings
	if (!GetModuleFileName(NULL, executablePathWString, sizeof(executablePathWString)))
	{
		assert(false && "GetModuleFileName failed in Ag::LoadSettings");
		return false;
	}

	std::filesystem::path executablePath(executablePathWString);
	std::filesystem::path executableFolderPath = executablePath.parent_path();

	locExecutableFolderPath = executableFolderPath.string();

	std::string executableFolder = locExecutableFolderPath;
	std::string settingsFolder = executableFolder + "\\settings\\";
	std::string filename = (aProjectName.find(".") == std::string::npos) ? (aProjectName + ".json") : aProjectName;
	std::string settingsFilepath = settingsFolder + filename;
	Settings::locSettingsFilepath = settingsFilepath;
	std::ifstream game_ifs(settingsFilepath.c_str());

	if (!game_ifs)
	{
		assert(false && "Could not open project settings file in Ag::LoadSettings");
		return false;
	}

	nlohmann::json game_settings;
	game_ifs >> game_settings;
	game_ifs.close();

	if (game_settings.contains("assets_path"))
	{
		locEngineAssetsPath = (executableFolderPath / game_settings["assets_path"]["engine"]).lexically_normal().string();
		locGameAssetsPath = (executableFolderPath / game_settings["assets_path"]["game"]).lexically_normal().string();

		// allow not specifying cooked path since it was added recently and the default value will work fine.
		if (game_settings["assets_path"].contains("cooked"))
			locCookedAssetsPath = (executableFolderPath / game_settings["assets_path"]["cooked"]).lexically_normal().string();
	}

	//////////////////////////////////////
	// Window Title
	{
		std::string app = game_settings["window_settings"]["title"];
		if (!app.empty()) {
			Settings::locWindowParams.applicationName = string_cast<std::wstring>(app);
		}
	}
	/////////////////////////////////////
	// Clear Color
	{
		auto& app = game_settings["window_settings"]["clear_color"];
		if (!app.is_null()) {
			Settings::locWindowParams.clearColor = Ag::Color(app["r"], app["g"], app["b"], app["a"]);
		}
	}
	/////////////////////////////////////
	// Window Width & Height
	{
		auto& window = game_settings["window_settings"]["window_size"];
		auto& render = game_settings["window_settings"]["render_size"];

		if (window.is_null() == false) {
			Settings::locWindowParams.windowSize.x = window["w"];
			Settings::locWindowParams.windowSize.y = window["h"];
			if (render.is_null()) {
				Settings::locWindowParams.renderSize.x = window["w"];
				Settings::locWindowParams.renderSize.y = window["h"];
			}
			else {
				Settings::locWindowParams.renderSize.x = render["w"];
				Settings::locWindowParams.renderSize.y = render["h"];
			}
		}
		else if (render.is_null() == false) {
			Settings::locWindowParams.renderSize.x = render["w"];
			Settings::locWindowParams.renderSize.y = render["h"];
			Settings::locWindowParams.windowSize.x = render["w"];
			Settings::locWindowParams.windowSize.y = render["h"];
		}
	}
	//////////////////////////////////////
	// VSync
	{
		auto& app = game_settings["enable_vsync"];
		if (!app.is_null()) {
			Settings::locWindowParams.enableVSync = app;
		}
	}
	//////////////////////////////////////
	// Streamline / upscaling
	{
		auto& app = game_settings["enable_upscaling"];
		if (!app.is_null()) {
			Settings::locWindowParams.enableUpscaling = app;
		}
	}
	/////////////////////////////////////
	// Start in fullscreen / Maximized
	{
		auto& app = game_settings["window_settings"]["start_in_fullscreen"];
		if (!app.is_null()) {
			Settings::locWindowParams.startInFullScreen = app;
		}
	}
	{
		auto& app = game_settings["window_settings"]["start_maximized"];
		if (!app.is_null()) {
			Settings::locWindowParams.startMaximized = app;
		}
	}

	/////////////////////////////////////
	// Window setting (overlapped/borderless)
	{
		auto& app = game_settings["window_settings"]["borderless"];
		if (!app.is_null()) {
			Settings::locWindowParams.borderless = app;
		}
	}
	{
		auto& app = game_settings["window_settings"]["keep_aspect_ratio"];
		if (!app.is_null()) {
			Settings::locWindowParams.keepAspectRatio = app;
		}
	}

	/////////////////////////////////////
	// Debug systems
	{
		auto& app = game_settings["debug_features"];
		DebugFeature dbg = static_cast<DebugFeature>(0);

		for (std::string flag : app) {
			if (flag == "All") { dbg = DebugFeature::All; break; }
			if (flag == "None") { dbg = DebugFeature::None; break; }
			if (flag == "Cpu") { dbg = dbg | DebugFeature::Cpu; continue; }
			if (flag == "Drawcalls") { dbg = dbg | DebugFeature::Drawcalls; continue; }
			if (flag == "Filewatcher") { dbg = dbg | DebugFeature::Filewatcher; continue; }
			if (flag == "Fps") { dbg = dbg | DebugFeature::Fps; continue; }
			if (flag == "FpsGraph") { dbg = dbg | DebugFeature::FpsGraph; continue; }
			if (flag == "Log") { dbg = dbg | DebugFeature::Log; continue; }
			if (flag == "Mem") { dbg = dbg | DebugFeature::Mem; continue; }
			if (flag == "MemoryTrackingAllAllocations") { dbg = dbg | DebugFeature::MemoryTrackingAllAllocations; continue; }
			if (flag == "MemoryTrackingStackTraces") { dbg = dbg | DebugFeature::MemoryTrackingStackTraces; continue; }
			if (flag == "OptimizeWarnings") { dbg = dbg | DebugFeature::OptimizeWarnings; continue; }
		}
		//if (dbg > static_cast<DebugFeature>(0))
		{
			Settings::locWindowParams.activateDebugSystems = dbg;
		}
	}

	/////////////////////////////////////
	// Multisampling Quality
	{
		auto& app = game_settings["multisampling"];

		for (std::string flag : app) {
			if (flag == "Off") {
				Settings::locWindowParams.preferedMultiSamplingQuality = MultiSamplingQuality::Off;
			}
			if (flag == "Low") {
				Settings::locWindowParams.preferedMultiSamplingQuality = MultiSamplingQuality::Low;
			}
			if (flag == "Medium") {
				Settings::locWindowParams.preferedMultiSamplingQuality = MultiSamplingQuality::Medium;
			}
			if (flag == "High") {
				Settings::locWindowParams.preferedMultiSamplingQuality = MultiSamplingQuality::High;
			}
		}
	}

	return true;
}

// Persist the upscaling flag to the settings file this run loaded.
//
// Streamline cannot be enabled at runtime: it interposes on DXGI/D3D12 and so
// has to load before the device is created (see Application::InternalStart).
// The honest thing the engine can offer is to record the choice and have it
// take effect on the next launch, which is what this does.
bool Ag::SetUpscalingEnabled(bool aEnabled)
{
	using namespace Settings;
	if (locSettingsFilepath.empty()) return false;

	nlohmann::json settings;
	{
		std::ifstream in(locSettingsFilepath.c_str());
		if (!in) return false;
		try { in >> settings; } catch (...) { return false; }
	}
	settings["enable_upscaling"] = aEnabled;
	{
		std::ofstream out(locSettingsFilepath.c_str());
		if (!out) return false;
		out << settings.dump(1, '	');
	}
	// The live value follows so the UI reflects the choice immediately, even
	// though nothing can act on it until restart.
	Settings::locWindowParams.enableUpscaling = aEnabled;
	return true;
}
