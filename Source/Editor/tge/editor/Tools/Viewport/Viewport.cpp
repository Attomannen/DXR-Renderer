#include <tge/editor/Tools/Viewport/Viewport.h>

#include <imgui.h>
#include <ImGuizmo.h>

#include <tge/editor/Editor.h>
#include <tge/scene/ScenePropertyTypes.h>
#include <tge/settings/settings.h>
#include <tge/log/Log.h>

#include <tge/Editor/CommandManager/CommandManager.h>
#include <tge/graphics/Camera.h>
#include <tge/graphics/DX11.h>
#include <tge/Graphics/RenderTarget.h>
#include <tge/scene/Scene.h>
#include <tge/scene/SceneSerialize.h>

#include <tge/editor/Commands/AddSceneObjectsCommand.h>

#include <tge/editor/imgui_widgets/imgui_widgets.h>
#include <tge/editor/Scene/SceneSelection.h>
#include <tge/editor/Scene/ActiveScene.h>
#include <tge/editor/Document/Document.h>

#include <tge/editor/p4/p4.h>

#include "tge/Application.h"

using namespace Tga;

struct IDPixelValues {
	uint32_t id;
	uint32_t selectionID;
	uint32_t p4info;
};

static IDPixelValues MouseOver(Tga::Vector2ui, const RenderTarget &);

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

void EditorViewport::SetupColorPass()
{
	myDepth.Clear(1.0f, 0);
	myRenderTarget.SetAsActiveTarget(&myDepth);
	myRenderTarget.Clear();

	if (Editor::GetEditor()->IsViewportGridVisible())
	{
		myViewportGrid.DrawViewportGrid();
	}
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
	Tga::Vector2ui resolution = { (unsigned int)aSize.x, (unsigned int)aSize.y };

	if (resolution.x == 0 || resolution.y == 0)
	{
		resolution = Tga::Application::GetInstance()->GetRenderSize();
	}

	Tga::Vector2f center = { (float)resolution.x * 0.5f, (float)resolution.y * 0.5f };

	myRenderTarget = RenderTarget::Create(resolution, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM);
	myIdTarget = RenderTarget::Create(resolution, DXGI_FORMAT_R32G32B32A32_UINT);
	myDepth = DepthBuffer::Create(resolution);

	myCamera.SetPerspectiveProjection(
		90,
		{
			(float)resolution.x,
			(float)resolution.y
		},
		0.1f,
		50000.0f
	);
	//camera.GetTransform().SetPosition(Vector3f(0.0f, 0.0f, -550.0f));
}

void EditorViewport::DrawAndUpdateViewportWindow(float aDeltaTime, ViewportInterface& aViewportInterface)
{
	{
		ImGui::Image((ImTextureID)myRenderTarget.GetShaderResourceView(), ImGui::GetContentRegionAvail());
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

		/////////////////////////
		// Only check viewport input if mouse is over
		if (ImGui::IsItemHovered() && ImGuizmo::IsUsingAny() == false)
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
				Tga::Camera& activeCamera = myCamera;

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
					aViewportInterface.EndDragSelection({ ImGui::GetMousePos().x, ImGui::GetMousePos().y}, io.KeyShift);
				}
				if (ImGui::IsMousePosValid(&mousePos))
				{
					Vector2ui mapped_mouse = { (uint32_t)(mousePos.x), (uint32_t)(mousePos.y) };
					IDPixelValues pixel = MouseOver(mapped_mouse, myIdTarget);

					if (pixel.p4info != 0)
					{
						const char* path  = GetActiveScene()->GetObjectFilePath(pixel.id).GetString();
						auto& info = P4::GetFileInfo(path);

						ImGui::BeginTooltip();
						ImGui::Text("%s\nopen for %s\nby: %s\nin changelist: %s\nworkspace:%s", path, P4::FileActionString(info.action).data(), info.user, info.changelist, info.client);
						ImGui::EndTooltip();
					}

					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						aViewportInterface.ClickSelection({ mousePos.x, mousePos.y }, pixel.id, io.KeyShift);
					}
				}

				////////////////////////////////////
				// Free fly camera
				if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
				{
					Tga::Camera& activeCamera = myCamera;

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
					activeCamera.GetTransform().SetPosition(activeCamera.GetTransform().GetPosition() + camMovement * (1000.f*myFreeFlyMovementSpeed) * aDeltaTime);

				}
				
				////////////////////////////////////
				// Camera controls - Blender like
				else
				{
					if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
					{
						if (io.KeyShift && io.KeyCtrl)
						{
							TranslateCameraInPlane(myCamera, mouseDelta, myCameraFocusDistance);
						}
						else if (io.KeyShift)
						{
							TranslateCamera(myCamera, mouseDelta, myCameraFocusDistance);
						}
						else if (io.KeyCtrl)
						{
							ZoomCamera(myCamera, mouseDelta, myCameraFocusDistance);
						}
						else
						{
							RotateCamera(myCamera, mouseDelta, myCameraFocusDistance, myCameraRotation);
						}
					}

					if (ImGui::IsKeyPressed(ImGuiKey_MouseWheelY)) {
						ZoomCamera(myCamera, { 0, io.MouseWheel * 100.f }, myCameraFocusDistance);
					}
				}
			}

			myPreviousMousePos = { mousePos.x, mousePos.y };
		}
	}
}


static IDPixelValues MouseOver(Tga::Vector2ui aPos, const RenderTarget &aTarget)
{
	if (aPos.x < 0 || aPos.x >= (int)DX11::GetResolution().X  || aPos.y < 0 || aPos.y >= (int)DX11::GetResolution().Y)
		return {};

	ID3D11Resource* src;
	const auto& context = DX11::Context;
	aTarget.GetShaderResourceView()->GetResource(&src);

	D3D11_TEXTURE2D_DESC textureDesc;
	textureDesc.Width = 1;
	textureDesc.Height = 1;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = (DXGI_FORMAT_R32G32B32A32_UINT);
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	textureDesc.Usage = D3D11_USAGE_STAGING;
	textureDesc.BindFlags = 0;
	textureDesc.MiscFlags = 0;

	ComPtr<ID3D11Texture2D> tmp;
	HRESULT hr = DX11::Device->CreateTexture2D(&textureDesc, nullptr, tmp.GetAddressOf());
	assert(SUCCEEDED(hr));

	D3D11_BOX srcBox;
	srcBox.left = aPos.x;
	srcBox.right = aPos.x + 1;
	srcBox.bottom = aPos.y + 1;
	srcBox.top = aPos.y;
	srcBox.front = 0;
	srcBox.back = 1;

	DX11::Context->CopySubresourceRegion(
		tmp.Get(),
		0, 0, 0, 0,
		src, 0,
		&srcBox
	);
	D3D11_MAPPED_SUBRESOURCE msr = {};
	hr = context->Map(tmp.Get(), 0, D3D11_MAP::D3D11_MAP_READ, 0, &msr);
	assert(SUCCEEDED(hr));

	uint32_t* data = reinterpret_cast<uint32_t*>(msr.pData);

	context->Unmap(tmp.Get(), 0);
	IDPixelValues val{};

	if (data != nullptr)
	{
		val.id = (uint32_t)data[0];
		val.selectionID = (uint32_t)data[1];
		val.p4info = (uint32_t)data[2];
	}

	return val;
}
