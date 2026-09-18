#include "stdafx.h"
#include <tge/editor/Import/FbxConvert.h>
#include <tge/editor/Editor.h>
#include <tge/editor/EditorSettings.h>
#include <tge/editor/FileDialog/FileDialog.h>
#include <tge/settings/settings.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <windows.h>
#include <imgui.h>

using namespace Tga;
namespace fs = std::filesystem;

namespace
{
	// The asset root without its trailing separator. Inside the quotes of a command
	// line a trailing backslash escapes the closing quote, which once swallowed every
	// argument after --game-root.
	fs::path AssetRoot()
	{
		fs::path root = fs::absolute(Settings::GameAssetRoot());
		if (!root.has_filename()) root = root.parent_path();
		return root;
	}

	std::string Quote(const fs::path& path)
	{
		std::string s = path.string();
		while (!s.empty() && (s.back() == '\\' || s.back() == '/')) s.pop_back();
		return "\"" + s + "\"";
	}

	fs::path AbsoluteAssetPath(const fs::path& root, const std::string& value)
	{
		fs::path result(value);
		return result.is_absolute() ? result : root / result;
	}

	// Runs a command with no window, its output going to a log file that starts with
	// the command line itself, and waits for it. Returns false if it couldn't launch.
	bool RunHidden(const std::string& command, const fs::path& logPath, DWORD& exitCode)
	{
		SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		HANDLE logHandle = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		STARTUPINFOW si{}; si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};
		if (logHandle != INVALID_HANDLE_VALUE)
		{
			const std::string header = "COMMAND: " + command + "\r\n";
			DWORD written = 0;
			WriteFile(logHandle, header.data(), (DWORD)header.size(), &written, nullptr);
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdOutput = logHandle;
			si.hStdError = logHandle;
		}
		// MultiByteToWideChar so non-ASCII asset paths survive CreateProcessW.
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), (int)command.size(), nullptr, 0);
		std::vector<wchar_t> cmd(wlen + 1, 0);
		MultiByteToWideChar(CP_UTF8, 0, command.c_str(), (int)command.size(), cmd.data(), wlen);
		const BOOL launched = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, logHandle != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW,
			nullptr, nullptr, &si, &pi);
		if (launched)
		{
			CloseHandle(pi.hThread);
			WaitForSingleObject(pi.hProcess, INFINITE);
			exitCode = 1;
			GetExitCodeProcess(pi.hProcess, &exitCode);
			CloseHandle(pi.hProcess);
		}
		if (logHandle != INVALID_HANDLE_VALUE) CloseHandle(logHandle);
		return launched != FALSE;
	}

	std::string LogTail(const fs::path& logPath)
	{
		std::ifstream in(logPath);
		std::vector<std::string> lines;
		for (std::string line; std::getline(in, line);)
			if (!line.empty() && line.rfind("COMMAND:", 0) != 0 && line.find("cooked ") == std::string::npos && line.find("compressing ") == std::string::npos)
				lines.push_back(line);
		std::string tail;
		for (size_t i = lines.size() > 8 ? lines.size() - 8 : 0; i < lines.size(); ++i)
			tail += "\n  " + lines[i];
		return tail;
	}

	bool InputString(const char* label, std::string& value)
	{
		char buffer[512]{};
		strncpy_s(buffer, value.c_str(), _TRUNCATE);
		if (!ImGui::InputText(label, buffer, sizeof(buffer))) return false;
		value = buffer;
		return true;
	}

	// Folder the FBX lives in, relative to the asset root.
	fs::path ModelFolder(const fs::path& absoluteFbx)
	{
		std::error_code ec;
		fs::path relative = fs::relative(absoluteFbx.parent_path(), AssetRoot(), ec);
		if (ec || relative == ".") return {};
		return relative;
	}

	// Previews are shown through the engine's TextureManager, which only resolves paths
	// relative to <exe folder>/data, the project or the engine assets -- never an
	// absolute path. So they live in <exe folder>/data/TGE_FbxPreview (outside the
	// project, so no asset scan or Asset Browser ever sees them) and are referred to
	// as "TGE_FbxPreview/<file>".
	constexpr const char* kPreviewSubfolder = "TGE_FbxPreview";
	fs::path TempPreviewFolder()
	{
		wchar_t module[MAX_PATH]{};
		GetModuleFileNameW(nullptr, module, MAX_PATH);
		return fs::path(module).parent_path() / "data" / kPreviewSubfolder;
	}
}

