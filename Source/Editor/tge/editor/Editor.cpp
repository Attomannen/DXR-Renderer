#include "stdafx.h"

#include <commdlg.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <tge/editor/Editor.h>
#include <IconFontHeaders/IconsLucide.h>

#include <tge/animation/AnimationClip.h>
#include <tge/input/InputManager.h>
#include <tge/editor/CommandManager/CommandManager.h>
#include <tge/graphics/DX11.h>
#include <tge/settings/settings.h>
#include <tge/imgui/ImGuiInterface.h>
#include <tge/scene/SceneSerialize.h>

#include <tge/script/ScriptManager.h>
#include <tge/script/ScriptRuntimeInstance.h>

#include <tge/animation/Script/AnimationNodes.h>

#include <tge/editor/AnimationClip/AnimationClipDocument.h>
#include <tge/editor/ObjectDefinition/ObjectDefinitionDocument.h>
#include <tge/editor/Scene/SceneDocument.h>
#include <tge/editor/Material/MaterialDocument.h>
#include <tge/editor/Import/ImportSettingsDocument.h>
#include <tge/editor/Scene/SceneSelection.h>
#include <tge/editor/ScriptEditor/ScriptEditor.h>
#include <tge/editor/Document/Document.h>

#include <tge/editor/Commands/AddSceneObjectsCommand.h>

#include <tge/editor/Tools/Viewport/Viewport.h>
#include <tge/editor/Tools/ProjectRunControls/ProjectRunControls.h>
#include <tge/editor/FileDialog/FileDialog.h>
#include <tge/editor/imgui_widgets/imgui_widgets.h>
#include "imgui_internal.h" // for DockBuilder Api
#include <ImGuizmo.h>

#include <tge/script/Nodes/CommonNodes.h>
#include <tge/script/Nodes/ExampleNodes.h>
#include <tge/script/Nodes/CommonMathNodes.h>
#include <tge/script/Nodes/SceneObjectNodes.h>
#include <tge/scene/ScenePropertyTypes.h>

#include <tge/editor/p4/p4.h>

#include <tge/editor/EditorGraphics/NullEditorGraphics.h>

static bool locImGuiDemoOpen = false;
static bool locImGuiStyleEditorOpen = false;
static bool locPerforceEnabled = false;
static bool locTextureImporterOpen = false;
static char locTextureInput[512]{};
static char locTextureOutput[512]{};
static bool locTextureNormalsDx = false;
static bool locTextureRecursive = true;
static bool locTextureForce = false;
static std::string locTextureCookStatus;

using namespace Tga;

StringId GetSelectionFromAssetBrowser()
{
	return Editor::GetEditor()->GetAssetBrowser().GetSelectedAsset();
}

Editor* locEditor;

Editor* Editor::GetEditor()
{
	return locEditor;
}

Tga::Editor::Editor()
{
	assert(locEditor == nullptr);

	locEditor = this;
}

Tga::Editor::~Editor()
{
	P4::StopPolling();
	for (auto& doc : myOpenDocuments)
	{
		doc->Close();
	}
	assert(locEditor == this);
	locEditor = nullptr;
}

namespace Tga
{
	void CommandManagerEditorCallback(CommandManager::Action action);
	static CommandManager::CallbackRegistration callbackRegistration(&CommandManagerEditorCallback);

	void CommandManagerEditorCallback(CommandManager::Action action)
	{
		if (locEditor != nullptr)
			locEditor->OnAction(action);

		/*
		if (action == CommandManager::Action::Do)
		{
			locRedoStack.clear();
			locUndoStack.push_back(locUndoStack.empty() ? SelectionUndoState{} : locUndoStack.back()); // duplicate selection state to have a value for each command
		}
		else if (action == CommandManager::Action::Undo)
		{
			locRedoStack.push_back(locUndoStack.back());
			locUndoStack.pop_back();
		}
		else if (action == CommandManager::Action::Redo)
		{
			locUndoStack.push_back(locRedoStack.back());
			locRedoStack.pop_back();
		}
		else if (action == CommandManager::Action::Clear)
		{
			locUndoStack.clear();
			locRedoStack.clear();
		}*/
	}
}

