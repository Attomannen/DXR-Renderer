#include <tge/editor/Gizmos/Gizmos.h>

#include <imgui.h>
#include <ImGuizmo.h>

#include <tge/graphics/Camera.h>

#include <tge/editor/CommandManager/CommandManager.h>
#include <tge/scene/Scene.h>

#include <tge/editor/Document/Document.h>

#include <tge/editor/p4/p4.h>

#include <filesystem>

#include <tge/editor/Commands/TransformCommand.h>

using namespace Tga;

void Gizmos::Draw()
{
	{ // World/local -space mode
		if (myCurrentOperation == ImGuizmo::TRANSLATE || myCurrentOperation == ImGuizmo::ROTATE)
		{
			if (ImGui::RadioButton("Local", myCurrentMode == ImGuizmo::LOCAL))
				myCurrentMode = ImGuizmo::LOCAL;
			ImGui::SameLine();
			if (ImGui::RadioButton("World", myCurrentMode == ImGuizmo::WORLD))
				myCurrentMode = ImGuizmo::WORLD;
		}
	}
	{ // Settings for snapping
		switch (myCurrentOperation)
		{
		case ImGuizmo::TRANSLATE:
			ImGui::Text("Translation Snap");
			ImGui::Checkbox("Enabled", &mySnap.snapPos);
			ImGui::DragFloat("Amount", &mySnap.pos);
			break;
		case ImGuizmo::ROTATE:
			ImGui::Text("Angle Snap");
			ImGui::Checkbox("Enabled", &mySnap.snapRot);
			ImGui::DragFloat("Amount", &mySnap.rot);
			break;
		case ImGuizmo::SCALE:
			ImGui::Text("Scale Snap");
			ImGui::Checkbox("Enabled", &mySnap.snapScale);
			ImGui::DragFloat("Amount", &mySnap.scale);
			break;
		}
	}
	

	{ // Keyboard shortcuts
		if (ImGui::IsAnyItemActive() == false && ImGui::GetIO().KeyCtrl == false && !ImGui::GetIO().MouseDown[ImGuiMouseButton_Right])
		{
            if (ImGui::IsKeyPressed(ImGuiKey_W)) 
			{
                myCurrentOperation = ImGuizmo::TRANSLATE;
            } 
			if (ImGui::IsKeyPressed(ImGuiKey_E)) 
			{
				myCurrentOperation = ImGuizmo::ROTATE;
            }
			if (ImGui::IsKeyPressed(ImGuiKey_R)) 
			{
                myCurrentOperation = ImGuizmo::SCALE;
            }
        }
	}
}

void Gizmos::DrawGizmos(const Camera& camera, ViewportInterface& aViewportInterface, Vector2i aViewportPos, Vector2i aViewportSize)
{
	bool hasSelection = aViewportInterface.HasTransformableSelection();

	ImGuizmo::SetID(ImGui::GetID(0));

    ImGui::SetItemDefaultFocus();	

    auto io = ImGui::GetIO();
    {
		// Imguizmo doesn't seem to work well with positions far from the origin
		// So we send it positions relative to the origin of the selection when the transformation starts (snapped to grid if needed)
		if (!ImGuizmo::IsUsing() && hasSelection)
		{
			Vector3f pos = aViewportInterface.CalculateSelectionPosition();

			if (mySnap.snapPos)
			{
				pos = pos / mySnap.pos;
				pos.x = round(pos.x);
				pos.y = round(pos.y);
				pos.z = round(pos.z);

				pos = mySnap.pos * pos;
			}

			myManipulationStartPos = pos;
		}

		Matrix4x4f cameraToWorld = camera.GetTransform();
		cameraToWorld.SetPosition(cameraToWorld.GetPosition() - myManipulationStartPos);
		Matrix4x4f view = Matrix4x4f::GetFastInverse(cameraToWorld);
        Matrix4x4f proj = camera.GetProjection();

		float left = (float)aViewportPos.x;
		float top = (float)aViewportPos.y;
		float width = (float)aViewportSize.x;
		float height = (float)aViewportSize.y;

		ImGuizmo::SetRect(left, top, width, height);

        ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

		if (!aViewportInterface.HasTransformableSelection())
			return; 

        Matrix4x4f transformBefore = myManipulationCurrentTransform;
		Matrix4x4f transformAfter;

		float snap[3];
		switch (myCurrentOperation)
		{
			case ImGuizmo::TRANSLATE: snap[0] = snap[1] = snap[2] = (!mySnap.snapPos != !io.KeyCtrl) ? mySnap.pos : 0.f;	break;
			case ImGuizmo::SCALE: snap[0] = snap[1] = snap[2] = (!mySnap.snapScale != !io.KeyCtrl) ? mySnap.scale : 0.f;	break;
			case ImGuizmo::ROTATE: snap[0] = snap[1] = snap[2] = (!mySnap.snapRot != !io.KeyCtrl) ? mySnap.rot : 0.f;	break;
		}

		
		if (myCurrentMode == ImGuizmo::LOCAL && !ImGuizmo::IsUsing())
		{
			transformBefore = aViewportInterface.CalculateSelectionOrientation();
		}

		transformBefore.SetPosition(aViewportInterface.CalculateSelectionPosition() - myManipulationStartPos);

		transformAfter = transformBefore;

		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

		ImGuizmo::Manipulate(
			view.GetDataPtr(),
			proj.GetDataPtr(),
			(ImGuizmo::OPERATION)myCurrentOperation,
			(ImGuizmo::MODE)myCurrentMode,
			transformAfter.GetDataPtr(),
			nullptr,
			snap
		);
		
		myManipulationCurrentTransform = transformAfter;

		if (!io.KeyAlt)
		{
			if (ImGuizmo::IsOver() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				aViewportInterface.BeginTransformation();
				myManipulationInitialTransform = transformBefore;
				myIsManipulating = true;
			} 

			if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && myIsManipulating)
			{
				if (!ImGui::IsItemHovered())
				{
					int a = 2;
					a = 3;
				}
				aViewportInterface.UpdateTransformation(myManipulationStartPos, myManipulationInitialTransform.GetInverse() * transformAfter);
			}

			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
			{
				Vector3f pos, scale;
				Quaternionf rot;
				transformAfter.DecomposeMatrix(pos, rot, scale);
				Vector3f euler = rot.GetYawPitchRoll();

				aViewportInterface.EndTransformation();
				myIsManipulating = false;
			}
		}
    }

	
}
