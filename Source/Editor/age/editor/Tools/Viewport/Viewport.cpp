#include <age/editor/Tools/Viewport/Viewport.h>

#include <imgui.h>
#include <ImGuizmo.h>

#include <age/editor/Editor.h>
#include <age/scene/ScenePropertyTypes.h>
#include <age/settings/settings.h>
#include <age/log/Log.h>

#include <age/Editor/CommandManager/CommandManager.h>
#include <age/graphics/Camera.h>
#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>
#include <age/Graphics/RenderTarget.h>
#include <age/scene/Scene.h>
#include <age/scene/SceneSerialize.h>

#include <age/editor/Commands/AddSceneObjectsCommand.h>

#include <age/editor/imgui_widgets/imgui_widgets.h>
#include <age/editor/Scene/SceneSelection.h>
#include <age/editor/Scene/ActiveScene.h>
#include <age/editor/Document/Document.h>
#include <age/Application.h>


#include "age/Application.h"

using namespace Ag;

struct IDPixelValues {
	uint32_t id;
	uint32_t selectionID;
	uint32_t p4info;
};

static IDPixelValues MouseOver(Ag::Vector2ui, const RenderTarget &);

static float mayaRotSpeed = 0.5f;
static float mayaZoomSpeed = 0.01f;
static float mayaPanSpeed = 0.005f;

bool EditorViewport::GetViewportNeedsResize() const
{
	return myNeedsResize;
}

void EditorViewport::SetNeedsResize(bool value)
{
	myNeedsResize = value;
}

static void TranslateCameraInPlane(Camera& aCamera, const Vector2f& aMouseDelta, float aFocusDistance)
{
	Vector3f camMovement = {};

	Vector3f upDir = aCamera.GetTransform().GetUp();
	Vector3f rightDir = aCamera.GetTransform().GetRight();

	Vector3f forwardDir = aCamera.GetTransform().GetForward();

	if (abs(forwardDir.x) > abs(forwardDir.y) && abs(forwardDir.x) > abs(forwardDir.z))
	{
		upDir.x = 0.f;
		rightDir.x = 0.f;
	} 
	else if (abs(forwardDir.y) > abs(forwardDir.z))
	{
		upDir.y = 0.f;
		rightDir.y = 0.f;
	}
	else
	{
		upDir.z = 0.f;
		rightDir.z = 0.f;
	}

	camMovement += upDir * (float)aMouseDelta.Y;
	camMovement -= rightDir * (float)aMouseDelta.X;
	aCamera.GetTransform().SetPosition(aCamera.GetTransform().GetPosition() + camMovement * aFocusDistance * mayaPanSpeed);
}

static void TranslateCamera(Camera& aCamera, const Vector2f &aMouseDelta, float aFocusDistance)
{
	Vector3f camMovement = {};
	camMovement += aCamera.GetTransform().GetUp() * (float)aMouseDelta.Y;
	camMovement -= aCamera.GetTransform().GetRight() * (float)aMouseDelta.X;
	aCamera.GetTransform().SetPosition(aCamera.GetTransform().GetPosition() + camMovement * aFocusDistance * mayaPanSpeed);
}

static void RotateCamera(Camera& aCamera, const Vector2f& aMouseDelta, float aFocusDistance, Vector3f& cameraRotation)
{
	Vector3f camPos = aCamera.GetTransform().GetPosition();
	Vector3f targetPoint = camPos + aCamera.GetTransform().GetForward() * aFocusDistance;

	cameraRotation.X += mayaRotSpeed*(float)aMouseDelta.Y;
	cameraRotation.Y += mayaRotSpeed*(float)aMouseDelta.X;

	aCamera.GetTransform().SetRotation(cameraRotation);

	aCamera.GetTransform().SetPosition((targetPoint)+(aCamera.GetTransform().GetForward() * -aFocusDistance));
}

static void ZoomCamera(Camera& aCamera, const Vector2f& aMouseDelta, float& aFocusDistance)
{
	Vector3f camPos = aCamera.GetTransform().GetPosition();
	Vector3f targetPoint = camPos + aCamera.GetTransform().GetForward() * aFocusDistance;
	aFocusDistance = (aFocusDistance * powf(2.f, -aMouseDelta.Y * mayaZoomSpeed));
	aCamera.GetTransform().SetPosition((targetPoint)+(aCamera.GetTransform().GetForward() * -aFocusDistance));
}

