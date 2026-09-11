#include <stdafx.h>
#include "settings.h"

#include <filesystem>

#include <nlohmann/json.hpp>
#include <fstream>

#include <tge/util/StringCast.h>
#include <tge/stringRegistry/StringRegistry.h>

namespace fs = std::filesystem;

namespace Tga
{
	namespace Settings
	{
		static std::string locEngineAssetsPath;
		static std::string locGameAssetsPath;
		static std::string locCookedAssetsPath;

		static std::string locExecutableFolderPath;

		static ApplicationConfiguration locWindowParams;
	}
}


const std::string& Tga::Settings::EngineAssetRoot()
{
	return Tga::Settings::locEngineAssetsPath;
}
const std::string& Tga::Settings::GameAssetRoot()
{
	return Tga::Settings::locGameAssetsPath;
}
const std::string& Tga::Settings::CookedAssetRoot()
{
	return Tga::Settings::locCookedAssetsPath;
}

Tga::ApplicationConfiguration& Tga::Settings::GetApplicationConfiguration()
{
	return locWindowParams;
}

bool Tga::Settings::ResolveAssetPath(std::string_view anAsset, FilePathStream& outResolvedPath)
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

std::string Tga::Settings::ResolveAssetPath(std::string_view anAsset)
{
	FilePathStream resolved;
	if (ResolveAssetPath(anAsset, resolved))
	{
		return std::string(resolved.GetStringView());
	}
	return "";
}

bool Tga::Settings::ResolveEngineAssetPath(std::string_view anAsset, FilePathStream& outResolvedPath)
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

std::string Tga::Settings::ResolveEngineAssetPath(std::string_view anAsset)
{
	FilePathStream resolved;
	if (ResolveEngineAssetPath(anAsset, resolved))
	{
		return std::string(resolved.GetStringView());
	}
	return "";
}

bool Tga::Settings::ResolveGameAssetPath(std::string_view anAsset, FilePathStream& outResolvedPath)
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

std::string Tga::Settings::ResolveGameAssetPath(std::string_view anAsset)
{
	FilePathStream resolved;
	if (ResolveGameAssetPath(anAsset, resolved))
	{
		return std::string(resolved.GetStringView());
	}
	return "";

}
bool Tga::Settings::ResolveCookedAssetPath(std::string_view anAsset, FilePathStream& outResolvedPath)
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

std::string Tga::Settings::ResolveCookedAssetPath(std::string_view anAsset)
	{
	FilePathStream resolved;
	if (ResolveCookedAssetPath(anAsset, resolved))
	{
		return std::string(resolved.GetStringView());
	}
	return "";
}

bool Tga::LoadSettings(const std::string& aProjectName)
{
	using namespace Settings;
	using namespace nlohmann;

	WCHAR executablePathWString[MAX_PATH]{ 0 };
	// Game settings
	if (!GetModuleFileName(NULL, executablePathWString, sizeof(executablePathWString)))
	{
		assert(false && "GetModuleFileName failed in Tga::LoadSettings");
		return false;
	}

	std::filesystem::path executablePath(executablePathWString);
	std::filesystem::path executableFolderPath = executablePath.parent_path();

	locExecutableFolderPath = executableFolderPath.string();

	std::string executableFolder = locExecutableFolderPath;
	std::string settingsFolder = executableFolder + "\\settings\\";
	std::string filename = (aProjectName.find(".") == std::string::npos) ? (aProjectName + ".json") : aProjectName;
	std::string settingsFilepath = settingsFolder + filename;
	std::ifstream game_ifs(settingsFilepath.c_str());

	if (!game_ifs)
	{
		assert(false && "Could not open project settings file in Tga::LoadSettings");
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
			Settings::locWindowParams.clearColor = Tga::Color(app["r"], app["g"], app["b"], app["a"]);
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
