#include "stdafx.h"
#include <tge/editor/Import/ImportSettingsDocument.h>
#include <tge/editor/Editor.h>
#include <tge/editor/FileDialog/FileDialog.h>
#include <tge/settings/settings.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <imgui.h>

using namespace Tga;
namespace fs = std::filesystem;

void ImportSettingsDocument::PickFolder(std::string& target)
{
	std::string* targetPtr = &target;
	FileDialog::OpenProjectFolder([targetPtr](const char* path) {
		const fs::path root = fs::absolute(Settings::GameAssetRoot());
		const fs::path chosen = fs::absolute(path);
		std::error_code ec;
		fs::path relative = fs::relative(chosen, root, ec);
		*targetPtr = (!ec && !relative.empty() && relative.string() != ".") ? relative.generic_string() : std::string();
	});
}

void ImportSettingsDocument::PickFbx()
{
	FileDialog::OpenFile([this](const char* path) {
		const fs::path root = fs::absolute(Settings::GameAssetRoot());
		const fs::path chosen = fs::absolute(path);
		std::error_code ec;
		const fs::path relative = fs::relative(chosen, root, ec);
		if (ec || relative.empty() || relative.string() == "." || relative.is_absolute())
		{
			myStatus = "Choose an FBX inside this project's asset folder.";
			return;
		}
		myFbx = relative.generic_string();
		ApplyRecommendedPaths();
		// Choosing an FBX is the important authored action. Persist it immediately
		// rather than relying on the user to press Save later or on a subsequent
		// generation attempt reaching its Save call. This also means reopening the
		// document after a cancelled/import-failed cook still shows the selection.
		Save();
		myStatus = "FBX selected. Review the summary, then generate the prefab.";
	});
}

void ImportSettingsDocument::ApplyRecommendedPaths()
{
	if (myFbx.empty()) return;
	fs::path fbxPath(myFbx);
	const fs::path folder = fbxPath.parent_path();
	if (mySourceFolder.empty()) mySourceFolder = (folder / "textures").generic_string();
	if (myOutputFolder.empty()) myOutputFolder = folder.generic_string();
	if (myGeneratedPrefab.empty()) myGeneratedPrefab = fbxPath.replace_extension(".tgo").generic_string();
}