void EditorViewport::Init()
{
	Resize();

			}

void EditorViewport::BeginDraw()
{
	if (GetViewportNeedsResize())
	{
		Resize(GetViewportSize());
		SetNeedsResize(false);
	}
}

void EditorViewport::SetupIdPass()
{
	myIdTarget.Clear();
	myDepth.Clear(1.0f, 0);
	myIdTarget.SetAsActiveTarget(&myDepth);
}

void EditorViewport::SetupColorPass(bool aDrawGrid)
{
	myDepth.Clear(1.0f, 0);
	myRenderTarget.SetAsActiveTarget(&myDepth);
	myRenderTarget.Clear();

	if (aDrawGrid)
		DrawGrid();
}

void EditorViewport::DrawGrid()
{
	if (!Editor::GetEditor()->IsViewportGridVisible()) return;
	myRenderTarget.SetAsActiveTarget(&myDepth);
	myViewportGrid.DrawViewportGrid(myCamera.GetTransform().GetForward());
}

void EditorViewport::SetColorAsTarget(bool useDepth)
{
	myRenderTarget.SetAsActiveTarget(useDepth ? &myDepth : nullptr);
}

void EditorViewport::EndDraw()
{
	DX11::BackBuffer->SetAsActiveTarget();
}

void EditorViewport::Resize(const Vector2i& aSize)
{
	// render target setup, need somehting for resize along these lines
	Ag::Vector2ui resolution = { (unsigned int)aSize.x, (unsigned int)aSize.y };

	if (resolution.x == 0 || resolution.y == 0)
	{
		resolution = Ag::Application::GetInstance()->GetRenderSize();
	}

	Ag::Vector2f center = { (float)resolution.x * 0.5f, (float)resolution.y * 0.5f };

	myRenderTarget = RenderTarget::Create(resolution, rhi::Format::R8G8B8A8_Typeless, rhi::Format::R8G8B8A8_UNorm_sRGB, rhi::Format::R8G8B8A8_UNorm);
	myIdTarget = RenderTarget::Create(resolution, rhi::Format::R32G32B32A32_UInt);
	myDepth = DepthBuffer::Create(resolution);

	myCamera.SetPerspectiveProjection(
		90,
		{
			(float)resolution.x,
			(float)resolution.y
		},
		0.01f,      // 1 cm near plane
		5000.0f     // 5 km far plane
	);
	//camera.GetTransform().SetPosition(Vector3f(0.0f, 0.0f, -550.0f));
}