void Tga::Editor::Init(const EditorConfiguration& aEditorConfiguration, std::unique_ptr<EditorGraphicsBase>&& graphics)
{
	myEditorConfiguration = aEditorConfiguration;
	myEditorGraphics = std::move(graphics);
	if (!myEditorGraphics)
	{
		myEditorGraphics = std::make_unique<NullEditorGraphics>();
	}
	Tga::RegisterCommonNodes();
	Tga::RegisterCommonMathNodes();
	Tga::RegisterAnimationNodes();

	RegisterExampleNodes();

	RegisterAssetBrowserGetSelectionFunction(GetSelectionFromAssetBrowser);

	// Defaults to setting the game project to default, perhaps not optimal, and could lead to problems if we move things around
	// but I believe that right now the use-case is that we are going to couple the editor to the game-project. And at this time
	// we dont really have the situation where we might want to set another project.. so for now and for convenience.
	
	const std::string& rootPath = Settings::GameAssetRoot();

	myAssetBrowser.SetPath(rootPath);
	mySceneObjectDefinitionManager.Init(rootPath);
	EditorScriptManager::GetInstance().Init();
	if (locPerforceEnabled)
	{
		P4::StartPolling(rootPath.c_str());
	}

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows

	myTopLevelWindowClass = {};
	myTopLevelWindowClass.ClassId = 0xFFFFFFFF;
	myTopLevelWindowClass.DockingAllowUnclassed = false;

	myDocumentLevelWindowClass = {};
	myDocumentLevelWindowClass.ClassId = 0xFFFFFFFF - 1;
	myDocumentLevelWindowClass.DockingAllowUnclassed = false;

	myGlobalDockSpaceId = 0;
	myDocumentDockSpaceId = 0;
}

bool Tga::Editor::ShowSavePromptModal()
{
	bool closeModal = false;
	ImGui::OpenPopup("Unsaved Changes");
	{
		if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			auto* document = myDocumentsPendingClose.front();
			ImGui::Text("There are unsaved changes in %s.", document->GetPath().data());
			ImGui::Text("Would you like to save changes before closing?");
			ImGui::Text("");

			if (ImGui::Button("Save"))
			{
				document->Save();
				document->SetState(Document::State::CloseConfirmed);
				closeModal = true;
			}

			ImGui::SameLine();
			if (ImGui::Button("Don't Save"))
			{
				// Set document to close without saving, i.e. set pending close to true
				document->SetState(Document::State::CloseConfirmed);
				closeModal = true;
			}

			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				// move document back from closed to open document collection..
				document->SetState(Document::State::Open);
				closeModal = true;
			}

			if (closeModal)
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
	return closeModal;
}


void Tga::Editor::CreateNewScene()
{
	const std::string initialFolder = myAssetBrowser.GetCurrentFolder().string();
	FileDialog::SaveFile(
		FileDialog::FileType::tgs,
		[this](const char* path) {
			fs::path p = path;
			if (p.extension().empty())
			{
				p = p.replace_extension(".tgs");
			}

			fs::path relativePath = fs::relative(p, Settings::GameAssetRoot());

			Scene scene;
			scene.SetName(p.filename().string().c_str());
			scene.SetPath(relativePath.string().c_str());

			SaveScene(scene, SceneP4Handler);
			std::unique_ptr<SceneDocument> sceneDocument = std::make_unique<SceneDocument>();
			sceneDocument->Init(relativePath.string());
			AddDocument(std::move(sceneDocument));
		}, initialFolder.c_str());
}

void Tga::Editor::CreateNewObjectDefinition()
{
	const std::string initialFolder = myAssetBrowser.GetCurrentFolder().string();
	FileDialog::SaveFile(
		FileDialog::FileType::tgo,
		[this](const char* path) {
			
			fs::path p = path;
			if (p.extension().empty())
			{
				p = p.replace_extension(".tgo");
			}

			fs::path relativePath = fs::relative(p, Settings::GameAssetRoot());

			mySceneObjectDefinitionManager.CreateOrGet(relativePath.string());

			std::unique_ptr<ObjectDefinitionDocument> sceneDocument = std::make_unique<ObjectDefinitionDocument>();
			sceneDocument->Init(p.string());
			AddDocument(std::move(sceneDocument));
		}, initialFolder.c_str());
}

void Tga::Editor::CreateNewMaterial()
{
	const std::string initialFolder = myAssetBrowser.GetCurrentFolder().string();
	FileDialog::SaveFile(
		FileDialog::FileType::tgmat,
		[this](const char* path) {

			fs::path p = path;
			if (p.extension().empty())
				p = p.replace_extension(".tgmat");

			// Write a default material so the file exists before the doc loads it.
			MaterialAsset{}.Save(p.string());

			std::unique_ptr<MaterialDocument> document = std::make_unique<MaterialDocument>();
			document->Init(p.string());
			AddDocument(std::move(document));
		}, initialFolder.c_str());
}

