#pragma once

#include <imgui.h>

#include <age/editor/Document/Document.h>
#include <age/editor/Tools/Viewport/Viewport.h>
#include <age/animation/AnimationClip.h>

#include <age/editor/EditorGraphics/EditorGraphicsBase.h>

namespace Ag
{
	class AnimationClipDocument : public Document, public ViewportInterface
	{
	public:
		enum class Panels
		{
			Skeleton,
			Properties,
			PlayControls,
			Viewport,
			Count,
		};

		enum class PlayState
		{
			Stopped,
			Playing
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

		bool HasTransformableSelection() override;
	private:
		void DrawSkeletonPanel();
		void DrawPropertyPanel();
		void DrawPlayControls();

		EditorViewport myViewport;
		std::unique_ptr<AnimationClipEditorGraphicsBase> myGraphics;

		StringId myPath;
		StringId myName;

		AnimationClip* myAnimationClip;

		bool myIsDockingInitialized = false;

		std::string myPanelWindowNames[(size_t)Panels::Count];

		int mySelectedSkeletonNodeIndex = -1;

		PlayState myPlayState = PlayState::Stopped;
		float myCurrentTime = 0.f;
	};

}