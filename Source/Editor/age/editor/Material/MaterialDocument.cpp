#include "stdafx.h"
#include <age/editor/Material/MaterialDocument.h>

#include <age/editor/imgui_widgets/imgui_widgets.h>
#include "imgui_internal.h" // DockBuilder
#include <imgui_node_editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

#include <age/imgui/ImGuiPropertyEditor.h>
#include <age/editor/Editor.h>
#include <age/editor/Material/ChangeMaterialCommand.h>
#include <age/editor/CommandManager/CommandManager.h>
#include "age/Application.h"
#include <age/settings/settings.h>
#include <imgui.h>
#include <IconFontHeaders/IconsLucide.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

using namespace Ag;

// The node editor derives node, pin and link ids from the same kind of Dear ImGui id, so a node 1 and
// a pin 1 collide ("items with conflicting ID"). Each kind gets its own range.
static constexpr uintptr_t kNodeIdSpace = 0x10000000u, kPinIdSpace = 0x20000000u, kLinkIdSpace = 0x30000000u, kEdIdMask = 0x0FFFFFFFu;
static ed::NodeId EdNode(int id) { return ed::NodeId((uintptr_t)id | kNodeIdSpace); }
static ed::PinId EdPin(int id) { return ed::PinId((uintptr_t)id | kPinIdSpace); }
static ed::LinkId EdLink(int id) { return ed::LinkId((uintptr_t)id | kLinkIdSpace); }
static int FromEd(uintptr_t id) { return (int)(id & kEdIdMask); }

using namespace Ag::MaterialGraphNS;

static const char* kPreviewMeshes[] = { "Sphere", "Cube", "Cylinder", "Cone", "Torus", "Plane" };

void MaterialDocument::Init(std::string_view aPath)
{
	Document::Init(aPath);

	myViewport.Init();
	myViewport.GetGrid().SetGridLineExtreme(4.0f);
	myGraphics = Editor::GetEditor()->GetEditorGraphics().CreateMaterialGraphicsInterface();

	std::filesystem::path path = aPath;
	myName = path.stem().string();
	myMaterial = MaterialAsset::Default();

	// The Content Browser hands Init() a path relative to the game asset root
	// (ContentBrowser.cpp: fs::relative(absPath, root)), which a bare
	// std::ifstream can't open from the process's own working directory --
	// every already-cooked .tgmat opened by double-click was silently
	// failing to load (falling back to MaterialAsset::Default(), with no
	// maps) while looking like the asset itself was never set up. Resolve it
	// the same way TextureManager does. CreateNewMaterial's brand-new-file
	// path from the native save dialog is already absolute, so falling back
	// to aPath verbatim when resolution fails still covers that case.
	std::string resolved = Ag::Settings::ResolveAssetPath(aPath);
	myResolvedPath = resolved.empty() ? std::string(aPath) : resolved;
	myMaterial.Load(myResolvedPath);      // ok if the file does not exist yet

	// Sibling ".tgmatgraph" JSON, opt-in: absent for every material that has
	// never used the node graph (which is every pre-existing .tgmat in the
	// project today), so those keep loading/rendering exactly as before.
	myGraphPath = std::filesystem::path(myResolvedPath).replace_extension(".tgmatgraph").string();
	myHasGraph = myGraph.Load(myGraphPath);
	// SettingsFile disabled: this library's own position/selection persistence
	// would otherwise write "NodeEditor.json" into the process's working
	// directory and collide across multiple materials open at once. Node
	// positions are already persisted ourselves via the .tgmatgraph sidecar
	// (Node::posX/posY, see DrawGraph()).
	ed::Config config;
	config.SettingsFile = nullptr;
	myGraphEditorContext = ed::CreateEditor(&config);

	char buffer[512];
	sprintf_s(buffer, "%s###Document:%s", myName.c_str(), std::string(aPath).c_str());
	myImGuiName = StringRegistry::RegisterOrGetString(buffer);

	sprintf_s(buffer, "Preview##Document:%s", aPath.data());
	myPanelWindowNames[(size_t)Panels::Preview] = buffer;
	sprintf_s(buffer, "Material##Document:%s", aPath.data());
	myPanelWindowNames[(size_t)Panels::Properties] = buffer;
	sprintf_s(buffer, "Graph##Document:%s", aPath.data());
	myPanelWindowNames[(size_t)Panels::Graph] = buffer;

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
	// myPath is the Asset-Browser-relative identity string (see Init()'s
	// comment); myResolvedPath is the actual openable file location. Saving
	// to myPath here previously meant Ctrl+S on a material opened via double
	// click silently wrote (or failed to write) to a bogus relative path
	// instead of the real file.
	if (myMaterial.Save(myResolvedPath))
		mySaveUndoStackSize = myUndoStackSize;
}

void MaterialDocument::Close()
{
	if (myGraphEditorContext)
	{
		ed::DestroyEditor(myGraphEditorContext);
		myGraphEditorContext = nullptr;
	}
}