void Tga::Editor::CreateNewAnimationClip()
{
	const std::string initialFolder = myAssetBrowser.GetCurrentFolder().string();
	FileDialog::SaveFile(
		FileDialog::FileType::tgac,
		[this](const char* path) {

			fs::path p = path;
			if (p.extension().empty())
			{
				p = p.replace_extension(".tgac");
			}

			fs::path relativePath = fs::relative(p, Settings::GameAssetRoot());

			StringId s = StringRegistry::RegisterOrGetString(relativePath.string());
			GetOrCreateAnimationClip(s);

			std::unique_ptr<AnimationClipDocument> sceneDocument = std::make_unique<AnimationClipDocument>();
			sceneDocument->Init(p.string());
			AddDocument(std::move(sceneDocument));
		}, initialFolder.c_str());
}

void Tga::Editor::CreateNewImportSettings()
{
	const std::string initialFolder = myAssetBrowser.GetCurrentFolder().string();
	FileDialog::SaveFile(FileDialog::FileType::tgm, [this](const char* path) {
		fs::path p = path;
		if (p.extension().empty()) p = p.replace_extension(".tgm");
		// Init accepts an empty/new file and supplies safe defaults; saving it
		// immediately makes the Project browser see the asset before it is edited.
		std::ofstream(p) << "{\n  \"version\": 1,\n  \"Fbx\": \"\",\n  \"scale\": 1.0,\n  \"axisConversion\": \"EngineDefault\",\n  \"normalConvention\": \"OpenGL\",\n  \"materialRemaps\": {},\n  \"reimport\": {}\n}\n";
		auto document = std::make_unique<ImportSettingsDocument>();
		document->Init(p.string());
		AddDocument(std::move(document));
	}, initialFolder.c_str());
}

static void OpenTextureImporterFromSelection()
{
	const std::filesystem::path root = Tga::Settings::GameAssetRoot();
	std::filesystem::path selected = Editor::GetEditor()->GetAssetBrowser().GetSelectedAsset().GetString();
	std::filesystem::path folder = root / selected;
	if (!std::filesystem::is_directory(folder)) folder = folder.parent_path();
	if (folder.empty()) folder = root;
	strncpy_s(locTextureInput, folder.string().c_str(), sizeof(locTextureInput) - 1);
	strncpy_s(locTextureOutput, folder.string().c_str(), sizeof(locTextureOutput) - 1);
	locTextureCookStatus.clear();
	locTextureImporterOpen = true;
}

static void DrawTextureImporter()
{
	if (!locTextureImporterOpen) return;
	ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Texture Importer", &locTextureImporterOpen)) { ImGui::End(); return; }
	ImGui::TextWrapped("Convert loose source maps into the engine DDS layout. Source names are detected as a convenience; use cook.json in this folder for explicit per-material channel mappings and overrides.");
	ImGui::Separator();
	ImGui::InputText("Source folder", locTextureInput, sizeof(locTextureInput));
	ImGui::InputText("Cooked DDS output", locTextureOutput, sizeof(locTextureOutput));
	ImGui::Checkbox("Source normals are DirectX", &locTextureNormalsDx);
	ImGui::Checkbox("Include subfolders", &locTextureRecursive);
	ImGui::Checkbox("Force recook", &locTextureForce);
	ImGui::SeparatorText("Output packing (Unreal)");
	ImGui::BulletText("_BC.dds  BC7 sRGB: base colour RGB, opacity A");
	ImGui::BulletText("_N.dds   BC5 linear: normal X/Y (DirectX)");
	ImGui::BulletText("_ORM.dds BC7 linear: ambient occlusion, roughness, metallic");
	ImGui::BulletText("_E.dds   BC7 sRGB: emissive colour (intensity in the .tgmat)");
	if (ImGui::Button("Cook textures", ImVec2(140, 0)))
	{
		// GameEditor normally runs with Bin as its working directory, so deriving
		// from current_path()/Bin produced Bin/Bin/TextureCooker_Debug.exe. Resolve
		// beside the actual host executable instead; this also survives launching
		// the editor from Visual Studio or a shortcut with another working folder.
		wchar_t modulePath[MAX_PATH]{};
		GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
		std::filesystem::path exe = std::filesystem::path(modulePath).parent_path() / "TextureCooker_Debug.exe";
		if (!std::filesystem::exists(exe))
			exe = std::filesystem::current_path() / "TextureCooker_Debug.exe";
		if (!std::filesystem::exists(exe))
		{
			locTextureCookStatus = "TextureCooker_Debug.exe was not found beside GameEditor.";
			ImGui::TextWrapped("%s", locTextureCookStatus.c_str());
			ImGui::End();
			return;
		}
		const std::string command = "\"" + exe.string() + "\" --in \"" + locTextureInput + "\" --out \"" + locTextureOutput + "\" --src-normals " + (locTextureNormalsDx ? "dx" : "gl") + (locTextureRecursive ? " --recursive" : "") + (locTextureForce ? " --force" : "");
		std::vector<wchar_t> commandLine(command.begin(), command.end()); commandLine.push_back(L'\0');
		STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
		if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
		{
			CloseHandle(process.hThread); CloseHandle(process.hProcess);
			locTextureCookStatus = "Texture cook started. Refresh the Project browser after it completes.";
		}
		else locTextureCookStatus = "Could not launch TextureCooker_Debug.exe.";
	}
	if (!locTextureCookStatus.empty()) ImGui::TextWrapped("%s", locTextureCookStatus.c_str());
	ImGui::End();
}

