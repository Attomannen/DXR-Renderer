#include "stdafx.h"
#include <tge/editor/Material/MaterialDocument.h>

#include <tge/editor/imgui_widgets/imgui_widgets.h>
#include "imgui_internal.h" // DockBuilder

#include <tge/imgui/ImGuiPropertyEditor.h>
#include <tge/editor/Editor.h>
#include <tge/editor/p4/p4.h>
#include "tge/Application.h"
#include <imgui.h>

#include <filesystem>

using namespace Tga;

static const char* kPreviewMeshes[] = { "Sphere", "Cube", "Cylinder", "Cone", "Torus", "Plane" };

void MaterialDocument::Init(std::string_view aPath)
{
	Document::Init(aPath);

	myViewport.Init();
	myViewport.GetGrid().SetGridLineExtreme(400.0f);
	myGraphics = Editor::GetEditor()->GetEditorGraphics().CreateMaterialGraphicsInterface();

	std::filesystem::path path = aPath;
	myName = path.stem().string();
	myMaterial = MaterialAsset::Default();
	myMaterial.Load(std::string(aPath));      // ok if the file does not exist yet

	char buffer[512];
	sprintf_s(buffer, "%s###Document:%s", myName.c_str(), std::string(aPath).c_str());
	myImGuiName = StringRegistry::RegisterOrGetString(buffer);

	sprintf_s(buffer, "Preview##Document:%s", aPath.data());
	myPanelWindowNames[(size_t)Panels::Preview] = buffer;
	sprintf_s(buffer, "Material##Document:%s", aPath.data());
	myPanelWindowNames[(size_t)Panels::Properties] = buffer;
	sprintf_s(buffer, "Preview Settings##Document:%s", aPath.data());
	myPanelWindowNames[(size_t)Panels::PreviewSettings] = buffer;

	Camera& camera = myViewport.GetCamera();
	Vector2i resolution = myViewport.GetViewportSize();
	camera.SetPerspectiveProjection(60, { (float)resolution.x, (float)resolution.y }, 0.1f, 50000.0f);

	Vector3f cameraRotation = { 20, 35, 0 };
	camera.GetTransform().SetRotation(cameraRotation);
	myViewport.SetCameraRotation(cameraRotation);
	myViewport.SetCameraFocusDistance(220.0f);
	camera.GetTransform().SetPosition(camera.GetTransform().GetForward() * -myViewport.GetCameraFocusDistance());
}

void MaterialDocument::Save()
{
	if (myMaterial.Save(myPath))
		mySaveUndoStackSize = myUndoStackSize;
}

void MaterialDocument::OnAction(CommandManager::Action action)
{
	if (action == CommandManager::Action::Clear)
		myUndoStackSize = 0;
}

