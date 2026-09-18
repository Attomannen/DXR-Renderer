#include "stdafx.h"

#include <commdlg.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <tge/editor/Editor.h>
#include <tge/editor/EditorSettings.h>
#include <IconFontHeaders/IconsLucide.h>

#include <tge/animation/AnimationClip.h>
#include <tge/input/InputManager.h>
#include <tge/editor/CommandManager/CommandManager.h>
#include <tge/editor/CommandManager/AbstractCommand.h>
#include <tge/graphics/DX11.h>
#include <tge/settings/settings.h>
#include <tge/imgui/ImGuiInterface.h>
#include <tge/imgui/ImGuiPropertyEditor.h>
#include <tge/scene/SceneSerialize.h>

#include <tge/script/ScriptRuntimeInstance.h>

#include <tge/animation/Script/AnimationNodes.h>

#include <tge/editor/AnimationClip/AnimationClipDocument.h>
#include <tge/editor/ObjectDefinition/ObjectDefinitionDocument.h>
#include <tge/editor/Scene/SceneDocument.h>
#include <tge/editor/Material/MaterialDocument.h>
#include <tge/editor/Import/FbxConvert.h>
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
#include <tge/script/Nodes/GameObjectNodes.h>
#include <tge/script/Nodes/MathExtraNodes.h>
#include <tge/script/Nodes/SceneObjectNodes.h>
#include <tge/scene/ScenePropertyTypes.h>

#include <tge/editor/EditorGraphics/NullEditorGraphics.h>

static bool locImGuiDemoOpen = false;
static bool locImGuiStyleEditorOpen = false;
static bool locTextureImporterOpen = false;
static char locTextureInput[512]{};
static char locTextureOutput[512]{};
static bool locTextureNormalsDx = false;
static bool locTextureRecursive = true;
static bool locTextureForce = false;
static std::string locTextureCookStatus;

using namespace Tga;

static void ListProjectAssets(std::span<const char* const> extensions, std::vector<std::string>& out)
{
	Editor::GetEditor()->GetContentBrowser().ListAssets(extensions, out);
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
	Tga::RegisterMathExtraNodes();
	Tga::RegisterGameObjectNodes();
	Tga::RegisterAnimationNodes();

	RegisterExampleNodes();

	Tga::PropertyEditor::RegisterAssetListFunction(ListProjectAssets);

	// Defaults to setting the game project to default, perhaps not optimal, and could lead to problems if we move things around
	// but I believe that right now the use-case is that we are going to couple the editor to the game-project. And at this time
	// we dont really have the situation where we might want to set another project.. so for now and for convenience.
	
	const std::string& rootPath = Settings::GameAssetRoot();

	myContentBrowser.SetPath(rootPath);
	mySceneObjectDefinitionManager.Init(rootPath);

	EditorSettings::Load();
	myIsViewportGridVisible = EditorSettings::Get().viewportGridVisible;
	myForceDockLayoutRebuild = EditorSettings::Get().dockLayoutVersion != DockLayoutVersion;
	myIsCollisionVisible = EditorSettings::Get().viewportCollisionVisible;

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


SceneDocument* Tga::Editor::GetLevelDocument()
{
	for (const std::unique_ptr<Document>& document : myOpenDocuments)
		if (SceneDocument* level = dynamic_cast<SceneDocument*>(document.get()))
			if (level->GetState() != Document::State::CloseConfirmed)
				return level;
	return nullptr;
}

void Tga::Editor::OpenLevel(const fs::path& path)
{
	const fs::path relative = path.is_absolute() ? fs::relative(path, Settings::GameAssetRoot()) : path;
	const std::string relativeString = relative.string();

	SceneDocument* current = GetLevelDocument();
	if (current && current->GetPath() == relativeString)
		return;

	if (current)
	{
		// Closing goes through the usual save prompt; the new level opens once it is gone.
		current->SetState(Document::State::CloseRequested);
		myPendingLevel = relativeString;
		myPendingLevelSawClose = false;
		return;
	}
	OpenLevelNow(relativeString);
}

void Tga::Editor::OpenLevelNow(const std::string& relativePath)
{
	try
	{
		std::unique_ptr<SceneDocument> document = std::make_unique<SceneDocument>();
		document->Init(relativePath);
		AddDocument(std::move(document));

		EditorSettings::Get().lastLevel = relativePath;
		EditorSettings::Save();
	}
	catch (const std::exception& e)
	{
		ERROR_PRINT("Could not open level '%s': %s", relativePath.c_str(), e.what());
	}
}

void Tga::Editor::OpenStartupLevel()
{
	const fs::path root = fs::absolute(Settings::GameAssetRoot());
	const std::string& last = EditorSettings::Get().lastLevel;
	if (!last.empty() && fs::exists(root / last))
	{
		OpenLevelNow(last);
		return;
	}

	std::error_code error;
	for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, error))
	{
		if (!entry.is_regular_file() || entry.path().extension() != ".tgs")
			continue;
		bool trashed = false;
		for (const auto& part : entry.path())
			trashed |= part == ".trash";
		if (trashed)
			continue;
		OpenLevelNow(fs::relative(entry.path(), root).string());
		return;
	}

	// A project with no level yet still gets one to work in.
	CreateNewScene(root / "Untitled.tgs");
}