void Tga::Editor::Update(float aTimeDelta, InputManager& inputManager)
{
	ImGuizmo::BeginFrame();
	size_t numOpenDocuments = myOpenDocuments.size();

	aTimeDelta; inputManager;
	{
		DX11::BackBuffer->SetAsActiveTarget();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		{
			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}

		////////////////////////////////
		// Keyboard shortcuts
		if (io.KeyCtrl)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Z))
			{
				if (io.KeyShift)
				{
					CommandManager::Redo();
				}
				else
				{
					CommandManager::Undo();
				}
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_S))
			{
				Editor::GetEditor()->Save();
			}
		}

		///////////////////////////////
		// Main Editor window
		ImGui::Begin("Editor", 0, window_flags);
		{
			ImGui::PopStyleVar(3);

			myGlobalDockSpaceId = ImGui::GetID("MainDockSpace");
			myDocumentDockSpaceId = ImGui::GetID("DocumentDockSpace");

			ImGui::DockSpace(myGlobalDockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode, &myTopLevelWindowClass);
			//ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_AutoHideTabBar | ImGuiDockNodeFlags_PassthruCentralNode);

			ImGuiID center = 0, top = 0, bottomLeft = 0, bottomRight = 0;
			if (!myIsDockingInitialized)
			{
				ImGui::DockBuilderRemoveNode(myGlobalDockSpaceId); // clear any previous layout
				ImGui::DockBuilderAddNode(myGlobalDockSpaceId, ImGuiDockNodeFlags_DockSpace);
				ImGui::DockBuilderSetNodeSize(myGlobalDockSpaceId, viewport->WorkSize);
				center = myGlobalDockSpaceId;

				ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.1f, &top, &center);
				ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f, &bottomRight, &center);
				ImGui::DockBuilderSplitNode(bottomRight, ImGuiDir_Left, 0.2f, &bottomLeft, &bottomRight);

				ImGui::DockBuilderDockWindow(GlobalWindowNames[(size_t)GlobalWindows::DocumentDock], center);
				ImGui::DockBuilderDockWindow(GlobalWindowNames[(size_t)GlobalWindows::AssetBrowserDirectories], bottomLeft);
				ImGui::DockBuilderDockWindow(GlobalWindowNames[(size_t)GlobalWindows::AssetBrowserFiles], bottomRight);

				ImGui::DockBuilderFinish(myGlobalDockSpaceId);

				myIsDockingInitialized = true;
			}

			//////////////////////////
			// Menu 
			{
				if (ImGui::BeginMenuBar())
				{
					if (ImGui::BeginMenu("File"))
					{
						if (ImGui::MenuItem("Save", "Ctrl+S"))
						{
							Save();
						}

						ImGui::EndMenu();
					}

					if (ImGui::BeginMenu("Edit"))
					{
						if (ImGui::MenuItem("Undo", "Ctrl+Z"))
						{
							CommandManager::Undo();
						}
						if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z"))
						{
							CommandManager::Redo();
						}

						ImGui::EndMenu();
					}
					if (ImGui::BeginMenu("View"))
					{
						ImGui::MenuItem("Render Viewport Grid", NULL, &myIsViewportGridVisible);
						ImGui::EndMenu();
					}
					if (ImGui::BeginMenu("Create"))
					{
						if (ImGui::MenuItem("Scene")) CreateNewScene();
						if (ImGui::MenuItem("Object Definition")) CreateNewObjectDefinition();
						if (ImGui::MenuItem("Animation Clip")) CreateNewAnimationClip();
						if (ImGui::MenuItem("Material")) CreateNewMaterial();
						if (ImGui::MenuItem("FBX Import Settings")) CreateNewImportSettings();
						ImGui::EndMenu();
					}
					if (ImGui::BeginMenu("Assets"))
					{
						if (ImGui::MenuItem("Texture Importer..."))
							OpenTextureImporterFromSelection();
						ImGui::EndMenu();
					}
					// @todo: need to change this if we want it working, we need a document to get active scene
					//if (ImGui::BeginMenu("Run"))
					//{
					//	if (ImGui::MenuItem("Run game"))
					//	{
					//		ProjectRunControls::ExecuteRun(/* expects a document to know what to run */);
					//	}
					//	ImGui::EndMenu();
					//}


					if (ImGui::BeginMenu("ImGui"))
					{
						if (ImGui::MenuItem("Show Demo"))
						{
							locImGuiDemoOpen = true;
						}
						if (ImGui::MenuItem("Show Style Edit"))
						{
							locImGuiStyleEditorOpen = true;
						}
						ImGui::EndMenu();
					}
					ImGui::SameLine(ImGui::GetWindowWidth() - 100.f);
					char buff[20];

					//P4::ErrorType errorState = P4::QueryErrorState();
					ImVec4 textColor = locPerforceEnabled ? ImVec4(1.f, 1.f, 1.f, 1.0f) : ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
					sprintf_s(buff, "%s Perforce %s", ICON_LC_GIT_MERGE, locPerforceEnabled ? ICON_LC_TOGGLE_RIGHT : ICON_LC_TOGGLE_LEFT);

					ImGui::PushStyleColor(ImGuiCol_Text, P4::QueryErrorState() == P4::ErrorType::None ? textColor : ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
					if (ImGui::MenuItem(buff))
						{
						locPerforceEnabled = !locPerforceEnabled;

							if (locPerforceEnabled)
							{
								const std::string& rootPath = Settings::GameAssetRoot();
								P4::StartPolling(rootPath.c_str());
							}
							else
							{
								P4::StopPolling();
							}
						}
					ImGui::PopStyleColor();

					if (ImGui::IsItemHovered()){ // tooltip
						ImGui::BeginTooltip();
					{
							ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20);
							if (P4::QueryErrorState() == P4::ErrorType::None)
						{
								ImGui::TextWrapped("Perforce integration is %s", locPerforceEnabled ? "active" : "not active");
						}
							else
						{
								ImGui::TextWrapped("Perforce error!\n   \"%s\"", P4::GetErrorString());
						}
							ImGui::PopTextWrapPos();
					}
						ImGui::EndTooltip();
				}

					if (locPerforceEnabled && P4::QueryErrorState() != P4::ErrorType::None)
					{
						locPerforceEnabled = false;
						P4::StopPolling();
					}
				
					ImGui::EndMenuBar();
				}
			}
		}
		ImGui::End();
		
		myAssetBrowser.Draw();
		
		{
			/*if (FileDialog::IsActive()) {
				FileDialog::Draw();
			}*/
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);

			ImGui::SetNextWindowClass(Editor::GetEditor()->GetGlobalWindowClass());

			// TODO: hide name probably?
			ImGui::Begin(GlobalWindowNames[(size_t)GlobalWindows::DocumentDock]);

			ImGui::PopStyleVar(2);

			myDocumentDockSize = ImGui::GetContentRegionAvail();
			ImGui::DockSpace(myDocumentDockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode, &myDocumentLevelWindowClass);
			ImGui::End();
			
			std::erase_if(myOpenDocuments, [](const auto& document) { return document->GetState() == Document::State::CloseConfirmed; });

			if (myDocumentsPendingClose.size() > 0)
			{
				myDocumentsPendingClose[0]->Update(aTimeDelta, inputManager);
				if (ShowSavePromptModal())
				{
					myDocumentsPendingClose.erase(myDocumentsPendingClose.begin());
				}
			}
			else
			{
				for (size_t i = 0; i < myOpenDocuments.size(); i++)
				{
					auto& document = myOpenDocuments[i];

					if (document->GetState() == Document::State::CloseRequested)
					{
						if (document->HasUnsavedChanges())
						{
							document->SetState(Document::State::ClosePending);
							myDocumentsPendingClose.push_back(document.get());
						}
						else 
						{
							document->SetState(Document::State::CloseConfirmed);
						}
					}
					myActiveDocument = document.get();
					document->Update(aTimeDelta, inputManager);
				}
				if (numOpenDocuments < myOpenDocuments.size())
				{
					ImGui::SetWindowFocus(myOpenDocuments.back()->GetImGuiName().GetString());
				}
				myActiveDocument = nullptr;			
			}
		}	

		if (locImGuiDemoOpen) {
			ImGui::ShowDemoWindow(&locImGuiDemoOpen);
		}
		if (locImGuiStyleEditorOpen) {
			ImGui::Begin("imgui-style-editor", &locImGuiStyleEditorOpen);
			ImGui::ShowStyleEditor();
			ImGui::End();
		}
		DrawTextureImporter();
	}	
}