void MaterialDocument::OnAction(CommandManager::Action action)
{
	// Same shape as SceneDocument::OnAction: myUndoStackSize now reflects
	// real transactions (ChangeMaterialCommand et al.) via CommandManager,
	// instead of the "changed |= widget" flag in DrawProperties() bumping it
	// directly with no relationship to the actual undo stack -- so Ctrl+Z
	// genuinely undoes a material edit now, not just decorating the title
	// bar with an asterisk.
	if (action == CommandManager::Action::Do)
	{
		if (myUndoStackSize < mySaveUndoStackSize)
			mySaveUndoStackSize = -1;
		myUndoStackSize++;
	}
	else if (action == CommandManager::Action::PostRedo)
	{
		myUndoStackSize++;
	}
	else if (action == CommandManager::Action::PostUndo)
	{
		myUndoStackSize--;
	}
	else if (action == CommandManager::Action::Clear)
	{
		myUndoStackSize = 0;
	}
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

		DrawToolbar();

		ImVec2 docSpaceSize = ImGui::GetContentRegionAvail();
		ImGuiID dockSpaceId = ImGui::GetID("Material Dockspace");
		ImGui::DockSpace(dockSpaceId, docSpaceSize, ImGuiDockNodeFlags_AutoHideTabBar, &myDocumentWindowClass);

		if (!myIsDockingInitialized && docSpaceSize.x > 0.0f && docSpaceSize.y > 0.0f)
		{
			ImGuiID center = 0, left = 0, leftBottom = 0;
			ImGui::DockBuilderRemoveNode(dockSpaceId);
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockSpaceId, docSpaceSize);
			center = dockSpaceId;
			// Preview over the material's details on the left, the graph gets everything else.
			ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.28f, &left, &center);
			ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.55f, &leftBottom, &left);
			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Preview].c_str(), left);
			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Properties].c_str(), leftBottom);
			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Graph].c_str(), center);
			if (ImGuiDockNode* graphNode = ImGui::DockBuilderGetNode(center))
				graphNode->SetLocalFlags(graphNode->LocalFlags | ImGuiDockNodeFlags_AutoHideTabBar);
			ImGui::DockBuilderFinish(dockSpaceId);
			myIsDockingInitialized = true;
		}
		ImGui::End();
	}

	const Ag::Color color = Ag::Application::GetInstance()->GetClearColor();
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
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Graph].c_str());
	DrawGraph();
	ImGui::End();
}

