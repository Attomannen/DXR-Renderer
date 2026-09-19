#pragma once

#include <unordered_set>

#include <age/editor/Document/Document.h>
#include <age/editor/Scene/SceneSelection.h>
#include <age/editor/Tools/Viewport/Viewport.h>
#include <age/scene/SceneObjectDefinition.h>
#include <age/script/ScriptRuntimeInstance.h>
#include <age/editor/ScriptEditor/ScriptEditor.h>

#include "age/animation/Pose.h"
#include <age/editor/EditorGraphics/EditorGraphicsBase.h>

namespace Ag
{ 

enum class LivePreviewMode
{
	Stopped,
	Paused,
	Running
};

struct LivePreviewData
{
	LivePreviewMode mode;
	ScriptPinId pinToTrigger;
	int frameNumber;

	std::unordered_map<StringId, ModelSpacePose> poses;

	std::unordered_map<StringId, Property> dynamicProperties;
	std::unordered_map<StringId, Property> staticProperties;

	std::unique_ptr<ScriptRuntimeInstance> graph; // the object's event graph while it runs
};

class ObjectDefinitionDocument : public Document, public ViewportInterface
{
public:
	// A Blueprint-style editor: Components and My Blueprint on the left, Details on the right,
	// Viewport and Event Graph as tabs in the middle, preview panels at the bottom right.
	enum class Panels
	{
		Components,
		MyBlueprint,
		Details,
		Viewport,
		EventGraph,
		Count
	};

	void Init(std::string_view path) override;
	void Update(float aTimeDelta, InputManager& inputManager) override;
	void Save() override;
	void OnAction(CommandManager::Action action) override;

	void HandleDrop() override;
	void BeginDragSelection(Vector2f mousePos) override;
	void EndDragSelection(Vector2f mousePos, bool isShiftDown) override;
	void ClickSelection(Vector2f mousePos, uint32_t selectedId, bool isShiftDown) override;

	void BeginTransformation() override;
	void UpdateTransformation(const Vector3f& referencePosition, const Matrix4x4f& transform) override;
	void EndTransformation() override;

	Vector3f CalculateSelectionPosition() override;
	Matrix4x4f CalculateSelectionOrientation() override;

	virtual bool HasTransformableSelection() override;
private:
	void DrawToolbar();
	void DrawComponentsPanel();
	void DrawMyBlueprintPanel();
	void DrawDetailsPanel();
	void DrawAndUpdateLivePreview(float aTimeDelta);

	SceneObjectDefinition* myObjectDefinition;

	StringId mySelectedProperty;
	ScriptGraphEditor myGraphEditor;

	EditorViewport myViewport;

	LivePreviewData myLivePreviewData;
	std::unique_ptr<ObjectDefinitionEditorGraphicsBase> myGraphics;

	bool myIsDockingInitialized = false;
	bool myShowEventGraphNextFrame = false;

	std::string myPanelWindowNames[(size_t)Panels::Count];
};

}