fs::path Tga::FindTextureCookerExe()
{
	wchar_t module[MAX_PATH]{};
	GetModuleFileNameW(nullptr, module, MAX_PATH);
	const fs::path dir = fs::path(module).parent_path();
#if defined(_DEBUG)
	const char* preferred = "TextureCooker_Debug.exe";
#elif defined(_RETAIL)
	const char* preferred = "TextureCooker_Retail.exe";
#else
	const char* preferred = "TextureCooker_Release.exe";
#endif
	if (fs::exists(dir / preferred)) return dir / preferred;
	for (const char* candidate : { "TextureCooker_Release.exe", "TextureCooker_Retail.exe", "TextureCooker_Debug.exe" })
		if (fs::exists(dir / candidate)) return dir / candidate;
	return {};
}

// ------------------------------------------------------------------ settings + sidecar
FbxImportSettings Tga::LoadFbxImportSettings(const fs::path& absoluteFbxPath)
{
	const EditorSettings& defaults = EditorSettings::Get();
	const fs::path folder = ModelFolder(absoluteFbxPath);

	FbxImportSettings s;
	s.normalsOpenGL = defaults.fbxNormalsOpenGL;
	s.flipGreen = defaults.fbxFlipGreen;
	s.recursive = defaults.fbxRecursive;
	s.sourceFolder = (folder / defaults.fbxSourceFolder).generic_string();
	s.cookedFolder = (folder / defaults.fbxCookedFolder).generic_string();
	s.materialFolder = (folder / defaults.fbxMaterialFolder).generic_string();

	fs::path sidecar = absoluteFbxPath;
	sidecar.replace_extension(".tgm");
	std::ifstream in(sidecar);
	if (!in) return s;
	try
	{
		nlohmann::json j;
		in >> j;
		if (!j.is_object()) return s;
		s.hasSidecar = true;
		if (j.contains("normalConvention") && j["normalConvention"].is_string()) s.normalsOpenGL = j["normalConvention"].get<std::string>() == "OpenGL";
		if (j.contains("flipGreen") && j["flipGreen"].is_boolean()) s.flipGreen = j["flipGreen"].get<bool>();
		if (j.contains("recursive") && j["recursive"].is_boolean()) s.recursive = j["recursive"].get<bool>();
		auto readFolder = [&j](const char* key, std::string& target)
		{
			if (j.contains(key) && j[key].is_string() && !j[key].get<std::string>().empty()) target = j[key].get<std::string>();
		};
		readFolder("sourceFolder", s.sourceFolder);
		readFolder("cookedFolder", s.cookedFolder);
		readFolder("materialFolder", s.materialFolder);
		if (j.contains("materialRemaps") && j["materialRemaps"].is_object())
			for (auto it = j["materialRemaps"].begin(); it != j["materialRemaps"].end(); ++it)
				if (it.value().is_string()) s.materialRemaps.push_back({ it.key(), it.value().get<std::string>() });
	}
	catch (...) {}
	return s;
}

std::string Tga::SaveFbxSidecar(const fs::path& absoluteFbxPath, const FbxImportSettings& settings)
{
	nlohmann::json remaps = nlohmann::json::object();
	for (const auto& remap : settings.materialRemaps)
		if (!remap[0].empty() && !remap[1].empty()) remaps[remap[0]] = remap[1];

	std::error_code ec;
	const fs::path relFbx = fs::relative(absoluteFbxPath, AssetRoot(), ec);
	const fs::path folder = ModelFolder(absoluteFbxPath);
	nlohmann::json j = {
		{ "Fbx", relFbx.generic_string() },
		{ "generatedPrefab", (folder / (absoluteFbxPath.stem().string() + ".tgo")).generic_string() },
		{ "normalConvention", settings.normalsOpenGL ? "OpenGL" : "DirectX" },
		{ "flipGreen", settings.flipGreen },
		{ "recursive", settings.recursive },
		{ "sourceFolder", settings.sourceFolder },
		{ "cookedFolder", settings.cookedFolder },
		{ "materialFolder", settings.materialFolder },
		{ "materialRemaps", remaps },
	};
	fs::path sidecar = absoluteFbxPath;
	sidecar.replace_extension(".tgm");
	std::ofstream out(sidecar);
	if (!out) return "Could not write " + sidecar.string();
	out << j.dump(2) << "\n";
	return {};
}

