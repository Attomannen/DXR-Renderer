#pragma once

#include <tge/Graphics/RenderTarget.h>
#include <tge/scene/Scene.h>

#include <tge/editor/Document/Document.h>

#include <tge/editor/Tools/AssetBrowser/AssetBrowser.h>

#include <imgui.h>
#include <tge/editor/CommandManager/CommandManager.h>

#include <tge/scene/SceneObjectDefinitionManager.h>
#include <tge/editor/Scene/EditorSceneManager.h>

#include <tge/editor/EditorConfiguration.h>

#include <tge/editor/EditorGraphics/EditorGraphicsBase.h>

namespace Tga
{ 
class SpriteShader;
class InputManager;


enum class GlobalWindows
{
	DocumentDock,
	AssetBrowserFiles,
	AssetBrowserDirectories,
	Count,
};


constexpr const char* GlobalWindowNames[] =
{
	"Documents",
	"Asset Browser - Files",
	"Asset Browser - Directories",
};

class Editor
{
public:
	static Editor* GetEditor();

	Editor();
	~Editor();

	void Init(const EditorConfiguration& editorConfiguration, std::unique_ptr<EditorGraphicsBase>&& graphics );
	void Update(float aTimeDelta, InputManager &inputManager);

	void AddDocument(std::unique_ptr<Document>&& ptr);

	const ImGuiWindowClass* GetGlobalWindowClass() const { return &myTopLevelWindowClass; }
	const ImGuiWindowClass* GetDocumentWindowClass() const { return &myDocumentLevelWindowClass; }

	const ImGuiID& GetDocumentDockSpaceId() const { return myDocumentDockSpaceId; }
	const ImVec2& GetDocumentDockSpaceSize() const { return myDocumentDockSize; }

	Tga::SceneObjectDefinitionManager& GetSceneObjectDefinitionManager() { return mySceneObjectDefinitionManager; }
	Tga::EditorSceneManager& GetEditorSceneManager() { return myEditorSceneManager; }

	Tga::AssetBrowser& GetAssetBrowser() { return myAssetBrowser; }
	void FocusDocument(Document* document);

	void CreateNewScene();
	void CreateNewObjectDefinition();
	void CreateNewAnimationClip();
	void CreateNewMaterial();
	void CreateNewImportSettings();

	bool IsViewportGridVisible() { return myIsViewportGridVisible; }

	void Save();

	const EditorConfiguration& GetEditorConfiguration() { return myEditorConfiguration; }
	const EditorGraphicsBase& GetEditorGraphics() const  { return *myEditorGraphics; }
private:
	bool ShowSavePromptModal();

	friend void CommandManagerEditorCallback(CommandManager::Action);
	void OnAction(CommandManager::Action action);

	EditorConfiguration myEditorConfiguration;
	std::unique_ptr<EditorGraphicsBase> myEditorGraphics;

	AssetBrowser myAssetBrowser;

	Tga::SceneObjectDefinitionManager mySceneObjectDefinitionManager;
	Tga::EditorSceneManager myEditorSceneManager;

	std::vector<std::unique_ptr<Document>> myOpenDocuments;
	std::vector<Document*> myDocumentsPendingClose;

	std::vector<std::unique_ptr<Document>> myClosedDocuments;

	// Todo: all this complexity should be encapsulated

	ImGuiWindowClass myTopLevelWindowClass;
	ImGuiWindowClass myDocumentLevelWindowClass;

	ImGuiID			 myGlobalDockSpaceId;
	ImGuiID			 myDocumentDockSpaceId;

	ImVec2 myDocumentDockSize;

	Document* myActiveDocument = nullptr;
	Document* myPreviousActiveDocument = nullptr;
	std::vector<Document*> myUndoActiveDocumentStack;
	std::vector<Document*> myRedoActiveDocumentStack;

	bool myIsDockingInitialized = false;
	bool myIsViewportGridVisible = true;
};

}