void ImportSettingsDocument::Init(std::string_view path)
{
	Document::Init(path);
	myFbx.clear(); mySourceFolder.clear(); myOutputFolder.clear(); myGeneratedPrefab.clear();
	// The renderer negates the imported bitangent. Authoring exports, including
	// Sponza, are OpenGL tangent-space (green-up), so cook them to the engine's
	// expected polarity by default. DirectX remains an explicit opt-in.
	myRemaps.clear(); myScale = 1.f; myAxis = 0; myNormals = 0; myFlipGreen = false; myRecursive = true;

	try
	{
		std::ifstream in(myPath);
		if (!in) { myStatus = "Could not open import settings."; return; }
		nlohmann::json j;
		in >> j;
		if (!j.is_object()) { myStatus = "Import settings must contain a JSON object."; return; }

		auto readString = [&j](const char* key, std::string& value)
		{
			auto it = j.find(key);
			if (it != j.end() && it->is_string()) value = it->get<std::string>();
		};
		readString("Fbx", myFbx);
		readString("generatedPrefab", myGeneratedPrefab);
		if (auto it = j.find("scale"); it != j.end() && it->is_number()) myScale = it->get<float>();
		if (auto it = j.find("axisConversion"); it != j.end() && it->is_string()) myAxis = it->get<std::string>() == "EngineDefault" ? 0 : 1;
		if (auto it = j.find("normalConvention"); it != j.end() && it->is_string()) myNormals = it->get<std::string>() == "DirectX" ? 1 : 0;
		if (auto it = j.find("flipGreen"); it != j.end() && it->is_boolean()) myFlipGreen = it->get<bool>();

		if (auto it = j.find("reimport"); it != j.end() && it->is_object())
		{
			const auto& reimport = *it;
			auto readReimportString = [&reimport](const char* key, std::string& value)
			{
				auto valueIt = reimport.find(key);
				if (valueIt != reimport.end() && valueIt->is_string()) value = valueIt->get<std::string>();
			};
			readReimportString("sourceFolder", mySourceFolder);
			readReimportString("outputFolder", myOutputFolder);
			if (auto recursive = reimport.find("recursive"); recursive != reimport.end() && recursive->is_boolean()) myRecursive = recursive->get<bool>();
		}
		if (auto it = j.find("materialRemaps"); it != j.end() && it->is_object())
			for (auto remap = it->begin(); remap != it->end(); ++remap)
				if (remap.value().is_string()) myRemaps.push_back({ remap.key(), remap.value().get<std::string>() });

		// Older .tgm files, including BistroExterior, only contain Fbx.  Keep them
		// usable without changing them until the user explicitly saves.
		if (!myFbx.empty())
		{
			fs::path fbxPath = fs::path(Settings::GameAssetRoot()) / fs::path(myFbx);
			if (mySourceFolder.empty())
			{
				fs::path textureFolder = fbxPath.parent_path() / "Textures";
				mySourceFolder = fs::relative(fs::exists(textureFolder) ? textureFolder : fbxPath.parent_path(), Settings::GameAssetRoot()).generic_string();
			}
			if (myOutputFolder.empty()) myOutputFolder = fs::relative(fbxPath.parent_path(), Settings::GameAssetRoot()).generic_string();
			if (myGeneratedPrefab.empty()) myGeneratedPrefab = fs::path(myFbx).replace_extension(".tgo").generic_string();
		}
	}
	catch (const std::exception& e) { myStatus = std::string("Could not load import settings: ") + e.what(); }
	catch (...) { myStatus = "Could not load import settings."; }
}
void ImportSettingsDocument::Save()
{
	nlohmann::json remaps=nlohmann::json::object(); for(const auto& r:myRemaps) if(!r[0].empty()&&!r[1].empty()) remaps[r[0]]=r[1];
	nlohmann::json j={{"version",1},{"Fbx",myFbx},{"scale",myScale},{"axisConversion",myAxis==0?"EngineDefault":"Custom"},{"normalConvention",myNormals==0?"OpenGL":"DirectX"},{"flipGreen",myFlipGreen},{"generatedPrefab",myGeneratedPrefab},{"materialRemaps",remaps},{"reimport",{{"sourceFolder",mySourceFolder},{"outputFolder",myOutputFolder},{"recursive",myRecursive}}}};
	std::ofstream out(myPath); if(out){out<<j.dump(2)<<"\n";mySaveUndoStackSize=myUndoStackSize;myStatus="Saved.";} else myStatus="Could not save import settings.";
}
std::string ImportSettingsDocument::RunCooker(const FbxCookRequest& request)
{
	try
	{
		if (request.fbx.empty() || request.sourceFolder.empty() || request.outputFolder.empty())
			return "Choose an FBX first; the other paths are filled in automatically.";
		const fs::path root = fs::absolute(Settings::GameAssetRoot());
		auto absoluteAssetPath = [&root](const std::string& value) { fs::path result(value); return result.is_absolute() ? result : root / result; };
		if (!fs::exists(absoluteAssetPath(request.fbx))) return "The selected FBX no longer exists.";
		if (!fs::is_directory(absoluteAssetPath(request.sourceFolder))) return "The texture source folder does not exist.";
		fs::create_directories(absoluteAssetPath(request.outputFolder));

		wchar_t module[MAX_PATH]{}; GetModuleFileNameW(nullptr,module,MAX_PATH); fs::path exe=fs::path(module).parent_path()/"TextureCooker_Debug.exe";
		if (!fs::exists(exe)) return "TextureCooker_Debug.exe was not found beside GameEditor.";
		const fs::path generatedPrefab = request.generatedPrefab.empty() ? fs::path(request.fbx).replace_extension(".tgo") : fs::path(request.generatedPrefab);
		std::string command="\""+exe.string()+"\" --in \""+absoluteAssetPath(request.sourceFolder).string()+"\" --out \""+absoluteAssetPath(request.outputFolder).string()+"\" --fbx \""+absoluteAssetPath(request.fbx).string()+"\" --game-root \""+root.string()+"\" --tgo \""+absoluteAssetPath(generatedPrefab.string()).string()+"\" --force"+(request.srcNormalsGl?" --src-normals gl":" --src-normals dx")+(request.flipGreen?" --flip-green":"")+(request.recursive?" --recursive":"");
		for (const auto& remap : request.materialRemaps)
			if (!remap[0].empty() && !remap[1].empty())
				command += " --material-remap \"" + remap[0] + "=" + remap[1] + "\"";
		// MultiByteToWideChar (not a naive per-char widen) so non-ASCII asset
		// paths survive the trip through CreateProcessW's wide command line.
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), (int)command.size(), nullptr, 0);
		std::vector<wchar_t> cmd(wlen + 1, 0);
		MultiByteToWideChar(CP_UTF8, 0, command.c_str(), (int)command.size(), cmd.data(), wlen);
		STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
		if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
			nullptr, nullptr, &si, &pi))
		{
			CloseHandle(pi.hThread);
			// Wait for a deterministic result rather than detaching: a bad path or
			// cooker failure must not look like a successful import.
			WaitForSingleObject(pi.hProcess, INFINITE);
			DWORD exitCode = 1;
			GetExitCodeProcess(pi.hProcess, &exitCode);
			CloseHandle(pi.hProcess);
			const fs::path generated = absoluteAssetPath(generatedPrefab.string());
			bool hasMaterial = false;
			std::error_code ec;
			if (fs::exists(generated, ec))
				for (const auto& entry : fs::directory_iterator(generated.parent_path(), ec))
					if (entry.path().extension() == ".tgmat") { hasMaterial = true; break; }
			if (exitCode == 0 && fs::exists(generated, ec) && hasMaterial)
				return "Import complete: generated " + generated.filename().string() + " and material assets in " + generated.parent_path().string() + ".";
			return "TextureCooker exited " + std::to_string(exitCode) + ". Expected " + generated.string() + " plus .tgmat files in its folder, but they were not found.";
		}
		return "Could not launch TextureCooker.";
	}
	catch (const std::exception& e) { return std::string("Could not start import: ") + e.what(); }
	catch (...) { return "Could not start import."; }
}