void Tga::RememberFbxImportDefaults(const fs::path& absoluteFbxPath, const FbxImportSettings& settings)
{
	EditorSettings& defaults = EditorSettings::Get();
	defaults.fbxNormalsOpenGL = settings.normalsOpenGL;
	defaults.fbxFlipGreen = settings.flipGreen;
	defaults.fbxRecursive = settings.recursive;

	// Folders are remembered as names relative to the model's own folder so they
	// carry over to the next model; one outside that folder can't be expressed that way.
	const fs::path folder = ModelFolder(absoluteFbxPath);
	auto remember = [&folder](const std::string& value, std::string& target)
	{
		std::error_code ec;
		const fs::path relative = fs::relative(fs::path(value), folder, ec);
		if (!ec && !relative.empty() && relative.begin() != relative.end() && *relative.begin() != "..")
			target = relative.generic_string();
	};
	remember(settings.sourceFolder, defaults.fbxSourceFolder);
	remember(settings.cookedFolder, defaults.fbxCookedFolder);
	remember(settings.materialFolder, defaults.fbxMaterialFolder);
	EditorSettings::Save();
}

std::string Tga::MakeFbxCookRequest(const fs::path& absoluteFbxPath, const FbxImportSettings& settings, FbxCookRequest& outRequest)
{
	const fs::path root = AssetRoot();
	std::error_code ec;
	const fs::path relFbx = fs::relative(absoluteFbxPath, root, ec);
	if (ec || relFbx.empty() || relFbx.string() == "." || relFbx.is_absolute())
		return "The FBX must be inside this project's asset folder.";
	if (settings.sourceFolder.empty() || settings.cookedFolder.empty() || settings.materialFolder.empty())
		return "Choose the texture, cooked texture and material folders.";

	for (const std::string* folder : { &settings.sourceFolder, &settings.cookedFolder, &settings.materialFolder })
	{
		fs::create_directories(AbsoluteAssetPath(root, *folder), ec);
		if (ec) return "Could not create " + *folder + ": " + ec.message();
	}

	outRequest = {};
	outRequest.fbx = relFbx.generic_string();
	outRequest.sourceFolder = settings.sourceFolder;
	outRequest.outputFolder = settings.cookedFolder;
	outRequest.materialFolder = settings.materialFolder;
	outRequest.generatedPrefab = (relFbx.parent_path() / (relFbx.stem().string() + ".tgo")).generic_string();
	outRequest.srcNormalsGl = settings.normalsOpenGL;
	outRequest.flipGreen = settings.flipGreen;
	outRequest.recursive = settings.recursive;
	outRequest.materialRemaps = settings.materialRemaps;
	return {};
}

std::string Tga::MakeDefaultFbxCookRequest(const fs::path& absoluteFbxPath, FbxCookRequest& outRequest)
{
	return MakeFbxCookRequest(absoluteFbxPath, LoadFbxImportSettings(absoluteFbxPath), outRequest);
}

