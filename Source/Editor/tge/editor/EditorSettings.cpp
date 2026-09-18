#include <tge/editor/EditorSettings.h>

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>

using namespace Tga;

namespace
{
	// Same <exe folder>/settings/ convention Tga::LoadSettings (settings.cpp)
	// uses for the project settings file -- duplicated here rather than
	// exposing a new public accessor on that system for one caller.
	std::filesystem::path SettingsFilePath()
	{
		wchar_t exePath[MAX_PATH]{};
		if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
			return {};
		return std::filesystem::path(exePath).parent_path() / "settings" / "EditorUserSettings.json";
	}
}

EditorSettings& EditorSettings::Get()
{
	static EditorSettings instance;
	return instance;
}

void EditorSettings::Load()
{
	EditorSettings& s = Get();
	s = EditorSettings{};   // reset to defaults; a missing/corrupt file just keeps these

	std::ifstream in(SettingsFilePath());
	if (!in) return;

	try
	{
		nlohmann::json j;
		in >> j;
		if (!j.is_object()) return;

		if (auto it = j.find("viewportGridVisible"); it != j.end() && it->is_boolean()) s.viewportGridVisible = it->get<bool>();
		if (auto it = j.find("viewportCollisionVisible"); it != j.end() && it->is_boolean()) s.viewportCollisionVisible = it->get<bool>();

		if (auto it = j.find("fbxImport"); it != j.end() && it->is_object())
		{
			const auto& f = *it;
			if (auto v = f.find("normalsOpenGL"); v != f.end() && v->is_boolean()) s.fbxNormalsOpenGL = v->get<bool>();
			if (auto v = f.find("flipGreen"); v != f.end() && v->is_boolean()) s.fbxFlipGreen = v->get<bool>();
			if (auto v = f.find("recursive"); v != f.end() && v->is_boolean()) s.fbxRecursive = v->get<bool>();
			if (auto v = f.find("sourceFolder"); v != f.end() && v->is_string() && !v->get<std::string>().empty()) s.fbxSourceFolder = v->get<std::string>();
			if (auto v = f.find("cookedFolder"); v != f.end() && v->is_string() && !v->get<std::string>().empty()) s.fbxCookedFolder = v->get<std::string>();
			if (auto v = f.find("materialFolder"); v != f.end() && v->is_string() && !v->get<std::string>().empty()) s.fbxMaterialFolder = v->get<std::string>();
		}

		if (auto it = j.find("snap"); it != j.end() && it->is_object())
		{
			const auto& snap = *it;
			if (auto v = snap.find("snapPosEnabled"); v != snap.end() && v->is_boolean()) s.snapPosEnabled = v->get<bool>();
			if (auto v = snap.find("snapRotEnabled"); v != snap.end() && v->is_boolean()) s.snapRotEnabled = v->get<bool>();
			if (auto v = snap.find("snapScaleEnabled"); v != snap.end() && v->is_boolean()) s.snapScaleEnabled = v->get<bool>();
			if (auto v = snap.find("snapPosAmount"); v != snap.end() && v->is_number()) s.snapPosAmount = v->get<float>();
			if (auto v = snap.find("snapRotAmount"); v != snap.end() && v->is_number()) s.snapRotAmount = v->get<float>();
			if (auto v = snap.find("snapScaleAmount"); v != snap.end() && v->is_number()) s.snapScaleAmount = v->get<float>();
		}
	}
	catch (...) { /* keep defaults already assigned above */ }
}

void EditorSettings::Save()
{
	const EditorSettings& s = Get();
	nlohmann::json j = {
		{"version", 1},
		{"viewportGridVisible", s.viewportGridVisible},
		{"viewportCollisionVisible", s.viewportCollisionVisible},
		{"fbxImport", {
			{"normalsOpenGL", s.fbxNormalsOpenGL},
			{"flipGreen", s.fbxFlipGreen},
			{"recursive", s.fbxRecursive},
			{"sourceFolder", s.fbxSourceFolder},
			{"cookedFolder", s.fbxCookedFolder},
			{"materialFolder", s.fbxMaterialFolder},
		}},
		{"snap", {
			{"snapPosEnabled", s.snapPosEnabled},
			{"snapRotEnabled", s.snapRotEnabled},
			{"snapScaleEnabled", s.snapScaleEnabled},
			{"snapPosAmount", s.snapPosAmount},
			{"snapRotAmount", s.snapRotAmount},
			{"snapScaleAmount", s.snapScaleAmount},
		}},
	};

	const std::filesystem::path path = SettingsFilePath();
	if (path.empty()) return;
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);

	std::ofstream out(path);
	if (out) out << j.dump(2) << "\n";
}
