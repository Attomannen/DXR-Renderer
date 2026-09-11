#pragma once

#include <tge/editor/Document/Document.h>
#include <tge/editor/Tools/Viewport/Viewport.h>
#include <tge/editor/EditorGraphics/EditorGraphicsBase.h>
#include <tge/editor/Material/MaterialAsset.h>

namespace Tga
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
		PreviewSettings,
		Count
	};

	void Init(std::string_view path) override;
	void Update(float aTimeDelta, InputManager& inputManager) override;
	void Save() override;
	void OnAction(CommandManager::Action action) override;

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
	void DrawProperties();

	MaterialAsset myMaterial;
	std::string myName;

	EditorViewport myViewport;
	std::unique_ptr<MaterialEditorGraphicsBase> myGraphics;

	bool myIsDockingInitialized = false;
	std::string myPanelWindowNames[(size_t)Panels::Count];
};

}