void ImportSettingsDocument::Reimport()
{
	ApplyRecommendedPaths();
	if(myFbx.empty()||mySourceFolder.empty()||myOutputFolder.empty()){myStatus="Choose an FBX first; the other paths are filled in automatically.";return;}
	Save();
	// Verify the durable descriptor before spawning an external process. A
	// write failure must never masquerade as a successful texture-only import.
	try
	{
		std::ifstream saved(myPath);
		nlohmann::json savedSettings;
		if (!saved || !(saved >> savedSettings) || savedSettings.value("Fbx", std::string()) != myFbx)
		{
			myStatus = "Could not persist the selected FBX to " + myPath + ". The importer was not started.";
			return;
		}
	}
	catch (...)
	{
		myStatus = "Could not verify the saved import settings. The importer was not started.";
		return;
	}
	// Save is the source of truth for this import.  Do not pass --tgm to the
	// cooker: that mode creates a new descriptor and used to overwrite this
	// document, silently erasing scale, conversion and material-remap edits.
	// The explicit --tgo request still generates every .tgmat and the prefab.
	FbxCookRequest request;
	request.fbx = myFbx;
	request.sourceFolder = mySourceFolder;
	request.outputFolder = myOutputFolder;
	request.generatedPrefab = myGeneratedPrefab.empty() ? fs::path(myFbx).replace_extension(".tgo").generic_string() : myGeneratedPrefab;
	request.srcNormalsGl = myNormals == 0;
	request.flipGreen = myFlipGreen;
	request.recursive = myRecursive;
	request.materialRemaps = myRemaps;
	myStatus = RunCooker(request);
}
void ImportSettingsDocument::Update(float, InputManager&)
{
	bool open=true; std::string title="Import Settings: "+fs::path(myPath).stem().string()+"###Document:"+myPath; ImGui::Begin(title.c_str(),&open); if(!open)myState=State::CloseRequested;
	ImGui::TextDisabled("FBX to prefab (.tgm)"); ImGui::TextWrapped("Pick a model, verify the folders, then generate its TGO and materials. Most models only need the first button and Generate."); ImGui::Separator();
	auto text=[](const char* label,std::string& s){char b[512]{};strncpy_s(b,s.c_str(),_TRUNCATE);if(ImGui::InputText(label,b,sizeof(b)))s=b;};
	if (ImGui::Button("Choose FBX...")) PickFbx(); ImGui::SameLine(); if (ImGui::Button("Use recommended paths")) { ApplyRecommendedPaths(); myStatus="Recommended paths applied."; }
	text("FBX source",myFbx);
	text("Texture source folder",mySourceFolder); ImGui::SameLine(); if(ImGui::SmallButton("Pick##TextureFolder")) PickFolder(mySourceFolder);
	text("Cooked output folder",myOutputFolder); ImGui::SameLine(); if(ImGui::SmallButton("Pick##OutputFolder")) PickFolder(myOutputFolder);
	text("Generated prefab (.tgo)",myGeneratedPrefab);
	if (!myFbx.empty()) ImGui::TextDisabled("Will create: %s", myGeneratedPrefab.c_str());
	if (ImGui::CollapsingHeader("Advanced import options")) { ImGui::DragFloat("Scale",&myScale,.01f,.0001f,10000.f); const char* axes[]={"Engine default","Custom"};ImGui::Combo("Axis conversion",&myAxis,axes,2); const char* normals[]={"OpenGL (+Y, recommended)","DirectX (-Y)"};ImGui::Combo("Source normal convention",&myNormals,normals,2);ImGui::Checkbox("Flip normal green channel",&myFlipGreen);ImGui::Checkbox("Include subfolders",&myRecursive); ImGui::SeparatorText("Material remaps"); for(size_t i=0;i<myRemaps.size();++i){ImGui::PushID((int)i);text("FBX material",myRemaps[i][0]);ImGui::SameLine();text(".tgmat",myRemaps[i][1]);ImGui::SameLine();if(ImGui::SmallButton("Remove")){myRemaps.erase(myRemaps.begin()+i);ImGui::PopID();break;}ImGui::PopID();} if(ImGui::Button("Add material remap"))myRemaps.push_back({"",""}); }
	ImGui::Separator();if(ImGui::Button("Save settings"))Save();ImGui::SameLine();if(ImGui::Button("Generate TGO and textures"))Reimport();if(!myStatus.empty())ImGui::TextWrapped("%s",myStatus.c_str());ImGui::End();
}
