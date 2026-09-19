#pragma once
#include <age/settings/GameSettings.h>

#include <age/Graphics/RenderTarget.h>
#include <age/scene/Scene.h>

#include <age/editor/Document/Document.h>

#include <age/editor/Tools/ContentBrowser/ContentBrowser.h>

#include <imgui.h>
#include <age/editor/CommandManager/CommandManager.h>

#include <age/scene/SceneObjectDefinitionManager.h>
#include <age/editor/Scene/EditorSceneManager.h>

#include <age/editor/EditorConfiguration.h>

#include <age/editor/EditorGraphics/EditorGraphicsBase.h>

namespace Ag
{ 
class SpriteShader;
class InputManager;
class SceneDocument;


enum class GlobalWindows
{
	DocumentDock,
	Count,
};


constexpr const char* GlobalWindowNames[] =
{
	"Documents",
};

// The Content Browser window's name, for docking it.
constexpr const char* ContentBrowserWindowName = "Content Browser";
// Bumped whenever the default dock layout changes, so a layout saved by an older editor is
// rebuilt instead of leaving windows floating.
constexpr int DockLayoutVersion = 4;

class Editor
{
public:
	static Editor* GetEditor();

	Editor();
	~Editor();

	void Init(const EditorConfiguration& editorConfiguration, std::unique_ptr<EditorGraphicsBase>&& graphics );
	void Update(float aTimeDelta, InputManager &inputManager);

	void AddDocument(std::unique_ptr<Document>&& ptr);
	// For commands that capture a raw Document* (e.g. ChangeMaterialCommand):
	// the global undo stack is never pruned when a document closes (a known
	// gap, roadmap "no limit"), so a command built while a document was open
	// can still be sitting on the stack after it's gone. Check this before
	// dereferencing a captured pointer in Execute()/Undo() rather than
	// assuming it's still valid.
	bool IsDocumentOpen(const Document* aDocument) const;

	const ImGuiWindowClass* GetGlobalWindowClass() const { return &myTopLevelWindowClass; }
	const ImGuiWindowClass* GetDocumentWindowClass() const { return &myDocumentLevelWindowClass; }

	const ImGuiID& GetDocumentDockSpaceId() const { return myDocumentDockSpaceId; }
	const ImVec2& GetDocumentDockSpaceSize() const { return myDocumentDockSize; }

	Ag::SceneObjectDefinitionManager& GetSceneObjectDefinitionManager() { return mySceneObjectDefinitionManager; }
	Ag::EditorSceneManager& GetEditorSceneManager() { return myEditorSceneManager; }

	Ag::ContentBrowser& GetContentBrowser() { return myContentBrowser; }
	void FocusDocument(Document* document);

	// The editor always has one level open. Opening another replaces it (after the save prompt
	// if it has unsaved changes). The path is absolute or relative to the asset root.
	void OpenLevel(const fs::path& path);

	// Create an asset at the given path (absolute) and open it. Return an error message, or empty.
	std::string CreateNewScene(const fs::path& path);
	std::string CreateNewObjectDefinition(const fs::path& path);
	std::string CreateNewAnimationClip(const fs::path& path);
	std::string CreateNewParticleSystem(const fs::path& path);
	// A .tgo with a Game Mode component (opens in the TGO editor, where its rules are scripted).
	std::string CreateNewGameMode(const fs::path& path);
	// The Player Start marker object, created in Framework/PlayerStart.tgo the first time it is needed.
	SceneObjectDefinition* EnsurePlayerStartDefinition();
	std::string CreateNewMaterial(const fs::path& path);

	bool IsViewportGridVisible() { return myIsViewportGridVisible; }
	bool IsCollisionVisible() { return myIsCollisionVisible; }

	void Save();

	const EditorConfiguration& GetEditorConfiguration() { return myEditorConfiguration; }
	const EditorGraphicsBase& GetEditorGraphics() const  { return *myEditorGraphics; }
private:
	void OpenLevelNow(const std::string& relativePath);
	void OpenStartupLevel();
	SceneDocument* GetLevelDocument();

	bool myStartupLevelOpened = false;
	std::string myPendingLevel;          // the level to open once the current one has closed
	bool myPendingLevelSawClose = false; // the current level went through its close prompt

	bool ShowSavePromptModal();
	void DrawUndoHistoryPanel();
	void DrawProjectSettingsPanel();

	friend void CommandManagerEditorCallback(CommandManager::Action);
	void OnAction(CommandManager::Action action);

	EditorConfiguration myEditorConfiguration;
	std::unique_ptr<EditorGraphicsBase> myEditorGraphics;

	ContentBrowser myContentBrowser;

	Ag::SceneObjectDefinitionManager mySceneObjectDefinitionManager;
	Ag::EditorSceneManager myEditorSceneManager;

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
	bool myForceDockLayoutRebuild = false;
	bool myIsViewportGridVisible = true;
	bool myIsCollisionVisible = false;
	bool myShowUndoHistory = false;
	bool myShowProjectSettings = false;
	GameSettings myGameSettings;
	bool myGameSettingsLoaded = false;
};

}
