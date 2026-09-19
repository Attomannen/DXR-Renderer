#pragma once
#include <age/math/Vector.h>
#include <age/editor/Gizmos/Gizmos.h>
#include <age/editor/Tools/ViewportGrid/ViewportGrid.h>
#include <age/editor/Tools/Viewport/CollisionOverlay.h>
#include <age/graphics/DepthBuffer.h>
#include <age/graphics/RenderTarget.h>
#include <age/graphics/Camera.h>
#include <age/graphics/FlyCameraTuning.h>

namespace Ag
{
	class ModelShader;
	class Scene;
	class RenderTarget;
	class ViewportInterface;

	class EditorViewport 
	{
	public:
		void Init();

		void DrawAndUpdateViewportWindow(float aDeltaTime, ViewportInterface& aViewportInterface);

		void Resize(const Vector2i& aSize = { 0,0 });
		bool GetViewportNeedsResize() const;
		void SetNeedsResize(bool);

		const RenderTarget& GetIdRenderTarget() const { return myIdTarget; }
		// Exposed so a pluggable EditorGraphics backend (DefaultEditorGraphics,
		// specifically DefaultSceneEditorGraphics) can point DeferredRenderer's
		// hardcoded DX11::BackBuffer/DepthBuffer globals at THIS viewport's own
		// targets for the duration of one deferred-rendered frame, then restore
		// them -- see DeferredRenderer's Composite()/BeginGeometryPass(), which
		// assume those globals rather than taking an explicit render target.
		// EditorViewport itself stays graphics-backend-agnostic; it just owns
		// the resources being pointed at.
		RenderTarget& GetRenderTarget() { return myRenderTarget; }
		DepthBuffer& GetColorDepthBuffer() { return myDepth; }

		// Whether the 3D image was hovered last frame. A play session uses this
		// instead of ImGui's global WantCaptureMouse, which is always true here
		// -- the viewport IS an ImGui window.
		bool IsViewportHovered() const { return myViewportHovered; }

		inline const Vector2i& GetViewportSize() const;
		inline const Vector2i& GetViewportPos() const;

		void BeginDraw();
		void SetupIdPass();
		void SetupColorPass(bool aDrawGrid = true);
		// Grid over an already-rendered colour target, depth-tested against it.
		void DrawGrid();
		void EndDraw();

		void SetColorAsTarget(bool useDepth);

		Gizmos& GetGizmos() { return myGizmos;  };
		ViewportGrid& GetGrid() { return myViewportGrid; }

		Camera& GetCamera() { return myCamera; }
		const Camera& GetCamera() const { return myCamera; }
		void SetCameraFocusDistance(float cameraFocusDistance) { myCameraFocusDistance = cameraFocusDistance; }
		const float& GetCameraFocusDistance() const { return myCameraFocusDistance; }
		void SetCameraRotation(Vector3f cameraRotation) { myCameraRotation = cameraRotation; }
		const Vector3f& GetCameraRotation() const { return myCameraRotation; }

	private:
		ViewportGrid myViewportGrid;
		CollisionOverlay myCollisionOverlay;

		RenderTarget myRenderTarget;
		RenderTarget myIdTarget;
		DepthBuffer myDepth;

		Vector2f myPreviousMousePos;
		Vector2i myViewportPos;
		Vector2i myViewportSize;
		// Starts true so the render target is sized to the real panel on the
		// first frame. Uninitialised, it was whatever the stack held.
		bool myNeedsResize = true;
		bool myViewportHovered = false;
		// A camera drag (free-fly, Maya orbit, middle-button pan) is in
		// progress. While it is, the cursor is confined to the panel and the
		// camera keeps reading input even though the mouse may no longer be
		// hovering -- releasing the button ends it, not leaving the rectangle.
		bool myCameraDragActive = false;

		// Metres per second, shared with the game's flight camera so a level
		// handles the same before and after pressing Play -- see
		// age/graphics/FlyCameraTuning.h.
		float myFreeFlyMovementSpeed = Ag::FlyCamera::kDefaultSpeed;
		Vector3f myFreeFlyVelocity{ 0, 0, 0 };
		Gizmos myGizmos;

		Camera myCamera;
		float myCameraFocusDistance = 10.f;   // metres
		Vector3f myCameraRotation = { 0.f, 0.f, 0.f };
	};

	const Vector2i& EditorViewport::GetViewportSize() const
	{
		return myViewportSize;
	}

	const Vector2i& EditorViewport::GetViewportPos() const
	{
		return myViewportPos;
	}

}