void MaterialDocument::DrawToolbar()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 5.f));
	ImGui::BeginChild("##MaterialToolbar", ImVec2(0.f, 34.f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
	ImGui::PopStyleVar();

	if (ImGui::Button(ICON_LC_SAVE " Save"))
		Save();
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	if (ImGui::Button(ICON_LC_HAMMER " Bake"))
		Bake();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("Evaluate the graph's connected outputs into real textures and save the material.");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(64.f); ImGui::DragInt("##bw", &myBakeWidth, 32.f, 64, 4096);
	ImGui::SameLine(0.f, 4.f); ImGui::TextDisabled("x");
	ImGui::SameLine(0.f, 4.f); ImGui::SetNextItemWidth(64.f); ImGui::DragInt("##bh", &myBakeHeight, 32.f, 64, 4096);
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	if (ImGui::Button(ICON_LC_SETTINGS " Preview"))
		ImGui::OpenPopup("MaterialPreviewSettings");
	if (ImGui::BeginPopup("MaterialPreviewSettings"))
	{
		if (myGraphics)
			myGraphics->DrawPreviewSettings();
		ImGui::EndPopup();
	}

	ImGui::EndChild();
}

void MaterialDocument::DrawProperties()
{
	// Capture the pre-edit value once, the first frame of an edit session
	// (not every frame) -- see myHasPendingMaterialEdit's declaration.
	if (!myHasPendingMaterialEdit)
		myUndoSnapshot = myMaterial;

	bool changed = false;
	{
	Ag::InspectorSection materialSection("Material", true, "The authored material asset and its rendering model.");
	if (materialSection.IsOpen() && Ag::BeginInspectorPropertyTable("MaterialIdentity"))
	{
		Ag::InspectorPropertyLabel("Actions");
		Ag::InspectorPropertyValue();
		if (ImGui::SmallButton("Reset Material"))
		{
			const std::string preservedPreviewMesh = myMaterial.previewMesh;
			myMaterial = MaterialAsset::Default();
			myMaterial.previewMesh = preservedPreviewMesh;
			changed = true;
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Restore all authored properties and texture assignments to their defaults.");
		Ag::InspectorPropertyLabel("Shader", "The shader family used to render this material.");
		Ag::InspectorPropertyValue(); ImGui::TextDisabled("%s", myMaterial.masterMaterial.c_str());
	const char* surfaceTypes[] = { "Opaque", "Masked", "Transparent" };
	int surfaceType = myMaterial.surfaceType == "Masked" ? 1 : myMaterial.surfaceType == "Transparent" ? 2 : 0;
		Ag::InspectorPropertyLabel("Surface Type", "Masked exposes Alpha Cutoff; Transparent renders as glass.");
		Ag::InspectorPropertyValue();
	if (ImGui::Combo("##Surface", &surfaceType, surfaceTypes, IM_ARRAYSIZE(surfaceTypes)))
	{
		myMaterial.surfaceType = surfaceTypes[surfaceType];
		changed = true;
	}
	if (surfaceType >= 1)
		{ Ag::InspectorPropertyLabel("Alpha Cutoff"); Ag::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##AlphaCutoff", &myMaterial.alphaCutoff, 0.01f, 0.99f, "%.2f"); }
	if (surfaceType == 2)
	{
		Ag::InspectorPropertyLabel("Opacity"); Ag::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##Opacity", &myMaterial.opacity, 0.f, 1.f, "%.2f");
		Ag::InspectorPropertyLabel("Index of Refraction"); Ag::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##Ior", &myMaterial.ior, 1.01f, 2.5f, "%.3f");
		Ag::InspectorPropertyLabel("Refraction Strength"); Ag::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##Refraction", &myMaterial.refractionScale, 0.f, 3.f, "%.2f");
		Ag::InspectorPropertyLabel("Thickness (cm)"); Ag::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##Thickness", &myMaterial.thickness, 0.f, 1.f, "%.3f m");
		Ag::InspectorPropertyLabel("Absorption", "Tinted by 1 - base colour."); Ag::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##Absorption", &myMaterial.absorption, 0.f, 4.f, "%.2f");
	}

		Ag::InspectorPropertyLabel("Preview Mesh"); Ag::InspectorPropertyValue();
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
		Ag::EndInspectorPropertyTable();
	}
	}

	{
	Ag::InspectorSection surfaceSection("Surface");
	if (surfaceSection.IsOpen() && Ag::BeginInspectorPropertyTable("SurfaceProperties"))
	{
		Ag::InspectorPropertyLabel("Base Colour"); Ag::InspectorPropertyValue(); changed |= ImGui::ColorEdit3("##BaseColour", myMaterial.baseColor, ImGuiColorEditFlags_Float);
		Ag::InspectorPropertyLabel("Metallic"); Ag::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##Metallic", &myMaterial.metalness, 0.f, 1.f, "%.3f");
		Ag::InspectorPropertyLabel("Roughness"); Ag::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##Roughness", &myMaterial.roughness, 0.f, 1.f, "%.3f");
		Ag::InspectorPropertyLabel("Ambient Occlusion"); Ag::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##AO", &myMaterial.ao, 0.f, 1.f, "%.3f");
		if (myMaterial.AnyMaps())
		{
			Ag::InspectorPropertyLabel("Base Colour Tint", "Multiplies the base colour map."); Ag::InspectorPropertyValue(); changed |= ImGui::ColorEdit3("##BaseTint", myMaterial.baseColorTint, ImGuiColorEditFlags_Float);
			Ag::InspectorPropertyLabel("Roughness Scale", "Multiplies ORM green."); Ag::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##RoughnessScale", &myMaterial.roughnessScale, 0.f, 2.f, "%.2f");
			Ag::InspectorPropertyLabel("Metallic Scale", "Multiplies ORM blue."); Ag::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##MetallicScale", &myMaterial.metalnessScale, 0.f, 1.f, "%.2f");
			Ag::InspectorPropertyLabel("AO Strength", "Blend towards ORM red."); Ag::InspectorPropertyValue(); changed |= ImGui::SliderFloat("##AoStrength", &myMaterial.aoStrength, 0.f, 1.f, "%.2f");
		}
		Ag::InspectorPropertyLabel("Normal Map Convention", "DirectX (green down) is Unreal's; OpenGL maps are flipped."); Ag::InspectorPropertyValue();
		{
			int convention = myMaterial.normalConvention == "OpenGL" ? 1 : 0;
			if (ImGui::Combo("##NormalConvention", &convention, "DirectX (Unreal) OpenGL ")) { myMaterial.normalConvention = convention ? "OpenGL" : "DirectX"; changed = true; }
		}
		Ag::InspectorPropertyLabel("Normal Strength", "Used only when a normal map is assigned."); Ag::InspectorPropertyValue();
		if (myMaterial.maps[1].empty()) { ImGui::BeginDisabled(); ImGui::SliderFloat("##NormalStrength", &myMaterial.normalStrength, 0.f, 4.f, "%.2f"); ImGui::EndDisabled(); }
		else changed |= ImGui::SliderFloat("##NormalStrength", &myMaterial.normalStrength, 0.f, 4.f, "%.2f");
		Ag::EndInspectorPropertyTable();
	}
	}

	{
	Ag::InspectorSection emissionSection("Emission");
	if (emissionSection.IsOpen() && Ag::BeginInspectorPropertyTable("EmissionProperties"))
	{
		Ag::InspectorPropertyLabel("Enable Emission"); Ag::InspectorPropertyValue(); bool emission = myMaterial.emissiveStrength > 0.f;
		// Default to a bright but ordinary emitter: 1000 cd/m² (a backlit sign).
		if (ImGui::Checkbox("##EmissionEnabled", &emission)) { myMaterial.emissiveStrength = emission ? Ag::Photometry::NitsToUnits(1000.f) : 0.f; changed = true; }
		ImGui::BeginDisabled(!emission);
		Ag::InspectorPropertyLabel("Emissive Colour"); Ag::InspectorPropertyValue(); changed |= ImGui::ColorEdit3("##EmissiveColour", myMaterial.emissiveColor, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		Ag::InspectorPropertyLabel("Luminance"); Ag::InspectorPropertyValue();
		float luminance = myMaterial.emissiveStrength * Ag::Photometry::kNitsPerUnit;
		if (ImGui::SliderFloat("##EmissiveLuminance", &luminance, 0.f, 1.0e7f, "%.0f cd/m2", ImGuiSliderFlags_Logarithmic))
		{
			myMaterial.emissiveStrength = Ag::Photometry::NitsToUnits(luminance);
			changed = true;
		}
		Ag::InspectorPropertyLabel("Emissive Map", "Auto: _FX files are the legacy mask pack, anything else is RGB colour (Unreal _E)."); Ag::InspectorPropertyValue();
		{
			int mode = myMaterial.emissiveMode == "RGB" ? 1 : myMaterial.emissiveMode == "Legacy" ? 2 : 0;
			if (ImGui::Combo("##EmissiveMode", &mode, "Auto RGB colour (_E) Legacy mask (_FX) ")) { myMaterial.emissiveMode = mode == 1 ? "RGB" : mode == 2 ? "Legacy" : "Auto"; changed = true; }
		}
		ImGui::EndDisabled(); Ag::EndInspectorPropertyTable();
	}
	}

	{
	Ag::InspectorSection texturesSection("Texture Maps", true, "Pick a cooked DDS texture from the list, or drop one here.");
	if (texturesSection.IsOpen() && Ag::BeginInspectorPropertyTable("TextureProperties"))
	{
	const char* slotNames[4] = { "Base Colour (_BC)", "Normal (_N)", "ORM (_ORM)", "Emissive (_E / _FX)" };
	for (int i = 0; i < 4; ++i)
	{
		ImGui::PushID(i);
		Ag::InspectorPropertyLabel(slotNames[i]); Ag::InspectorPropertyValue();
		StringId texture = myMaterial.maps[i].empty() ? StringId() : StringRegistry::RegisterOrGetString(myMaterial.maps[i]);
		if (PropertyEditor::AssetField("##texture", texture, { ".dds" }, "None (Texture)"))
		{
			myMaterial.maps[i] = texture.IsEmpty() ? std::string() : std::string(texture.GetString());
			changed = true;
		}
		ImGui::PopID();
	}
		Ag::EndInspectorPropertyTable();
	}
	}

	if (changed)
		myHasPendingMaterialEdit = true;

	// Commit once nothing in this panel is still being interacted with --
	// covers both instant widgets (Combo/Checkbox/Button, where this fires
	// the same frame as `changed`) and continuous drags (ColorEdit/Slider,
	// where it defers until mouse-up), so a whole drag is one undo entry,
	// not one per intermediate frame.
	if (myHasPendingMaterialEdit && !ImGui::IsAnyItemActive())
	{
		CommandManager::DoCommand(std::make_shared<ChangeMaterialCommand>(*this, myMaterial, myUndoSnapshot));
		myHasPendingMaterialEdit = false;
	}
}

void MaterialDocument::DrawGraph()
{
	// Adds a node at (x,y) in grid space and wires up bookkeeping. Shared by
	// the toolbar button's menu and the right-click canvas menu below.
	auto addNodeAt = [&](NodeKind k, float x, float y)
	{
		myGraph.AddNode(k, x, y);
		myHasGraph = true;
		myGraph.Save(myGraphPath);
	};

	if (ImGui::Button(ICON_LC_PLUS " Add Node"))
		ImGui::OpenPopup("AddGraphNodeMenu");
	ImGui::SameLine();
	ImGui::TextDisabled("Right-click the graph to add a node");

	ed::SetCurrentEditor(myGraphEditorContext);
	ed::Begin("MaterialGraph");

	for (const Node& constNode : myGraph.Nodes())
	{
		Node* node = myGraph.FindNode(constNode.id);
		// Tell the node editor where a node starts out exactly once (new
		// node, or first frame after loading) -- from then on it owns the
		// position itself as the user drags it. Unlike imnodes (see the old
		// comment this replaced), this library's SetNodePosition isn't tied
		// to a specific point in the frame -- but re-calling it every frame
		// would still stomp an in-progress drag the same way, so the
		// once-only guard is still required, just no longer timing-sensitive.
		if (myGraphPositionedNodes.insert(node->id).second)
			ed::SetNodePosition(EdNode(node->id), ImVec2(node->posX, node->posY));
		DrawGraphNode(*node);
	}
	for (const Link& link : myGraph.Links())
		ed::Link(EdLink(link.id), EdPin(link.fromPin), EdPin(link.toPin));

	// Explicit event-query pattern (this library's replacement for imnodes'
	// one-shot IsLinkCreated/IsLinkDestroyed/NumSelectedNodes+GetSelectedNodes):
	// each gesture is offered via Query*(), and must be explicitly Accept- or
	// Reject-ed before the corresponding End*() call.
	if (ed::BeginCreate())
	{
		ed::PinId startPinId, endPinId;
		if (ed::QueryNewLink(&startPinId, &endPinId) && startPinId && endPinId)
		{
			if (ed::AcceptNewItem())
			{
				const int startPin = FromEd(startPinId.Get());
				const int endPin = FromEd(endPinId.Get());
				// The library doesn't guarantee which end it reports first;
				// AddLink() itself validates output->input direction and
				// rejects the wrong one.
				if (!myGraph.AddLink(startPin, endPin)) myGraph.AddLink(endPin, startPin);
				myHasGraph = true;
				myGraph.Save(myGraphPath);
			}
		}
	}
	ed::EndCreate();

	if (ed::BeginDelete())
	{
		ed::LinkId deletedLinkId;
		while (ed::QueryDeletedLink(&deletedLinkId))
		{
			if (ed::AcceptDeletedItem())
			{
				myGraph.RemoveLink(FromEd(deletedLinkId.Get()));
				myGraph.Save(myGraphPath);
			}
		}
		ed::NodeId deletedNodeId;
		while (ed::QueryDeletedNode(&deletedNodeId))
		{
			const int nodeId = FromEd(deletedNodeId.Get());
			const Node* node = myGraph.FindNode(nodeId);
			if (node && node->kind == NodeKind::Output)
			{
				// Output nodes are protected -- reject rather than accept, so
				// the library doesn't visually remove it for a frame before
				// RemoveNode()'s own no-op would otherwise let it reappear.
				ed::RejectDeletedItem();
			}
			else if (ed::AcceptDeletedItem())
			{
				myGraph.RemoveNode(nodeId);
				myGraph.Save(myGraphPath);
			}
		}
	}
	ed::EndDelete();

	// Right-click anywhere in the panel to add a node -- the more
	// discoverable path (matches this engine's own ScriptEditor and UE's
	// material graph); the toolbar "Add Node" button above covers the same
	// action for anyone who doesn't think to right-click.
	//
	// Unlike the imnodes version (which detected the raw mouse-down itself,
	// before BeginNodeEditor()), this uses the library's own
	// ShowBackgroundContextMenu(). That's a deliberate departure, not just a
	// rename: right-click is also this library's default pan gesture
	// (Config::NavigateButtonIndex), and reacting to the raw mouse-down
	// ourselves stole every right-click before the library could tell a
	// click from the start of a drag -- confirmed live, panning by
	// right-click-drag stopped working entirely once that port landed.
	// ShowBackgroundContextMenu() does that click-vs-drag disambiguation
	// internally and only returns true for an actual (non-dragged) click.
	// Suspend()/Resume() (required around it, per the library's own asserts)
	// must be called before End(), not after -- they operate on the draw
	// list End() tears down. The popup itself is opened and drawn in this
	// same Suspend()/Resume() scope, matching thedmd/imgui-node-editor's own
	// examples -- ordinary ImGui::OpenPopup()/BeginPopup(), not the earlier
	// hand-rolled always-focused window (which never reliably received the
	// click: forcing window focus every single frame the menu was open, as
	// that version did, interfered with ImGui's own click-activation
	// tracking for the Selectable items inside it -- confirmed live, hover
	// highlighted normally but a click on an item never registered).
	bool hasPendingAdd = false;
	NodeKind pendingAddKind = NodeKind::ConstantScalar;
	ImVec2 pendingAddScreenPos{};

	ed::Suspend();
	if (ed::ShowBackgroundContextMenu())
		ImGui::OpenPopup("AddGraphNodeMenu");
	if (ImGui::BeginPopup("AddGraphNodeMenu"))
	{
		const ImVec2 openedAt = ImGui::GetMousePosOnOpeningCurrentPopup();
		static char nodeFilter[64] = "";
		if (ImGui::IsWindowAppearing())
		{
			nodeFilter[0] = '\0';
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(240.f);
		ImGui::InputTextWithHint("##nodefilter", ICON_LC_SEARCH " Search nodes", nodeFilter, sizeof(nodeFilter));
		ImGui::Separator();

		std::string lowerFilter = nodeFilter;
		for (char& c : lowerFilter) c = (char)std::tolower((unsigned char)c);
		auto contains = [](const char* text, const std::string& what)
		{
			std::string lower = text;
			for (char& c : lower) c = (char)std::tolower((unsigned char)c);
			return lower.find(what) != std::string::npos;
		};
		auto pick = [&](NodeKind k)
		{
			hasPendingAdd = true;
			pendingAddKind = k;
			pendingAddScreenPos = openedAt;
			ImGui::CloseCurrentPopup();
		};

		if (!lowerFilter.empty())
		{
			// Searching lists every match flat, with its category alongside.
			for (int i = 0; i < (int)NodeKind::Count; ++i)
			{
				const NodeKind k = (NodeKind)i;
				if (!MaterialGraph::IsAddable(k)) continue;
				const NodeDef& def = GetNodeDef(k);
				if (!contains(def.name, lowerFilter) && !contains(def.category, lowerFilter)) continue;
				if (ImGui::Selectable(def.name)) pick(k);
				ImGui::SameLine();
				ImGui::TextDisabled("%s", def.category);
			}
		}
		else
		{
			static const char* kCategories[] = { "Constants", "Math", "Vector", "Color", "Coordinates", "Procedural", "Texture" };
			for (const char* category : kCategories)
			{
				if (!ImGui::BeginMenu(category)) continue;
				for (int i = 0; i < (int)NodeKind::Count; ++i)
				{
					const NodeKind k = (NodeKind)i;
					if (!MaterialGraph::IsAddable(k) || std::string(GetNodeDef(k).category) != category) continue;
					if (ImGui::MenuItem(GetNodeDef(k).name)) pick(k);
				}
				ImGui::EndMenu();
			}
		}
		ImGui::EndPopup();
	}
	ed::Resume();

	ed::End();

	// ScreenToCanvas() deliberately runs out here, after End(), matching how
	// GetNodePosition() below already has to run after End() rather than
	// inside the node-drawing scope itself.
	if (hasPendingAdd)
	{
		ed::SetCurrentEditor(myGraphEditorContext);
		const ImVec2 canvasPos = ed::ScreenToCanvas(pendingAddScreenPos);
		addNodeAt(pendingAddKind, canvasPos.x, canvasPos.y);
	}

	// Persist wherever the user actually dragged each node to -- but only
	// for nodes ed:: actually knows about (myGraphPositionedNodes, populated
	// by the drawing loop above). A node added this same frame (via
	// hasPendingAdd, just above) was never BeginNode()'d/SetNodePosition()'d
	// this session, so GetNodePosition() for it doesn't return the position
	// AddNode() just gave it -- it returns a default the library made up for
	// an id it's never seen, clobbering the correct value right after
	// addNodeAt() saved it to disk. That's exactly why the new node used to
	// only ever show up after closing and reopening the document: the saved
	// file had the right position, but this loop immediately overwrote the
	// live in-memory copy with garbage, and the next frame's SetNodePosition
	// (see the drawing loop above) then handed that garbage straight to the
	// node editor. Confirmed live via logging: node count updated correctly
	// in myGraph, it just never rendered where expected.
	for (const Node& constNode : myGraph.Nodes())
	{
		if (!myGraphPositionedNodes.contains(constNode.id))
			continue;
		Node* node = myGraph.FindNode(constNode.id);
		const ImVec2 pos = ed::GetNodePosition(EdNode(node->id));
		node->posX = pos.x; node->posY = pos.y;
	}
}

static ImVec4 CategoryColor(const std::string& category)
{
	if (category == "Math") return ImVec4(0.55f, 0.75f, 1.f, 1.f);
	if (category == "Vector") return ImVec4(0.6f, 0.9f, 0.7f, 1.f);
	if (category == "Color") return ImVec4(1.f, 0.75f, 0.5f, 1.f);
	if (category == "Coordinates") return ImVec4(0.9f, 0.6f, 0.9f, 1.f);
	if (category == "Procedural") return ImVec4(0.95f, 0.85f, 0.5f, 1.f);
	if (category == "Texture") return ImVec4(0.6f, 0.9f, 1.f, 1.f);
	if (category == "Constants") return ImVec4(0.75f, 0.75f, 0.75f, 1.f);
	return ImVec4(1.f, 1.f, 1.f, 1.f);
}

void MaterialDocument::DrawGraphNode(MaterialGraphNS::Node& node)
{
	// Position is set exactly once, the first time this node is ever drawn
	// (see DrawGraph()) -- the node editor owns and persists it internally
	// from then on. Roomier than the 8px default on all sides -- the default
	// left content sitting flush against the dot/border.
	ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(14.f, 10.f, 14.f, 12.f));
	ed::BeginNode(EdNode(node.id));
	// Tracks the widest thing drawn so far, screen-space, so the output pin
	// column below can be flush against the node's actual right edge (like
	// Unreal's material graph) instead of just the widest output label --
	// title text and body widgets (a texture path button, a color swatch)
	// often are wider than any pin label, and the node autosizes to fit
	// *all* of them, not just the pin rows.
	const float nodeLeftScreenX = ImGui::GetCursorScreenPos().x;
	float contentWidth = 0.f;

	const char* title = node.kind == NodeKind::Output ? MaterialGraph::RootChannelName(node.outputChannel) : MaterialGraph::NodeKindName(node.kind);
	ImGui::TextColored(CategoryColor(node.kind == NodeKind::Output ? "" : GetNodeDef(node.kind).category), "%s", title);
	contentWidth = std::max(contentWidth, ImGui::GetItemRectMax().x - nodeLeftScreenX);
	ImGui::Dummy(ImVec2(0.f, 4.f));

	bool changed = false;
	const NodeDef& nodeDef = GetNodeDef(node.kind);
	ImGui::PushItemWidth(120.f);
	ImGui::PushID(node.id);
	switch (node.kind)
	{
	case NodeKind::TextureSample:
	{
		StringId texture = node.texturePath.empty() ? StringId() : StringRegistry::RegisterOrGetString(node.texturePath);
		ImGui::SetNextItemWidth(220.f);
		if (PropertyEditor::AssetField("##nodetexture", texture, { ".dds" }, "None (Texture)"))
		{
			node.texturePath = texture.IsEmpty() ? std::string() : std::string(texture.GetString());
			changed = true;
		}
		break;
	}
	case NodeKind::ConstantScalar:
		changed |= ImGui::DragFloat("##value", &node.constant[0], 0.01f, 0.f, 1.f);
		break;
	case NodeKind::ConstantVector:
		changed |= ImGui::ColorEdit3("##value", node.constant, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
		break;
	case NodeKind::ConstantVector4:
		changed |= ImGui::ColorEdit4("##value", node.constant, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
		break;
	case NodeKind::TexCoord:
		ImGui::TextUnformatted("Tiling"); ImGui::SameLine(); changed |= ImGui::DragFloat2("##tiling", &node.constant[0], 0.01f);
		ImGui::TextUnformatted("Offset"); ImGui::SameLine(); changed |= ImGui::DragFloat2("##offset", &node.constant[2], 0.01f);
		break;
	case NodeKind::RotateUV:
	{
		float center[2] = { node.paramA, node.paramB };
		ImGui::TextUnformatted("Center"); ImGui::SameLine();
		if (ImGui::DragFloat2("##center", center, 0.01f)) { node.paramA = center[0]; node.paramB = center[1]; changed = true; }
		break;
	}
	case NodeKind::ComponentMask:
	{
		const char* names[4] = { "R", "G", "B", "A" };
		for (int c = 0; c < 4; ++c)
		{
			if (c > 0) ImGui::SameLine();
			bool enabled = node.constant[c] > 0.5f;
			if (ImGui::Checkbox(names[c], &enabled)) { node.constant[c] = enabled ? 1.f : 0.f; changed = true; }
		}
		break;
	}
	case NodeKind::Checker:
	{
		float tiles[2] = { node.paramA, node.paramB };
		ImGui::TextUnformatted("Tiles"); ImGui::SameLine();
		if (ImGui::DragFloat2("##tiles", tiles, 0.1f, 1.f, 256.f)) { node.paramA = tiles[0]; node.paramB = tiles[1]; changed = true; }
		break;
	}
	case NodeKind::Noise:
	{
		int octaves = (int)std::lround(node.paramB);
		ImGui::TextUnformatted("Scale"); ImGui::SameLine(); changed |= ImGui::DragFloat("##scale", &node.paramA, 0.1f, 0.01f, 256.f);
		ImGui::TextUnformatted("Octaves"); ImGui::SameLine();
		if (ImGui::DragInt("##octaves", &octaves, 0.1f, 1, 8)) { node.paramB = (float)octaves; changed = true; }
		break;
	}
	case NodeKind::Voronoi:
		ImGui::TextUnformatted("Scale"); ImGui::SameLine(); changed |= ImGui::DragFloat("##scale", &node.paramA, 0.1f, 0.01f, 256.f);
		break;
	case NodeKind::Circle:
		ImGui::TextUnformatted("Radius"); ImGui::SameLine(); changed |= ImGui::DragFloat("##radius", &node.paramA, 0.005f, 0.f, 1.f);
		ImGui::TextUnformatted("Softness"); ImGui::SameLine(); changed |= ImGui::DragFloat("##softness", &node.paramB, 0.005f, 0.f, 0.5f);
		break;
	case NodeKind::Clamp:
		ImGui::TextUnformatted("Min"); ImGui::SameLine(); changed |= ImGui::DragFloat("##min", &node.paramA, 0.01f);
		ImGui::TextUnformatted("Max"); ImGui::SameLine(); changed |= ImGui::DragFloat("##max", &node.paramB, 0.01f);
		break;
	default:
		break;
	}
	// Harmless no-op for the NodeKinds whose switch case draws nothing
	// (Multiply/Add/Lerp/OneMinus/SplitChannels/CombineChannels/Output all
	// fall to `default`) -- GetItemRectMax() then still refers to the title
	// text above, so this just re-applies the same width already captured.
	contentWidth = std::max(contentWidth, ImGui::GetItemRectMax().x - nodeLeftScreenX);
	ImGui::PopItemWidth();
	ImGui::Dummy(ImVec2(0.f, 6.f));

	// thedmd/imgui-node-editor draws no pin visual of its own (unlike the
	// imnodes attributes this replaced, which auto-placed a colored dot at
	// the node's left/right edge) -- input/output columns and the dot itself
	// are drawn by hand here, same convention ScriptEditor.cpp's node UI
	// uses (see NodeEditorPinIcon in imgui_widgets.h). A single neutral pin
	// color is enough here since, unlike script pins, every material graph
	// pin carries the same float4 value type -- nothing to distinguish by
	// color.
	static constexpr ImU32 kPinColor = IM_COL32(200, 200, 200, 255);
	const auto hasOutgoing = [&](Id pinId)
	{
		for (const Link& link : myGraph.Links())
			if (link.fromPin == pinId) return true;
		return false;
	};

	float widthLeft = 0.f;
	for (size_t i = 0; i < node.inputPins.size(); ++i)
	{
		const Id pinId = node.inputPins[i];
		float rowWidth = ImGui::CalcTextSize(myGraph.FindPin(pinId)->name.c_str()).x;
		// Same "does this pin get an inline default-value control" test as
		// the actual drawing loop below -- account for that control's width
		// too, or a long enough pin name plus its default box would overflow
		// past the node's right edge instead of the node growing to fit it.
		const bool usesConstantDefault = i < nodeDef.inputs.size() && nodeDef.inputs[i].inlineDefault && i < 5;
		if (usesConstantDefault && !myGraph.IncomingLink(pinId))
			rowWidth += 60.f + ImGui::GetStyle().ItemSpacing.x;
		widthLeft = std::max(widthLeft, rowWidth);
	}
	float widthRight = 0.f;
	for (Id pinId : node.outputPins)
		widthRight = std::max(widthRight, ImGui::CalcTextSize(myGraph.FindPin(pinId)->name.c_str()).x);
	const float pinIconAndGap = 22.f;
	const float columnGap = 24.f;

	// Force the node to be at least wide enough for the title/body measured
	// above -- an invisible, zero-height item is enough, since the node
	// editor sizes the node to the bounding box of everything drawn between
	// BeginNode/EndNode, not just whatever's on the widest *line*.
	const float desiredWidth = std::max(contentWidth, widthLeft + pinIconAndGap + columnGap + widthRight + pinIconAndGap);
	ImGui::Dummy(ImVec2(desiredWidth, 0.f));

	const ImVec2 pinRowsStart = ImGui::GetCursorPos();

	for (size_t i = 0; i < node.inputPins.size(); ++i)
	{
		const Id pinId = node.inputPins[i];
		const Pin* pin = myGraph.FindPin(pinId);
		ed::BeginPin(EdPin(pinId), ed::PinKind::Input);
		NodeEditorPinIcon(kPinColor, myGraph.IncomingLink(pinId) != nullptr);
		const ImVec2 iconMin = ImGui::GetItemRectMin();
		const ImVec2 iconMax = ImGui::GetItemRectMax();
		const ImVec2 iconCenter((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f);
		ImGui::SameLine();
		ImGui::TextUnformatted(pin->name.c_str());
		// Constrain the pin's own hit/highlight rect to just the dot (default
		// is the whole icon+label bounding box, which hover-highlighted the
		// label too -- not what a Unreal-style pin looks like).
		ed::PinRect(iconMin, iconMax);
		// Anchor the link's visual attachment point to the dot's exact
		// center, not its whole bounding rect -- a non-degenerate pivot rect
		// makes the library attach the link to whichever point on that rect
		// is geometrically closest to the *other* pin, which visibly missed
		// the dot's center by a few pixels whenever the two pins weren't
		// perfectly level (a diagonal instead of a level line). Passing the
		// same point twice collapses that to always be the dot's center,
		// matching thedmd/imgui-node-editor's own examples.
		ed::PinPivotRect(iconCenter, iconCenter);
		ed::EndPin();
		// Multiply/Add/Lerp's A/B/T fall back to `constant[i]` when
		// unconnected (see MaterialGraph::Evaluate) -- expose that default
		// inline instead of forcing every input to be wired.
		const bool usesConstantDefault = i < nodeDef.inputs.size() && nodeDef.inputs[i].inlineDefault && i < 5;
		if (usesConstantDefault && !myGraph.IncomingLink(pinId))
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(60.f);
			ImGui::PushID((int)i);
			changed |= ImGui::DragFloat("##def", &node.constant[i], 0.01f);
			ImGui::PopID();
		}
	}
	const float inputColumnEndY = ImGui::GetCursorPosY();

	// Right column starts flush with the node's actual right edge (per
	// desiredWidth above), not just past the widest output label -- same
	// cursor-reset trick ScriptEditor.cpp's node UI already relies on for
	// laying out a second column.
	ImVec2 rowPos = pinRowsStart;
	rowPos.x = pinRowsStart.x + desiredWidth - widthRight - pinIconAndGap;
	for (Id pinId : node.outputPins)
	{
		const Pin* pin = myGraph.FindPin(pinId);
		ImGui::SetCursorPos(rowPos);
		ed::BeginPin(EdPin(pinId), ed::PinKind::Output);
		ImGui::SetCursorPosX(rowPos.x + (widthRight - ImGui::CalcTextSize(pin->name.c_str()).x));
		ImGui::TextUnformatted(pin->name.c_str());
		ImGui::SameLine();
		NodeEditorPinIcon(kPinColor, hasOutgoing(pinId));
		const ImVec2 iconMin = ImGui::GetItemRectMin();
		const ImVec2 iconMax = ImGui::GetItemRectMax();
		const ImVec2 iconCenter((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f);
		ed::PinRect(iconMin, iconMax);
		ed::PinPivotRect(iconCenter, iconCenter);
		ed::EndPin();
		rowPos.y = ImGui::GetCursorPosY();
	}

	// Move past whichever column (input, with its inline default-value
	// controls, or output) ended up taller, so the node's bottom padding
	// applies below all of it rather than wherever the last output row left
	// the cursor.
	ImGui::SetCursorPos(ImVec2(pinRowsStart.x, std::max(inputColumnEndY, rowPos.y)));

	ImGui::PopID();
	ed::EndNode();
	ed::PopStyleVar();

	if (changed)
		myGraph.Save(myGraphPath);
}

void MaterialDocument::Bake()
{
	if (!myGraphics) return;

	// mat.maps[] (and thus every path this bake writes) is stored relative to
	// the game asset root, matching every hand-authored .tgmat already in the
	// project -- see MaterialAsset's own "Sponza\Arches_C.dds" convention.
	std::error_code ec;
	std::filesystem::path rel = std::filesystem::relative(myResolvedPath, Ag::Settings::GameAssetRoot(), ec);
	std::string stem = (ec || rel.empty()) ? std::filesystem::path(myResolvedPath).stem().string() : rel.replace_extension().string();
	std::replace(stem.begin(), stem.end(), '\\', '/');

	MaterialGraphBakeRequest request;
	request.graph = &myGraph;
	request.material = &myMaterial;
	request.absoluteMatPath = myResolvedPath;
	request.gameRootRelativeStem = stem;
	request.width = myBakeWidth;
	request.height = myBakeHeight;

	if (myGraphics->BakeMaterialGraph(request))
		mySaveUndoStackSize = myUndoStackSize;   // the bake itself already saved the .tgmat
}
