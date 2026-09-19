#pragma once

#include <age/editor/Document/Document.h>
#include <age/editor/Tools/Viewport/Viewport.h>
#include <age/editor/EditorGraphics/EditorGraphicsBase.h>
#include <age/editor/Material/MaterialAsset.h>
#include <age/editor/Material/Graph/MaterialGraph.h>
#include <set>

namespace ax { namespace NodeEditor { struct EditorContext; } }

namespace Ag
{

// Unreal-style material editor tab: a 3D PBR preview of a mesh rendered with the
// material being edited, plus a properties panel and preview-lighting settings.
// The material is a self-contained .tgmat JSON file (no asset manager).
class MaterialDocument : public Document, public ViewportInterface
{
public:
	enum class Panels
	{
		Preview,
		Properties,
		Graph,
		Count
	};

	void Init(std::string_view path) override;
	void Update(float aTimeDelta, InputManager& inputManager) override;
	void Save() override;
	void OnAction(CommandManager::Action action) override;

	// For ChangeMaterialCommand's Execute()/Undo() -- not for general use,
	// hence not exposed as a "SetMaterial"-looking public setter elsewhere.
	void SetMaterialAsset(const MaterialAsset& aMaterial) { myMaterial = aMaterial; }
	// Document has no virtual destructor even though Editor stores documents
	// as unique_ptr<Document> (Editor.h), so a MaterialDocument destructor
	// here would never actually run -- Close() is the real, existing
	// end-of-life hook, so the node editor context is freed there instead.
	void Close() override;

	// ViewportInterface (no selection / gizmos in the material preview).
	void HandleDrop() override {}
	void BeginDragSelection(Vector2f) override {}
	void EndDragSelection(Vector2f, bool) override {}
	void ClickSelection(Vector2f, uint32_t, bool) override {}
	void BeginTransformation() override {}
	void UpdateTransformation(const Vector3f&, const Matrix4x4f&) override {}
	void EndTransformation() override {}
	Vector3f CalculateSelectionPosition() override { return {}; }
	Matrix4x4f CalculateSelectionOrientation() override { return {}; }
	bool HasTransformableSelection() override { return false; }

private:
	void DrawToolbar();
	void DrawProperties();
	void DrawGraph();
	void DrawGraphNode(MaterialGraphNS::Node& node);
	void Bake();

	MaterialAsset myMaterial;
	std::string myName;
	// DrawProperties() batches every widget changed in one frame (there is no
	// per-field IsItemDeactivatedAfterEdit tracking there, unlike the scene
	// inspector) into a single undo command per edit *session*: myUndoSnapshot
	// is captured the moment an edit starts and pushed as ChangeMaterialCommand's
	// "old" value once no widget in the panel is still active, rather than
	// pushing one command per changed frame while e.g. a colour is being dragged.
	MaterialAsset myUndoSnapshot;
	bool myHasPendingMaterialEdit = false;

	// Node-graph authoring layer, opt-in per material -- see MaterialGraph.h.
	// Stored as a sidecar ".tgmatgraph" next to the .tgmat, loaded/created in
	// Init() and saved explicitly (not on every Document::Save(), since baking
	// is the action that actually matters -- the graph itself autosaves right
	// after a successful bake and whenever the graph topology changes).
	MaterialGraphNS::MaterialGraph myGraph;
	std::string myGraphPath;
	bool myHasGraph = false;
	ax::NodeEditor::EditorContext* myGraphEditorContext = nullptr;
	MaterialGraphNS::Id mySelectedNode = MaterialGraphNS::kInvalidId;
	int myBakeWidth = 1024;
	int myBakeHeight = 1024;
	// Nodes ImNodes has already been told the position of at least once --
	// see DrawGraph()/DrawGraphNode()'s comment on why this can only happen
	// once per node, not every frame.
	std::set<MaterialGraphNS::Id> myGraphPositionedNodes;
	// The path handed to Init() by the Content Browser is relative to the game
	// asset root (see ContentBrowser.cpp's fs::relative(absPath, root)), not
	// something openable via a bare std::ifstream from the process's working
	// directory. This is the actual absolute path Load()/Save() use instead --
	// see MaterialDocument.cpp's Init()/Save() for why myPath itself can't be
	// repurposed for this (it's also the document's stable identity/title key).
	std::string myResolvedPath;

	EditorViewport myViewport;
	std::unique_ptr<MaterialEditorGraphicsBase> myGraphics;

	bool myIsDockingInitialized = false;
	std::string myPanelWindowNames[(size_t)Panels::Count];
};

}