// ------------------------------------------------------------------ cooker
std::string Tga::RunFbxCooker(const FbxCookRequest& request)
{
	try
	{
		if (request.fbx.empty() || request.sourceFolder.empty() || request.outputFolder.empty() || request.materialFolder.empty())
			return "Choose an FBX and its texture, cooked texture and material folders.";
		const fs::path root = AssetRoot();
		const fs::path fbx = AbsoluteAssetPath(root, request.fbx);
		const fs::path source = AbsoluteAssetPath(root, request.sourceFolder);
		const fs::path cooked = AbsoluteAssetPath(root, request.outputFolder);
		const fs::path materials = AbsoluteAssetPath(root, request.materialFolder);
		const fs::path prefab = AbsoluteAssetPath(root, request.generatedPrefab);
		if (!fs::exists(fbx)) return "The selected FBX no longer exists.";
		if (!fs::is_directory(source)) return "The texture source folder does not exist: " + source.string();
		fs::create_directories(cooked);
		fs::create_directories(materials);

		const fs::path exe = FindTextureCookerExe();
		if (exe.empty()) return "No TextureCooker_{Debug,Release,Retail}.exe was found beside GameEditor.";

		std::string command = Quote(exe) + " --in " + Quote(source) + " --out " + Quote(cooked) + " --tgmat-dir " + Quote(materials)
			+ " --fbx " + Quote(fbx) + " --game-root " + Quote(root) + " --tgo " + Quote(prefab)
			+ (request.force ? " --force" : "") + (request.srcNormalsGl ? " --src-normals gl" : " --src-normals dx")
			+ (request.flipGreen ? " --flip-green" : "") + (request.recursive ? " --recursive" : "");
		for (const auto& remap : request.materialRemaps)
			if (!remap[0].empty() && !remap[1].empty())
				command += " --material-remap \"" + remap[0] + "=" + remap[1] + "\"";

		const fs::path logPath = fs::temp_directory_path() / "TextureCooker_last.log";
		DWORD exitCode = 1;
		if (!RunHidden(command, logPath, exitCode)) return "Could not launch TextureCooker.";

		bool hasMaterial = false;
		std::error_code ec;
		if (fs::exists(prefab, ec))
			for (const auto& entry : fs::directory_iterator(materials, ec))
				if (entry.path().extension() == ".tgmat") { hasMaterial = true; break; }
		if (exitCode == 0 && fs::exists(prefab, ec) && hasMaterial)
			return "Import complete: generated " + prefab.filename().string() + " and its materials in " + materials.string() + ".";
		return "TextureCooker exited " + std::to_string(exitCode) + ". Expected " + prefab.string() + " plus .tgmat files in " + materials.string()
			+ ", but they were not found.\nCooker output (full log: " + logPath.string() + "):" + LogTail(logPath);
	}
	catch (const std::exception& e) { return std::string("Could not start import: ") + e.what(); }
	catch (...) { return "Could not start import."; }
}

void TextureCookRunner::Start(const FbxCookRequest& request)
{
	if (IsRunning()) return;
	auto state = std::make_shared<SharedResult>();
	myState = state;
	std::thread([state, request]()
	{
		std::string result = RunFbxCooker(request);
		std::lock_guard<std::mutex> lock(state->mutex);
		state->result = std::move(result);
		state->done = true;
	}).detach();
}

bool TextureCookRunner::PollResult(std::string& outResult)
{
	if (!myState || !myState->done.load()) return false;
	{
		std::lock_guard<std::mutex> lock(myState->mutex);
		outResult = myState->result;
	}
	myState.reset();
	return true;
}

// ------------------------------------------------------------------ normal preview
void NormalPreviewRunner::Start(const Params& params)
{
	if (IsRunning()) return;
	auto state = std::make_shared<SharedResult>();
	myState = state;
	static std::atomic<int> counter{ 0 };
	const std::string name = "p" + std::to_string(counter++);
	std::thread([state, params, name]()
	{
		Result result;
		try
		{
			const fs::path exe = FindTextureCookerExe();
			if (exe.empty()) throw std::runtime_error("No TextureCooker executable was found beside GameEditor.");
			const fs::path folder = TempPreviewFolder();
			fs::create_directories(folder);
			std::string command = Quote(exe) + " --in " + Quote(params.sourceFolder) + " --out " + Quote(folder) + " --preview-out " + Quote(folder)
				+ " --preview-name " + name + (params.normalsOpenGL ? " --src-normals gl" : " --src-normals dx")
				+ (params.flipGreen ? " --flip-green" : "") + (params.recursive ? " --recursive" : "");
			if (params.index >= 0) command += " --preview-index " + std::to_string(params.index);
			DWORD exitCode = 1;
			if (!RunHidden(command, folder / "preview_last.log", exitCode)) throw std::runtime_error("Could not launch TextureCooker.");

			std::ifstream in(folder / (name + ".json"));
			if (!in) throw std::runtime_error("The cooker produced no preview." + LogTail(folder / "preview_last.log"));
			nlohmann::json j;
			in >> j;
			if (j.contains("error")) throw std::runtime_error(j["error"].get<std::string>());
			result.key = j.value("key", std::string());
			result.index = j.value("index", 0);
			result.count = j.value("count", 0);
			result.sourceImage = std::string(kPreviewSubfolder) + "/" + name + "_source.dds";
			result.currentImage = std::string(kPreviewSubfolder) + "/" + name + "_current.dds";
			result.oppositeImage = std::string(kPreviewSubfolder) + "/" + name + "_opposite.dds";
			result.ok = true;
		}
		catch (const std::exception& e) { result.error = e.what(); }
		std::lock_guard<std::mutex> lock(state->mutex);
		state->result = std::move(result);
		state->done = true;
	}).detach();
}

