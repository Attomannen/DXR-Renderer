#pragma once
#include <age/Graphics/RenderTarget.h>
#include <age/Graphics/DepthBuffer.h>
#include <age/scene/Scene.h>
#include <age/editor/CommandManager/CommandManager.h>

#include <imgui.h>

namespace Ag
{ 
	class InputManager;

class ViewportInterface
{
public:
	virtual void HandleDrop() = 0;
	virtual void BeginDragSelection(Vector2f mousePos) = 0;
	virtual void EndDragSelection(Vector2f mousePos, bool isShiftDown) = 0;
	virtual void ClickSelection(Vector2f mousePos, uint32_t selectedId, bool isShiftDown) = 0;

	virtual void BeginTransformation() = 0;
	virtual void UpdateTransformation(const Vector3f& referencePosition, const Matrix4x4f& transform) = 0;
	virtual void EndTransformation() = 0;
	virtual Vector3f CalculateSelectionPosition() = 0;
	virtual Matrix4x4f CalculateSelectionOrientation() = 0;

	virtual bool HasTransformableSelection() = 0;

	// Draws collision wireframes over the viewport (see CollisionOverlay). Optional.
	virtual void DrawCollisionOverlay(class CollisionOverlay& /*overlay*/) {}
};


class Document
{
public:
	enum class State { Open, CloseRequested, ClosePending, CloseConfirmed };
protected:
	std::string myPath;
	StringId myImGuiName;
	int myId;

	State myState = State::Open;

	int myUndoStackSize = 0;
	int mySaveUndoStackSize = 0;
	ImGuiWindowClass myDocumentWindowClass; // all windows in one document have a shared document class. This restricts their docking to eachother 

public:
	Document();
	// Editor.h stores every open document as unique_ptr<Document> (myOpenDocuments/
	// myClosedDocuments) and destroys them through that base pointer -- without a
	// virtual destructor here, a derived document's own destructor (and its
	// members' destructors) never ran on close, only on process exit. Confirmed
	// live this session: MaterialDocument had to route ImNodes context cleanup
	// through Close() instead of its destructor to work around exactly this.
	virtual ~Document() = default;
	int GetId() { return myId; };
	bool HasUnsavedChanges() const;
	const State GetState() const { return myState; }
	void SetState(const State aState) { myState = aState; }


	virtual void Close() {};
	virtual void Init(std::string_view path);

	virtual void Update(float aTimeDelta, InputManager& inputManager) = 0;
	virtual void Save() = 0;
	virtual void OnAction(CommandManager::Action action) { action; };

	std::string_view GetPath() const { return myPath; }
	StringId GetImGuiName() const { return myImGuiName; }

	const ImGuiWindowClass* GetWindowClass() { return &myDocumentWindowClass; }
};

}