void MaterialDocument::Update(float aTimeDelta, InputManager& inputManager)
{
	(void)inputManager;

	MaterialEditorDrawParameters params = { .viewport = &myViewport, .material = &myMaterial };
	if (myGraphics)
		myGraphics->Draw(params);

	char buffer[512];
	char asterix[2] = { 0, 0 };
	if (mySaveUndoStackSize != myUndoStackSize)
		asterix[0] = '*';
	sprintf_s(buffer, "%s%s###Document:%s", myName.c_str(), asterix, myPath.c_str());

	// See SceneDocument.cpp's identical guard: GetDocumentDockSpaceSize() can
	// still be {0,0} on this document's opening frame, which asserts inside
	// DockBuilderSetNodeSize -- wait for a real size instead.
	const ImVec2 outerDockSize = Editor::GetEditor()->GetDocumentDockSpaceSize();
	if (!myIsDockingInitialized && outerDockSize.x > 0.0f && outerDockSize.y > 0.0f)
	{
		ImGui::DockBuilderSetNodeSize(Editor::GetEditor()->GetDocumentDockSpaceId(), outerDockSize);
		ImGui::DockBuilderDockWindow(buffer, Editor::GetEditor()->GetDocumentDockSpaceId());
		ImGui::DockBuilderFinish(Editor::GetEditor()->GetDocumentDockSpaceId());
	}

	ImGui::SetNextWindowClass(Editor::GetEditor()->GetDocumentWindowClass());
	ImGui::SetNextWindowDockID(Editor::GetEditor()->GetDocumentDockSpaceId(), ImGuiCond_Once);

	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);

		bool open = true;
		ImGui::Begin(buffer, &open);
		if (myState == Document::State::Open && !open)
			myState = Document::State::CloseRequested;
		ImGui::PopStyleVar(2);

		ImVec2 docSpaceSize = ImGui::GetContentRegionAvail();
		ImGuiID dockSpaceId = ImGui::GetID("Material Dockspace");
		ImGui::DockSpace(dockSpaceId, docSpaceSize, ImGuiDockNodeFlags_None, &myDocumentWindowClass);

		if (!myIsDockingInitialized && docSpaceSize.x > 0.0f && docSpaceSize.y > 0.0f)
		{
			ImGuiID center = 0, left = 0, right = 0;
			ImGui::DockBuilderRemoveNode(dockSpaceId);
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockSpaceId, docSpaceSize);
			center = dockSpaceId;
			ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.28f, &left, &center);
			ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, &right, &center);
			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Properties].c_str(), left);
			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::PreviewSettings].c_str(), right);
			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Preview].c_str(), center);
			ImGui::DockBuilderFinish(dockSpaceId);
			myIsDockingInitialized = true;
		}
		ImGui::End();
	}

	const Tga::Color color = Tga::Application::GetInstance()->GetClearColor();
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(color.myR, color.myG, color.myB, color.myA));
	ImGui::SetNextWindowClass(&myDocumentWindowClass);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Preview].c_str());
	ImGui::PopStyleVar(1);
	myViewport.DrawAndUpdateViewportWindow(aTimeDelta, *this);
	ImGui::End();
	ImGui::PopStyleColor();

	ImGui::SetNextWindowClass(&myDocumentWindowClass);
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Properties].c_str());
	DrawProperties();
	ImGui::End();

	ImGui::SetNextWindowClass(&myDocumentWindowClass);
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::PreviewSettings].c_str());
	if (myGraphics)
		myGraphics->DrawPreviewSettings();
	ImGui::End();
}