void Editor::AddDocument(std::unique_ptr<Document>&& ptr)
{
	auto it = std::find_if(myOpenDocuments.begin(), myOpenDocuments.end(),
		[&](auto& i) { 
			return i->GetImGuiName() == ptr->GetImGuiName();
		}
	);

	if (it == myOpenDocuments.end())
	{
		myOpenDocuments.push_back(std::move(ptr));
	}
	else
	{
		ImGui::SetWindowFocus(ptr->GetImGuiName().GetString());
	}
}

void Editor::FocusDocument(Document* document)
{
	for (int i = 0; i < myClosedDocuments.size(); i++)
	{
		if (myClosedDocuments[i].get() == document)
		{
			myOpenDocuments.push_back(nullptr);
			myClosedDocuments[i].swap(myOpenDocuments.back());
			myClosedDocuments.erase(myClosedDocuments.begin() + i);
		}
	}

	// Todo: make sure it is visible and focused also
}

void Editor::Save()
{
	for (int i = 0; i < myOpenDocuments.size(); i++)
	{
		myOpenDocuments[i]->Save();
	}

	// TODO: Should fix so that Revert is an undoable action
	// that way, documents can be closed without saving by reverting before closing
	// undoing will then undo the revert, but also open the document

	for (int i = 0; i < myClosedDocuments.size(); i++)
	{
		myClosedDocuments[i]->Save();
	}

	EditorScriptManager::GetInstance().SaveAll();
}

