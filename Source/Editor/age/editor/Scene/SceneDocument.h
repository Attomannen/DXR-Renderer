#pragma once
#include <age/Graphics/RenderTarget.h>
#include <age/Graphics/DepthBuffer.h>
#include <age/scene/Scene.h>

#include <age/editor/Tools/Navmesh/NavmeshCreationTool.h>
#include <age/editor/Tools/SceneObjectProperties/SceneObjectProperties.h>
#include <age/editor/Tools/ContentBrowser/ContentBrowser.h>
#include <age/editor/Tools/SceneObjectList/SceneObjectList.h>
#include <age/editor/Tools/Viewport/Viewport.h>
#include <age/editor/Tools/ViewportGrid/ViewportGrid.h>

#include <imgui.h>

#include <age/editor/Document/Document.h>
#include <age/editor/Scene/SceneSelection.h>
#include <age/scene/SceneSerialize.h>

#include <age/editor/EditorGraphics/EditorGraphicsBase.h>

namespace Ag
{

	class InputManager;

class SceneDocument : public Document, public ViewportInterface
{
public:
	enum class Panels
	{
		Viewport,
		Instances,
		Properties,
		ToolSettings,
		NavmeshCreationTool,
		Count
	};

	void Close() override;
	void Init(std::string_view path) override;
	void Update(float aTimeDelta, InputManager& inputManager) override;
	void Save() override;
	void OnAction(CommandManager::Action action) override;

	void HandleDrop() override;
	void BeginDragSelection(Vector2f mousePos) override;
	void EndDragSelection(Vector2f mousePos, bool isShiftDown) override;
	void DrawCollisionOverlay(CollisionOverlay& overlay) override;
	void ClickSelection(Vector2f mousePos, uint32_t selectedId, bool isShiftDown) override;

	void BeginTransformation() override;
	void UpdateTransformation(const Vector3f& referencePosition, const Matrix4x4f& transformRelativeStart) override;
	void EndTransformation() override;

	Vector3f CalculateSelectionPosition() override;
	Matrix4x4f CalculateSelectionOrientation() override;

	virtual bool HasTransformableSelection() override;

	// Puts an instance of a TGO in front of the viewport camera and selects it.
	void PlaceObject(const std::string& definitionPath, const std::string& displayName);

private:
	void DrawAddMenu();

	EditorViewport myViewport;
	SceneObjectProperties myProperties;
	SceneObjectList mySceneObjectList;
	NavmeshCreationTool myNavmeshCreationTool;

	Scene* myScene;
	SceneSelection mySceneSelection;

	bool myIsDockingInitialized = false;

	std::string myPanelWindowNames[(size_t)Panels::Count];

	std::unordered_map<uint32_t, int> myObjectModificationsCounts;
	int mySceneModificationsCount = 0;

	TransformCommand myPendingTransformCommand;
	std::vector<Matrix4x4f> myTransformationInitialTransforms;

	std::unique_ptr<SceneEditorGraphicsBase> myGraphics;
};

}
