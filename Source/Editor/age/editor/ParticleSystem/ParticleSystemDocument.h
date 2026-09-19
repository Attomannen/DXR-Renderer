#pragma once

#include <age/editor/Document/Document.h>
#include <age/editor/Tools/Viewport/Viewport.h>
#include <age/editor/EditorGraphics/EditorGraphicsBase.h>
#include <age/particles/ParticleSystem.h>

#include <string>

namespace Ag
{

// Niagara-style particle system editor: the System panel lists the emitters and their module stacks, the Details
// panel edits whatever is selected there, and the viewport plays the effect live.
class ParticleSystemDocument : public Document, public ViewportInterface
{
public:
	enum class Panels
	{
		Viewport,
		System,
		Details,
		Count
	};

	ParticleSystemDocument();

	void Init(std::string_view path) override;
	void Update(float aTimeDelta, InputManager& inputManager) override;
	void Save() override;
	void OnAction(CommandManager::Action action) override;

	// For ChangeParticleSystemCommand's Execute() and Undo().
	void SetAsset(const Particles::SystemAsset& anAsset);

	// ViewportInterface (nothing to select or transform in the preview).
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
	enum class SelectionKind { System, Emitter, Module, Renderer };
	struct Selection
	{
		SelectionKind kind = SelectionKind::System;
		int emitter = 0;
		Particles::ModuleStage stage = Particles::ModuleStage::ParticleUpdate;
		int module = 0;
	};

	// Structural edits made while the lists are being drawn, applied afterwards so nothing is invalidated mid-loop.
	struct PendingOp
	{
		enum class Kind { None, DeleteEmitter, DuplicateEmitter, MoveEmitter, DeleteModule, DuplicateModule, MoveModule };
		Kind kind = Kind::None;
		int emitter = 0;
		Particles::ModuleStage stage = Particles::ModuleStage::ParticleUpdate;
		int index = 0;
		int target = 0;
	};

	void DrawToolbar();
	void ApplyPendingOp();
	void DrawSystemPanel();
	void DrawEmitterNode(int anEmitterIndex);
	void DrawStack(int anEmitterIndex, Particles::ModuleStage aStage, const char* aTitle);
	void DrawUserParameters();
	void DrawAddModulePopup();
	void DrawDetailsPanel();
	void DrawModuleDetails(Particles::ModuleInstance& aModule);
	void DrawEmitterDetails(Particles::EmitterAsset& anEmitter);
	void DrawRendererDetails(Particles::RendererSettings& aRenderer);
	void ClampSelection();

	Particles::SystemAsset myAsset;
	Particles::SystemAsset myUndoSnapshot;
	bool myHasPendingEdit = false;
	bool myChangedThisFrame = false;

	Particles::SystemInstance myPreview;
	bool myPlaying = true;
	bool myLoopPreview = true;
	float myTimeScale = 1.f;

	Selection mySelection;
	PendingOp myPending;
	// The stack the "+" button of the module picker was pressed on.
	int myAddEmitter = 0;
	Particles::ModuleStage myAddStage = Particles::ModuleStage::ParticleUpdate;
	bool myOpenAddPopup = false;

	std::string myName;
	std::string myResolvedPath;

	EditorViewport myViewport;
	std::unique_ptr<ParticleEditorGraphicsBase> myGraphics;

	bool myIsDockingInitialized = false;
	std::string myPanelWindowNames[(size_t)Panels::Count];
};

}