void Editor::OnAction(CommandManager::Action action)
{
	// Set up active document correctly for handling undo/redo
	// this way code running in undo/redo is able to query for the active document if needed

	if (action == CommandManager::Action::Do)
	{
		myRedoActiveDocumentStack.clear();
		myUndoActiveDocumentStack.push_back(myActiveDocument);

		if (myActiveDocument != nullptr)
		{
			myActiveDocument->OnAction(action);
		}
	}
	else if (action == CommandManager::Action::PreUndo)
	{
		myPreviousActiveDocument = myActiveDocument;
		myActiveDocument = myUndoActiveDocumentStack.back();

		if (myActiveDocument != nullptr)
		{
			FocusDocument(myActiveDocument);
			myActiveDocument->OnAction(action);
		}
	}
	else if (action == CommandManager::Action::PostUndo)
	{
		if (myActiveDocument != nullptr)
			myActiveDocument->OnAction(action);

		myActiveDocument = myPreviousActiveDocument;
		myPreviousActiveDocument = nullptr;

		myRedoActiveDocumentStack.push_back(myUndoActiveDocumentStack.back());
		myUndoActiveDocumentStack.pop_back();
	}
	else if (action == CommandManager::Action::PreRedo)
	{
		myPreviousActiveDocument = myActiveDocument;
		myActiveDocument = myRedoActiveDocumentStack.back();

		if (myActiveDocument != nullptr)
		{
			FocusDocument(myActiveDocument);
			myActiveDocument->OnAction(action);
		}
	}
	else if (action == CommandManager::Action::PostRedo)
	{
		if (myActiveDocument != nullptr)
			myActiveDocument->OnAction(action);

		myActiveDocument = myPreviousActiveDocument;
		myPreviousActiveDocument = nullptr;

		myUndoActiveDocumentStack.push_back(myRedoActiveDocumentStack.back());
		myRedoActiveDocumentStack.pop_back();
	}
	else if (action == CommandManager::Action::Clear)
	{
		myUndoActiveDocumentStack.clear();
		myRedoActiveDocumentStack.clear();

		if (myActiveDocument != nullptr)
		{
			myActiveDocument->OnAction(action);
		}
	}
}