bool NormalPreviewRunner::PollResult(Result& out)
{
	if (!myState || !myState->done.load()) return false;
	{
		std::lock_guard<std::mutex> lock(myState->mutex);
		out = myState->result;
	}
	myState.reset();
	return true;
}

// ------------------------------------------------------------------ dialog
void FbxConvertDialog::Open(const fs::path& absoluteFbxPath)
{
	// Previews from earlier sessions are just clutter. Done once per process: the
	// texture manager watches every file it has loaded, so nothing is deleted after that.
	static bool clearedOldPreviews = false;
	if (!clearedOldPreviews)
	{
		clearedOldPreviews = true;
		std::error_code ec;
		fs::remove_all(TempPreviewFolder(), ec);
	}
	myFbx = absoluteFbxPath;
	mySettings = LoadFbxImportSettings(absoluteFbxPath);
	myRememberForModel = mySettings.hasSidecar;
	myError.clear();
	myHasPreview = false;
	myPreview = {};
	myPreviewIndex = -1;
	myPreviewDirty = true;
	myOpenRequested = true;
}

void FbxConvertDialog::RequestPreview()
{
	myPreviewDirty = false;
	const fs::path source = AbsoluteAssetPath(AssetRoot(), mySettings.sourceFolder);
	if (!fs::is_directory(source))
	{
		myHasPreview = false;
		myPreview = {};
		myPreview.error = "The texture source folder does not exist yet: " + source.string();
		return;
	}
	NormalPreviewRunner::Params params;
	params.sourceFolder = source;
	params.normalsOpenGL = mySettings.normalsOpenGL;
	params.flipGreen = mySettings.flipGreen;
	params.recursive = mySettings.recursive;
	params.index = myPreviewIndex;
	myPreviewRunner.Start(params);
}

void FbxConvertDialog::ApplyPreviewResult(const NormalPreviewRunner::Result& result)
{
	myPreview = result;
	myHasPreview = result.ok;
	if (!result.ok)
	{
		for (ImTextureID& id : myPreviewTextures) id = 0;
		return;
	}
	myPreviewIndex = result.index;
	const EditorGraphicsBase& graphics = Editor::GetEditor()->GetEditorGraphics();
	myPreviewTextures[0] = graphics.GetTextureID(result.sourceImage);
	myPreviewTextures[1] = graphics.GetTextureID(result.currentImage);
	myPreviewTextures[2] = graphics.GetTextureID(result.oppositeImage);
}