std::string Tga::Editor::CreateNewScene(const fs::path& path)
{
	fs::path p = path;
	if (p.extension().empty())
		p.replace_extension(".tgs");
	if (fs::exists(p))
		return "'" + p.filename().string() + "' already exists";

	const fs::path relativePath = fs::relative(p, Settings::GameAssetRoot());

	Scene scene;
	scene.SetName(p.filename().string().c_str());
	scene.SetPath(relativePath.string().c_str());
	SaveScene(scene);

	OpenLevel(p);
	return {};
}

std::string Tga::Editor::CreateNewObjectDefinition(const fs::path& path)
{
	fs::path p = path;
	if (p.extension().empty())
		p.replace_extension(".tgo");
	if (fs::exists(p))
		return "'" + p.filename().string() + "' already exists";

	const fs::path relativePath = fs::relative(p, Settings::GameAssetRoot());

	// Written right away, so the new TGO shows in the Content Browser before anything is edited.
	SceneObjectDefinition* definition = mySceneObjectDefinitionManager.CreateOrGet(relativePath.string());
	if (!definition)
		return "Could not create '" + p.filename().string() + "'";
	definition->Save();

	std::unique_ptr<ObjectDefinitionDocument> document = std::make_unique<ObjectDefinitionDocument>();
	document->Init(p.string());
	AddDocument(std::move(document));
	return {};
}

std::string Tga::Editor::CreateNewMaterial(const fs::path& path)
{
	fs::path p = path;
	if (p.extension().empty())
		p.replace_extension(".tgmat");
	if (fs::exists(p))
		return "'" + p.filename().string() + "' already exists";

	// Write a default material so the file exists before the document loads it.
	MaterialAsset{}.Save(p.string());

	std::unique_ptr<MaterialDocument> document = std::make_unique<MaterialDocument>();
	document->Init(p.string());
	AddDocument(std::move(document));
	return {};
}

std::string Tga::Editor::CreateNewAnimationClip(const fs::path& path)
{
	fs::path p = path;
	if (p.extension().empty())
		p.replace_extension(".tgac");
	if (fs::exists(p))
		return "'" + p.filename().string() + "' already exists";

	const fs::path relativePath = fs::relative(p, Settings::GameAssetRoot());
	GetOrCreateAnimationClip(StringRegistry::RegisterOrGetString(relativePath.string()));

	std::unique_ptr<AnimationClipDocument> document = std::make_unique<AnimationClipDocument>();
	document->Init(p.string());
	AddDocument(std::move(document));
	return {};
}

