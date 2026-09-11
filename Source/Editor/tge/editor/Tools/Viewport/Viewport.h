#pragma once
#include <tge/math/Vector.h>
#include <tge/editor/Gizmos/Gizmos.h>
#include <tge/editor/Tools/ViewportGrid/ViewportGrid.h>
#include <tge/graphics/DepthBuffer.h>
#include <tge/graphics/RenderTarget.h>
#include <tge/graphics/Camera.h>

namespace P4
{
	struct FileInfo;
}

namespace Tga 
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
		//void SetPerforceInfo(const P4::FileInfo& someInfo);

		const RenderTarget& GetIdRenderTarget() const { return myIdTarget; }

		inline const Vector2i& GetViewportSize() const;
		inline const Vector2i& GetViewportPos() const;

		void BeginDraw();
		void SetupIdPass();
		void SetupColorPass();
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