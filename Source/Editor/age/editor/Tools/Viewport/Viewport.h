#pragma once
#include <age/math/Vector.h>
#include <age/editor/Gizmos/Gizmos.h>
#include <age/editor/Tools/ViewportGrid/ViewportGrid.h>
#include <age/editor/Tools/Viewport/CollisionOverlay.h>
#include <age/graphics/DepthBuffer.h>
#include <age/graphics/RenderTarget.h>
#include <age/graphics/Camera.h>

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
		bool myNeedsResize;

		float myFreeFlyMovementSpeed = 1.f;
		Gizmos myGizmos;

		Camera myCamera;
		float myCameraFocusDistance = 1000.f;
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