#pragma once

#include <unordered_set>

#include <tge/editor/Document/Document.h>
#include <tge/editor/Scene/SceneSelection.h>
#include <tge/editor/Tools/Viewport/Viewport.h>
#include <tge/scene/SceneObjectDefinition.h>
#include <tge/script/ScriptRuntimeInstance.h>

#include "tge/animation/Pose.h"
#include <tge/editor/EditorGraphics/EditorGraphicsBase.h>

namespace Tga
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

	std::unordered_set<StringId> enabledScripts;
	std::vector<std::pair<StringId, ScriptRuntimeInstance>> scriptInstances;
};

class ObjectDefinitionDocument : public Document, public ViewportInterface
{
public:
	enum class Panels
	{
		ObjectDefinition,
		Properties,
		Viewport,
		Script,
		VisualPreviewSettings,
		LivePreview,
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
	void DrawObjectDefinitionPanel();
	void DrawPropertyPanel();
	void DrawAndUpdateLivePreview(float aTimeDelta);

	SceneObjectDefinition* myObjectDefinition;

	StringId mySelectedProperty;
	std::string mySelectedScript;
	std::string myActiveScript;
	std::string myPrevActiveScript;

	EditorViewport myViewport;

	LivePreviewData myLivePreviewData;
	std::unique_ptr<ObjectDefinitionEditorGraphicsBase> myGraphics;

	bool myIsDockingInitialized = false;

	std::string myPanelWindowNames[(size_t)Panels::Count];
};

}