bool FbxConvertDialog::Draw(FbxCookRequest& outRequest)
{
	NormalPreviewRunner::Result finished;
	if (myPreviewRunner.PollResult(finished)) ApplyPreviewResult(finished);

	if (myOpenRequested)
	{
		ImGui::OpenPopup("Convert FBX to TGO");
		myOpenRequested = false;
		myOpen = true;
	}
	if (!myOpen) return false;

	bool convert = false;
	bool keepOpen = true;
	ImGui::SetNextWindowSize(ImVec2(760.f, 0.f), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Convert FBX to TGO", &keepOpen, ImGuiWindowFlags_NoSavedSettings))
	{
		bool settingsChanged = false;

		ImGui::TextDisabled("Model");
		ImGui::SameLine();
		ImGui::TextUnformatted(myFbx.filename().string().c_str());

		ImGui::SeparatorText("Normal maps");
		int convention = mySettings.normalsOpenGL ? 0 : 1;
		if (ImGui::Combo("Source convention", &convention, "OpenGL (green up)\0DirectX (green down)\0"))
		{
			mySettings.normalsOpenGL = convention == 0;
			settingsChanged = true;
		}
		settingsChanged |= ImGui::Checkbox("Flip green channel", &mySettings.flipGreen);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("An extra flip on top of the convention -- for a model that still looks inverted, e.g. mirrored UVs.");

		ImGui::SeparatorText("Folders");
		auto folderRow = [&](const char* label, const char* id, std::string& value)
		{
			ImGui::PushID(id);
			settingsChanged |= InputString(label, value);
			ImGui::SameLine();
			if (ImGui::SmallButton("Pick"))
			{
				std::string* target = &value;
				FileDialog::OpenProjectFolder([target](const char* path) {
					std::error_code ec;
					const fs::path relative = fs::relative(fs::absolute(path), AssetRoot(), ec);
					if (!ec && !relative.empty() && relative != ".") *target = relative.generic_string();
				});
			}
			ImGui::PopID();
		};
		folderRow("Source textures", "src", mySettings.sourceFolder);
		folderRow("Cooked textures", "cooked", mySettings.cookedFolder);
		folderRow("Materials", "mat", mySettings.materialFolder);
		settingsChanged |= ImGui::Checkbox("Include subfolders", &mySettings.recursive);

		ImGui::SeparatorText("Normal map preview");
		if (myPreviewRunner.IsRunning())
			ImGui::TextDisabled("Generating preview...");
		else if (myHasPreview)
		{
			ImGui::Text("%s  (%d of %d)", myPreview.key.c_str(), myPreview.index + 1, myPreview.count);
			ImGui::SameLine();
			if (myPreview.count > 1)
			{
				if (ImGui::SmallButton("Previous")) { myPreviewIndex = (myPreview.index + myPreview.count - 1) % myPreview.count; myPreviewDirty = true; }
				ImGui::SameLine();
				if (ImGui::SmallButton("Next")) { myPreviewIndex = (myPreview.index + 1) % myPreview.count; myPreviewDirty = true; }
			}
		}
		else if (!myPreview.error.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.20f, 1.0f));
			ImGui::TextWrapped("%s", myPreview.error.c_str());
			ImGui::PopStyleColor();
		}

		if (myHasPreview)
		{
			const char* captions[3] = { "Source normal map", "Cooked with these settings", "Cooked with green flipped" };
			for (int i = 0; i < 3; ++i)
			{
				ImGui::BeginGroup();
				ImGui::Image(myPreviewTextures[i], ImVec2(220.f, 220.f));
				ImGui::TextUnformatted(captions[i]);
				ImGui::EndGroup();
				if (i < 2) ImGui::SameLine();
			}
			ImGui::TextWrapped("Correct normals look raised: bricks, rivets and panel edges stand proud, lit on their upper-left edges. "
				"Change the settings until the middle image looks right.");
		}

		ImGui::Separator();
		ImGui::Checkbox("Remember these settings for this model", &myRememberForModel);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Writes a small %s beside the FBX so this model always converts the same way.", myFbx.filename().replace_extension(".tgm").string().c_str());

		if (!myError.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
			ImGui::TextWrapped("%s", myError.c_str());
			ImGui::PopStyleColor();
		}

		if (ImGui::Button("Convert", ImVec2(140.f, 0.f)))
		{
			RememberFbxImportDefaults(myFbx, mySettings);
			myError = myRememberForModel ? SaveFbxSidecar(myFbx, mySettings) : std::string();
			if (myError.empty()) myError = MakeFbxCookRequest(myFbx, mySettings, outRequest);
			if (myError.empty()) convert = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(140.f, 0.f))) keepOpen = false;

		if (settingsChanged) myPreviewDirty = true;
		if (myPreviewDirty && !myPreviewRunner.IsRunning()) RequestPreview();

		if (convert || !keepOpen)
		{
			ImGui::CloseCurrentPopup();
			myOpen = false;
		}
		ImGui::EndPopup();
	}
	else
		myOpen = false;
	return convert;
}
