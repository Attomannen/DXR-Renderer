#pragma once

#include <memory>
#include <span>
#include <vector>
#include <tge/Math/Matrix4x4.h>
#include <tge/editor/CommandManager/CommandManager.h>
#include "tge/graphics/Camera.h"

namespace Tga
{
	class Camera;
	struct BoxSphereBounds;

	struct SelectionRect 
	{
		Vector2f start;
		Vector2f end;
		Frustum frustum;
		bool visible = false;
	};

	class RectSelection
	{
	public:
		static RectSelection* GetCurrentRectSelection();

		bool IsActive();
		void Update(const Vector2f& aMousePos, const Vector2f& aViewportPos, const Vector2f aViewportSize, const Tga::Camera& aCamera);
		bool CheckFrustum(const Tga::BoxSphereBounds& someBounds);
		void AddToSelection(uint32_t anInstance);
		void RemoveFromSelection(uint32_t anInstance);
		std::vector<uint32_t> GetSelection() const;
		void ClearSelection();
		bool Contains(uint32_t anInstance) const;

	private:
		SelectionRect mySelectRect;
		std::vector<uint32_t> mySelection;
	};

	class SceneSelection 
	{
	public:
		static SceneSelection* GetActiveSceneSelection();
		static void SetActiveSceneSelection(SceneSelection*);

		std::span<const uint32_t> GetSelection() const;

		void SetSelection(const std::span<uint32_t>& aNewSelection);
		void AddToSelection(uint32_t anInstance);

		void RemoveFromSelection(uint32_t anInstance);
		void ClearSelection();

		bool Contains(uint32_t anInstance) const;
		void ToggleSelect(uint32_t anInstance);

		void OnAction(CommandManager::Action action);

	private:
		const std::vector<uint32_t>* GetSelectionInternal() const;
		std::vector<uint32_t>& GetOrCreateSelection();
		struct SelectionUndoState
		{
			std::vector<uint32_t> selection;
		};

		// We store a selection before and after each command in the command manager
		// This way we can always set the selection to what it was before/after each command without complicating the API for anyone using the selection

		std::vector<SelectionUndoState> myUndoStack;
		std::vector<SelectionUndoState> myRedoStack;
	};
}