void MaterialDocument::DrawProperties()
{
	bool changed = false;
	{
	Tga::InspectorSection materialSection("Material", true, "The authored material asset and its rendering model.");
	if (materialSection.IsOpen() && Tga::BeginInspectorPropertyTable("MaterialIdentity"))
	{
		Tga::InspectorPropertyLabel("Actions");
		Tga::InspectorPropertyValue();
		if (ImGui::SmallButton("Reset Material"))
		{
			const std::string preservedPreviewMesh = myMaterial.previewMesh;
			myMaterial = MaterialAsset::Default();
			myMaterial.previewMesh = preservedPreviewMesh;
			changed = true;
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Restore all authored properties and texture assignments to their defaults.");
		Tga::InspectorPropertyLabel("Shader", "The shader family used to render this material.");
		Tga::InspectorPropertyValue(); ImGui::TextDisabled("%s", myMaterial.masterMaterial.c_str());
	const char* surfaceTypes[] = { "Opaque", "Masked", "Transparent" };
	int surfaceType = myMaterial.surfaceType == "Masked" ? 1 : myMaterial.surfaceType == "Transparent" ? 2 : 0;
		Tga::InspectorPropertyLabel("Surface Type", "Masked exposes Alpha Cutoff; Transparent is blended.");
		Tga::InspectorPropertyValue();
	if (ImGui::Combo("##Surface", &surfaceType, surfaceTypes, IM_ARRAYSIZE(surfaceTypes)))
	{
		myMaterial.surfaceType = surfaceTypes[surfaceType];
		changed = true;
	}
	if (surfaceType == 1)
		{ Tga::InspectorPropertyLabel("Alpha Cutoff"); Tga::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##AlphaCutoff", &myMaterial.alphaCutoff, 0.01f, 0.99f, "%.2f"); }

		Tga::InspectorPropertyLabel("Preview Mesh"); Tga::InspectorPropertyValue();
	if (ImGui::BeginCombo("##PreviewMesh", myMaterial.previewMesh.c_str()))
	{
		for (const char* m : kPreviewMeshes)
		{
			bool sel = myMaterial.previewMesh == m;
			if (ImGui::Selectable(m, sel)) { myMaterial.previewMesh = m; changed = true; }
			if (sel) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
		Tga::EndInspectorPropertyTable();
	}
	}

	{
	Tga::InspectorSection surfaceSection("Surface");
	if (surfaceSection.IsOpen() && Tga::BeginInspectorPropertyTable("SurfaceProperties"))
	{
		Tga::InspectorPropertyLabel("Base Colour"); Tga::InspectorPropertyValue(); changed |= ImGui::ColorEdit3("##BaseColour", myMaterial.baseColor, ImGuiColorEditFlags_Float);
		Tga::InspectorPropertyLabel("Metallic"); Tga::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##Metallic", &myMaterial.metalness, 0.f, 1.f, "%.3f");
		Tga::InspectorPropertyLabel("Roughness"); Tga::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##Roughness", &myMaterial.roughness, 0.f, 1.f, "%.3f");
		Tga::InspectorPropertyLabel("Ambient Occlusion"); Tga::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##AO", &myMaterial.ao, 0.f, 1.f, "%.3f");
		Tga::InspectorPropertyLabel("Normal Strength", "Used only when a normal map is assigned."); Tga::InspectorPropertyValue();
		if (myMaterial.maps[1].empty()) { ImGui::BeginDisabled(); ImGui::SliderFloat("##NormalStrength", &myMaterial.normalStrength, 0.f, 4.f, "%.2f"); ImGui::EndDisabled(); }
		else changed |= ImGui::SliderFloat("##NormalStrength", &myMaterial.normalStrength, 0.f, 4.f, "%.2f");
		Tga::EndInspectorPropertyTable();
	}
	}

	{
	Tga::InspectorSection emissionSection("Emission");
	if (emissionSection.IsOpen() && Tga::BeginInspectorPropertyTable("EmissionProperties"))
	{
		Tga::InspectorPropertyLabel("Enable Emission"); Tga::InspectorPropertyValue(); bool emission = myMaterial.emissiveStrength > 0.f;
		if (ImGui::Checkbox("##EmissionEnabled", &emission)) { myMaterial.emissiveStrength = emission ? 1.f : 0.f; changed = true; }
		ImGui::BeginDisabled(!emission);
		Tga::InspectorPropertyLabel("Emissive Colour"); Tga::InspectorPropertyValue(); changed |= ImGui::ColorEdit3("##EmissiveColour", myMaterial.emissiveColor, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		Tga::InspectorPropertyLabel("Intensity"); Tga::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##EmissiveStrength", &myMaterial.emissiveStrength, 0.f, 16.f, "%.2f");
		ImGui::EndDisabled(); Tga::EndInspectorPropertyTable();
	}
	}

	{
	Tga::InspectorSection texturesSection("Texture Maps", true, "Drop a cooked DDS texture here, or select one in the Asset Browser and use the picker.");
	if (texturesSection.IsOpen() && Tga::BeginInspectorPropertyTable("TextureProperties"))
	{
	const char* slotNames[4] = { "Albedo (C)", "Normal (N)", "ORM (M)", "FX" };
	for (int i = 0; i < 4; ++i)
	{
		ImGui::PushID(i);
		Tga::InspectorPropertyLabel(slotNames[i]); Tga::InspectorPropertyValue();
		const char* assetName = myMaterial.maps[i].empty() ? "None (Texture)" : myMaterial.maps[i].c_str();
		if (ImGui::Button(assetName, ImVec2(-58, 0)))
		{
			std::string sel = Editor::GetEditor()->GetAssetBrowser().GetSelectedAsset().GetString();
			if (sel.ends_with(".dds")) { myMaterial.maps[i] = sel; changed = true; }
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Assign the selected DDS from Asset Browser, or drop one here.");
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(".dds"))
			{
				myMaterial.maps[i] = std::string((const char*)p->Data);
				changed = true;
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SameLine();
		if (Tga::InspectorResetButton("X", "Clear this texture assignment")) { myMaterial.maps[i].clear(); changed = true; }
		ImGui::PopID();
	}
		Tga::EndInspectorPropertyTable();
	}
	}

	if (changed)
	{
		if (myUndoStackSize == 0)
			P4::CheckoutFile(myPath.c_str());
		myUndoStackSize++;
	}
}