static void OpenTextureImporterFromSelection()
{
	const std::filesystem::path root = Tga::Settings::GameAssetRoot();
	std::filesystem::path selected = Editor::GetEditor()->GetContentBrowser().GetSelectedAsset().GetString();
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
		// from current_path()/Bin produced Bin/Bin/TextureCooker_*.exe. Resolve
		// beside the actual host executable instead; this also survives launching
		// the editor from Visual Studio or a shortcut with another working folder.
		// FindTextureCookerExe() also stops this hardcoding "_Debug": a Release
		// editor used to look for a cooker exe it would never have shipped.
		const std::filesystem::path exe = FindTextureCookerExe();
		if (exe.empty())
		{
			locTextureCookStatus = "No TextureCooker_{Debug,Release,Retail}.exe was found beside GameEditor.";
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
		else locTextureCookStatus = "Could not launch " + exe.filename().string() + ".";
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

			ImGuiID center = 0, bottom = 0;
			if (!myIsDockingInitialized)
			{
				// ImGui::DockSpace() above already auto-creates an empty leaf
				// node here if none exists, so "a node exists" alone doesn't
				// mean imgui.ini restored a real saved layout -- check it was
				// actually split. Previously this unconditionally rebuilt the
				// hardcoded default every single launch, discarding whatever
				// panel arrangement the user had saved; now that only happens
				// on a genuine first run, or after View > Reset Layout clears
				// myIsDockingInitialized.
				ImGuiDockNode* existingNode = ImGui::DockBuilderGetNode(myGlobalDockSpaceId);
				if (!myForceDockLayoutRebuild && existingNode && existingNode->IsSplitNode())
				{
					myIsDockingInitialized = true;
				}
				else
				{
					ImGui::DockBuilderRemoveNode(myGlobalDockSpaceId); // clear any previous layout
					ImGui::DockBuilderAddNode(myGlobalDockSpaceId, ImGuiDockNodeFlags_DockSpace);
					ImGui::DockBuilderSetNodeSize(myGlobalDockSpaceId, viewport->WorkSize);
					center = myGlobalDockSpaceId;

					ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, &bottom, &center);

					ImGui::DockBuilderDockWindow(GlobalWindowNames[(size_t)GlobalWindows::DocumentDock], center);
					ImGui::DockBuilderDockWindow(ContentBrowserWindowName, bottom);

					ImGui::DockBuilderFinish(myGlobalDockSpaceId);

					myIsDockingInitialized = true;
					if (myForceDockLayoutRebuild)
					{
						myForceDockLayoutRebuild = false;
						EditorSettings::Get().dockLayoutVersion = DockLayoutVersion;
						EditorSettings::Save();
					}
				}
			}

			//////////////////////////
			// Menu 
			{
				if (ImGui::BeginMenuBar())
				{
					if (ImGui::BeginMenu("File"))
					{
						if (ImGui::MenuItem("Save All", "Ctrl+S"))
						{
							Save();
						}
						ImGui::Separator();
						if (ImGui::MenuItem("New Level..."))
							myContentBrowser.RequestNewLevel();
						if (ImGui::BeginMenu("Open Level"))
						{
							const fs::path root = fs::absolute(Settings::GameAssetRoot());
							std::error_code error;
							int count = 0;
							for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, error))
							{
								if (!entry.is_regular_file() || entry.path().extension() != ".tgs")
									continue;
								bool trashed = false;
								for (const auto& part : entry.path())
									trashed |= part == ".trash";
								if (trashed)
									continue;
								++count;
								const fs::path relative = fs::relative(entry.path(), root);
								if (ImGui::MenuItem(fs::path(relative).replace_extension("").generic_string().c_str()))
									OpenLevel(entry.path());
							}
							if (count == 0)
								ImGui::TextDisabled("No levels yet");
							ImGui::EndMenu();
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
						ImGui::Separator();
						ImGui::MenuItem("Undo History", nullptr, &myShowUndoHistory);

						ImGui::EndMenu();
					}
					if (ImGui::BeginMenu("View"))
					{
						if (ImGui::MenuItem("Render Viewport Grid", NULL, &myIsViewportGridVisible))
						{
							EditorSettings::Get().viewportGridVisible = myIsViewportGridVisible;
							EditorSettings::Save();
						}
						if (ImGui::MenuItem("Show Collision", NULL, &myIsCollisionVisible))
						{
							EditorSettings::Get().viewportCollisionVisible = myIsCollisionVisible;
							EditorSettings::Save();
						}
						ImGui::EndMenu();
					}
					if (ImGui::BeginMenu("Window"))
					{
						// Rebuilds the default arrangement of the panels on the next frame.
						if (ImGui::MenuItem("Reset Layout"))
							myIsDockingInitialized = false;
						ImGui::EndMenu();
					}
					if (ImGui::BeginMenu("Tools"))
					{
						if (ImGui::MenuItem("Texture Importer..."))
							OpenTextureImporterFromSelection();
						ImGui::Separator();
						if (ImGui::BeginMenu("Dear ImGui"))
						{
							if (ImGui::MenuItem("Demo"))
								locImGuiDemoOpen = true;
							if (ImGui::MenuItem("Style Editor"))
								locImGuiStyleEditorOpen = true;
							ImGui::EndMenu();
						}
						ImGui::EndMenu();
					}

					ImGui::EndMenuBar();
				}
			}
		}
		ImGui::End();
		
		myContentBrowser.Draw();
		
		{
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

			if (!myStartupLevelOpened)
			{
				myStartupLevelOpened = true;
				OpenStartupLevel();
			}
			if (!myPendingLevel.empty())
			{
				SceneDocument* level = GetLevelDocument();
				if (!level)
				{
					OpenLevelNow(myPendingLevel);
					myPendingLevel.clear();
				}
				else if (level->GetState() != Document::State::Open)
				{
					myPendingLevelSawClose = true;
				}
				else if (myPendingLevelSawClose)
				{
					myPendingLevel.clear(); // the save prompt was cancelled: stay on this level
				}
			}

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
		DrawUndoHistoryPanel();
	}
}