void EditorViewport::DrawAndUpdateViewportWindow(float aDeltaTime, ViewportInterface& aViewportInterface)
{
	{
		// NOT GetShaderResourceView(): that's the DX11-only raw ID3D11ShaderResourceView*
		// (never populated on DX12 -- only the RHI SrvHandle is). Goes through
		// IDevice::ImGuiTextureId, the backend-agnostic bridge built for exactly
		// this (see its DX12 implementation for why a plain SRV pointer/handle
		// isn't enough there -- it needs a descriptor resident in ImGui's own
		// shader-visible heap). Found 2026-09-12: this was the one remaining
		// raw-DX11 call standing between DX12 GameEditor and actually showing
		// the 3D viewport at all.
		ImGui::Image((ImTextureID)DX11::Rhi()->ImGuiTextureId(myRenderTarget.GetSrv()), ImGui::GetContentRegionAvail());
		myViewportHovered = ImGui::IsItemHovered();
		ImVec2 viewportSize = ImGui::GetItemRectSize();
		ImVec2 viewportPos = ImGui::GetItemRectMin();

		if (viewportSize.x > 0 && viewportSize.y > 0 && (myViewportSize.x != (int32_t)viewportSize.x || myViewportSize.y != (int32_t)viewportSize.y))
		{
			myNeedsResize = true;
			myViewportSize = { (int32_t)viewportSize.x, (int32_t)viewportSize.y};
		}
		
		myViewportPos = { (int32_t)viewportPos.x, (int32_t)viewportPos.y };

		//////////////////////////
		// Drag and drop target

		aViewportInterface.HandleDrop();

		myGizmos.DrawGizmos(myCamera, aViewportInterface, GetViewportPos(), GetViewportSize());

		if (Editor::GetEditor()->IsCollisionVisible())
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->PushClipRect(viewportPos, ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y), true);
			myCollisionOverlay.Begin(drawList, myCamera, { viewportPos.x, viewportPos.y }, { viewportSize.x, viewportSize.y });
			aViewportInterface.DrawCollisionOverlay(myCollisionOverlay);
			drawList->PopClipRect();
		}

		/////////////////////////
		// Confine the cursor to the panel for the duration of a camera drag,
		// so looking around does not run the pointer out over the rest of the
		// editor -- and so the drag survives reaching the edge, which the
		// hover test below would otherwise end mid-motion.
		{
			const bool cameraButtonDown = ImGui::IsMouseDown(ImGuiMouseButton_Right)
				|| ImGui::IsMouseDown(ImGuiMouseButton_Middle)
				|| (ImGui::GetIO().KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left));

			if (!myCameraDragActive && cameraButtonDown && myViewportHovered && !ImGuizmo::IsUsingAny())
			{
				if (HWND* window = Application::GetInstance() ? Application::GetInstance()->GetHWND() : nullptr)
				{
					// ImGui coordinates are the main viewport's, which is this
					// window's client area; ClipCursor wants screen space.
					POINT upperLeft{ myViewportPos.x, myViewportPos.y };
					POINT lowerRight{ myViewportPos.x + myViewportSize.x, myViewportPos.y + myViewportSize.y };
					MapWindowPoints(*window, nullptr, &upperLeft, 1);
					MapWindowPoints(*window, nullptr, &lowerRight, 1);
					RECT clip{ upperLeft.x, upperLeft.y, lowerRight.x, lowerRight.y };
					if (clip.right > clip.left && clip.bottom > clip.top)
					{
						ClipCursor(&clip);
						myCameraDragActive = true;
					}
				}
			}
			else if (myCameraDragActive && !cameraButtonDown)
			{
				ClipCursor(nullptr);
				myCameraDragActive = false;
			}
		}

		/////////////////////////
		// Only check viewport input if mouse is over -- or if a camera drag
		// that started over it is still held.
		if ((ImGui::IsItemHovered() || myCameraDragActive) && ImGuizmo::IsUsingAny() == false)
		{
			ImGuiIO& io = ImGui::GetIO(); (void)io;

			ImVec2 mousePosScreen = ImGui::GetMousePos(); //
			ImVec2 mousePos(mousePosScreen.x - viewportPos.x, mousePosScreen.y - viewportPos.y);

			float freeFlyRotSpeed = 0.5f;

			Vector2f mouseDelta = Vector2f{ mousePos.x, mousePos.y } - myPreviousMousePos;

			///////////////////////////////////
			// Camera controls - Maya like
			if (io.KeyAlt)
			{
				Ag::Camera& activeCamera = myCamera;

				// pan
				if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				{
					TranslateCamera(activeCamera, mouseDelta, myCameraFocusDistance);
				}
				// rotate
				else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					RotateCamera(activeCamera, mouseDelta, myCameraFocusDistance, myCameraRotation);
				}
				// zoom
				else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
				{
					ZoomCamera(activeCamera, mouseDelta, myCameraFocusDistance);
				}
			}
			else
			{
				
				if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					aViewportInterface.BeginDragSelection({ ImGui::GetMousePos().x, ImGui::GetMousePos().y});
				}
				if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
				{
					aViewportInterface.EndDragSelection({ ImGui::GetMousePos().x, ImGui::GetMousePos().y}, io.KeyShift || io.KeyCtrl);
				}
				if (ImGui::IsMousePosValid(&mousePos))
				{
					Vector2ui mapped_mouse = { (uint32_t)(mousePos.x), (uint32_t)(mousePos.y) };
					IDPixelValues pixel = MouseOver(mapped_mouse, myIdTarget);

					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						aViewportInterface.ClickSelection({ mousePos.x, mousePos.y }, pixel.id, io.KeyShift || io.KeyCtrl);
					}
				}

				////////////////////////////////////
				// Free fly camera
				if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
				{
					Ag::Camera& activeCamera = myCamera;

					Vector3f cameraRotation = myCameraRotation;

					cameraRotation.X += freeFlyRotSpeed * (float)mouseDelta.Y;
					cameraRotation.Y += freeFlyRotSpeed * (float)mouseDelta.X;

					activeCamera.GetTransform().SetRotation(cameraRotation);
					myCameraRotation = cameraRotation;

					Vector3f camMovement = {};
					if (ImGui::IsKeyDown(ImGuiKey_W))
					{
						camMovement += activeCamera.GetTransform().GetForward() * 1.0f;
					}
					if (ImGui::IsKeyDown(ImGuiKey_S))
					{
						camMovement += activeCamera.GetTransform().GetForward() * -1.0f;
					}
					if (ImGui::IsKeyDown(ImGuiKey_A))
					{
						camMovement += activeCamera.GetTransform().GetRight() * -1.0f;
					}
					if (ImGui::IsKeyDown(ImGuiKey_D))
					{
						camMovement += activeCamera.GetTransform().GetRight() * 1.0f;
					}
					if (ImGui::IsKeyDown(ImGuiKey_Q))
					{
						camMovement += activeCamera.GetTransform().GetUp() * -1.0f;
					}
					if (ImGui::IsKeyDown(ImGuiKey_E))
					{
						camMovement += activeCamera.GetTransform().GetUp() * 1.0f;
					}
					if (ImGui::IsKeyPressed(ImGuiKey_MouseWheelY))
					{
						float wheel = io.MouseWheel > 0.f ? 0.1f : -0.1f;
						myFreeFlyMovementSpeed = std::clamp(myFreeFlyMovementSpeed + wheel, .1f, 10.f);
					}
					activeCamera.GetTransform().SetPosition(activeCamera.GetTransform().GetPosition() + camMovement * (10.f * myFreeFlyMovementSpeed * (io.KeyShift ? 3.f : 1.f)) * aDeltaTime);

				}
				
				////////////////////////////////////
				// Camera controls - Blender like
				else
				{
					// Middle mouse pans like Unreal; Shift pans in the ground plane.
					if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
					{
						if (io.KeyShift)
							TranslateCameraInPlane(myCamera, mouseDelta, myCameraFocusDistance);
						else
							TranslateCamera(myCamera, mouseDelta, myCameraFocusDistance);
					}

					if (ImGui::IsKeyPressed(ImGuiKey_MouseWheelY)) {
						ZoomCamera(myCamera, { 0, io.MouseWheel * 1.f }, myCameraFocusDistance);
					}
				}
			}

			myPreviousMousePos = { mousePos.x, mousePos.y };
		}
	}
}


static IDPixelValues MouseOver(Ag::Vector2ui aPos, const RenderTarget &aTarget)
{
	if (aPos.x < 0 || aPos.x >= (int)DX11::GetResolution().X  || aPos.y < 0 || aPos.y >= (int)DX11::GetResolution().Y)
		return {};

	// NOT raw D3D11 (GetShaderResourceView()->GetResource(...) etc): that
	// pointer is never populated on DX12 -- dereferencing it there was a
	// 100%-reproducible access violation the instant the mouse moved over
	// the viewport (found 2026-09-12, the actual cause of GameEditor
	// "vanishing" under DX12 with no error/dump at all). Goes through
	// IDevice::ReadBackUintPixel4, which moved this exact logic (unchanged
	// on DX11) into the RHI and added a DX12 implementation.
	uint32_t data[4] = {};
	if (!DX11::Rhi()->ReadBackUintPixel4(aTarget.GetTextureHandle(), (uint32_t)aPos.x, (uint32_t)aPos.y, data))
		return {};

	IDPixelValues val{};
	val.id = data[0];
	val.selectionID = data[1];
	val.p4info = data[2];
	return val;
}
