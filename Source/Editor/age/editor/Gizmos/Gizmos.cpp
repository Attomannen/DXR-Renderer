#include <age/editor/Gizmos/Gizmos.h>

#include <imgui.h>
#include <ImGuizmo.h>

#include <age/graphics/Camera.h>

#include <age/editor/CommandManager/CommandManager.h>
#include <age/scene/Scene.h>

#include <age/editor/Document/Document.h>

#include <filesystem>

#include <age/editor/Commands/TransformCommand.h>
#include <age/editor/EditorSettings.h>

using namespace Ag;

Gizmos::Gizmos()
{
	const EditorSettings& s = EditorSettings::Get();
	mySnap.snapPos = s.snapPosEnabled;
	mySnap.snapRot = s.snapRotEnabled;
	mySnap.snapScale = s.snapScaleEnabled;
	mySnap.pos = s.snapPosAmount;
	mySnap.rot = s.snapRotAmount;
	mySnap.scale = s.snapScaleAmount;
}

namespace
{
	// Called right after each snap widget below; writes the just-edited
	// value back into EditorSettings and saves, so any viewport's snap
	// settings become the default for every future one, per-document Gizmos
	// instances included -- see Gizmos::Gizmos() above.
	void SaveSnapSetting(const Gizmos::Snap& aSnap)
	{
		EditorSettings& s = EditorSettings::Get();
		s.snapPosEnabled = aSnap.snapPos;
		s.snapRotEnabled = aSnap.snapRot;
		s.snapScaleEnabled = aSnap.snapScale;
		s.snapPosAmount = aSnap.pos;
		s.snapRotAmount = aSnap.rot;
		s.snapScaleAmount = aSnap.scale;
		EditorSettings::Save();
	}
}

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
		// Save on deactivation, not on every value-changed frame: DragFloat
		// returns true continuously while being dragged, and this writes a
		// file -- saving every intermediate frame of a drag instead of once
		// at the end would mean a disk write per frame for as long as the
		// mouse is held down.
		bool saveNow = false;
		switch (myCurrentOperation)
		{
		case ImGuizmo::TRANSLATE:
			ImGui::Text("Translation Snap");
			ImGui::Checkbox("Enabled", &mySnap.snapPos);
			saveNow |= ImGui::IsItemDeactivatedAfterEdit();
			ImGui::DragFloat("Amount", &mySnap.pos);
			saveNow |= ImGui::IsItemDeactivatedAfterEdit();
			break;
		case ImGuizmo::ROTATE:
			ImGui::Text("Angle Snap");
			ImGui::Checkbox("Enabled", &mySnap.snapRot);
			saveNow |= ImGui::IsItemDeactivatedAfterEdit();
			ImGui::DragFloat("Amount", &mySnap.rot);
			saveNow |= ImGui::IsItemDeactivatedAfterEdit();
			break;
		case ImGuizmo::SCALE:
			ImGui::Text("Scale Snap");
			ImGui::Checkbox("Enabled", &mySnap.snapScale);
			saveNow |= ImGui::IsItemDeactivatedAfterEdit();
			ImGui::DragFloat("Amount", &mySnap.scale);
			saveNow |= ImGui::IsItemDeactivatedAfterEdit();
			break;
		}
		if (saveNow) SaveSnapSetting(mySnap);
	}
	
}

void Gizmos::UpdateShortcuts()
{
	const ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput || ImGui::IsAnyItemActive() || io.MouseDown[ImGuiMouseButton_Right])
		return;

	if (io.KeyCtrl)
	{
		// Ctrl+` flips between local and world space, like Unreal.
		if (!io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
			myCurrentMode = (uint16_t)(myCurrentMode == ImGuizmo::LOCAL ? ImGuizmo::WORLD : ImGuizmo::LOCAL);
		return;
	}
	if (io.KeyAlt)
		return;

	if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
		myCurrentOperation = 0;   // select only
	if (ImGui::IsKeyPressed(ImGuiKey_W, false))
		myCurrentOperation = ImGuizmo::TRANSLATE;
	if (ImGui::IsKeyPressed(ImGuiKey_E, false))
		myCurrentOperation = ImGuizmo::ROTATE;
	if (ImGui::IsKeyPressed(ImGuiKey_R, false))
		myCurrentOperation = ImGuizmo::SCALE;
	if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
	{
		// Space cycles move -> rotate -> scale.
		myCurrentOperation = (uint16_t)(myCurrentOperation == ImGuizmo::TRANSLATE ? ImGuizmo::ROTATE
			: (myCurrentOperation == ImGuizmo::ROTATE ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE));
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

			// Only finish a transaction we actually started on a gizmo handle.
			// Ending unconditionally created empty transform commands on ordinary
			// viewport clicks and could leave scale edits with stale state.
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && myIsManipulating)
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