void Editor::DrawUndoHistoryPanel()
{
	if (!myShowUndoHistory) return;
	if (!ImGui::Begin("Undo History", &myShowUndoHistory))
	{
		ImGui::End();
		return;
	}

	const std::vector<const AbstractCommand*> undone = CommandManager::GetUndoHistory();
	const std::vector<const AbstractCommand*> redoable = CommandManager::GetRedoHistory();

	// "Current position" row: every entry above it is already applied
	// (undoing walks up from here), every entry below is undone-but-
	// redoable (redoing walks down into it). Clicking a past entry undoes
	// down to it; clicking a future entry redoes up to it -- Undo()/Redo()
	// already exist and do the Execute()/Undo() + callback dispatch
	// correctly one step at a time, so jumping several steps is just
	// calling one of them in a loop rather than needing new CommandManager
	// plumbing.
	for (size_t i = 0; i < undone.size(); ++i)
	{
		ImGui::PushID((int)i);
		const bool isCurrent = (i + 1 == undone.size());
		if (ImGui::Selectable(undone[i]->GetName(), isCurrent))
		{
			const size_t steps = undone.size() - 1 - i;
			for (size_t s = 0; s < steps; ++s) CommandManager::Undo();
		}
		ImGui::PopID();
	}
	if (undone.empty())
		ImGui::TextDisabled("(nothing to undo)");

	ImGui::Separator();

	for (size_t i = 0; i < redoable.size(); ++i)
	{
		ImGui::PushID((int)(undone.size() + i));
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
		if (ImGui::Selectable(redoable[i]->GetName()))
		{
			const size_t steps = i + 1;
			for (size_t s = 0; s < steps; ++s) CommandManager::Redo();
		}
		ImGui::PopStyleColor();
		ImGui::PopID();
	}

	ImGui::End();
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

bool Editor::IsDocumentOpen(const Document* aDocument) const
{
	// Checks both lists, not just myOpenDocuments: FocusDocument can move a
	// unique_ptr from myClosedDocuments back into myOpenDocuments without
	// reallocating (reopening), so a document sitting in myClosedDocuments
	// is still a live object at the same address, just not currently shown
	// -- exactly what matters here is "not yet destructed," not "visible."
	auto find = [aDocument](const std::vector<std::unique_ptr<Document>>& list)
	{
		return std::find_if(list.begin(), list.end(), [aDocument](const std::unique_ptr<Document>& d) { return d.get() == aDocument; }) != list.end();
	};
	return find(myOpenDocuments) || find(myClosedDocuments);
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

	// myClosedDocuments is never actually populated today: a confirmed close
	// (Document::State::CloseConfirmed) is erased straight out of
	// myOpenDocuments further up in Update(), not moved here. This used to
	// loop over it and save it anyway -- currently a no-op since the vector
	// is always empty, but surprising to read and a live "Ctrl+S saves a
	// document you just closed without saving" bug waiting to happen the
	// moment something (e.g. a future "reopen last closed" feature) starts
	// populating myClosedDocuments without also revisiting this. Left out
	// deliberately rather than silently kept "for when that's wired up".

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
