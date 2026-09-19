#include "stdafx.h"
#include <age/editor/ParticleSystem/ParticleSystemDocument.h>

#include <age/editor/ParticleSystem/ChangeParticleSystemCommand.h>
#include <age/editor/Editor.h>
#include <age/editor/CommandManager/CommandManager.h>
#include <age/imgui/ImGuiPropertyEditor.h>
#include <age/settings/settings.h>
#include "age/Application.h"

#include <imgui.h>
#include "imgui_internal.h" // DockBuilder
#include <IconFontHeaders/IconsLucide.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

using namespace Ag;
using namespace Ag::Particles;

namespace
{
	const char* StageTitle(ModuleStage aStage)
	{
		switch (aStage)
		{
		case ModuleStage::EmitterUpdate: return "Emitter Update";
		case ModuleStage::ParticleSpawn: return "Particle Spawn";
		default: return "Particle Update";
		}
	}

	ImVec4 StageColor(ModuleStage aStage)
	{
		switch (aStage)
		{
		case ModuleStage::EmitterUpdate: return ImVec4(0.95f, 0.75f, 0.4f, 1.f);
		case ModuleStage::ParticleSpawn: return ImVec4(0.55f, 0.85f, 0.6f, 1.f);
		default: return ImVec4(0.55f, 0.75f, 1.f, 1.f);
		}
	}

	// One label + value row of a property table.
	void Row(const char* aLabel, const char* aTooltip)
	{
		PropertyEditor::PropertyLabel();
		ImGui::TextUnformatted(aLabel);
		if (aTooltip && *aTooltip)
			PropertyEditor::HelpMarker(aTooltip);
		PropertyEditor::PropertyValue();
	}

	// Edits a piecewise linear curve. Drag a key to move it, double-click empty space to add one, right-click a key
	// to remove it. A colour curve (4 channels) is shown as a gradient; click a key to edit its colour.
	bool CurveWidget(const char* anId, Curve& aCurve, int aChannels)
	{
		bool changed = false;
		ImGui::PushID(anId);
		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID activeId = ImGui::GetID("active");
		const ImGuiID selectedId = ImGui::GetID("selected");

		if (aCurve.keys.size() < 2)
		{
			// A curve always has a start and an end.
			CurveKey key;
			for (int c = 0; c < 4; ++c) key.value[c] = aCurve.keys.empty() ? 1.f : aCurve.keys[0].value[c];
			aCurve.keys.clear();
			key.time = 0.f; aCurve.keys.push_back(key);
			key.time = 1.f; aCurve.keys.push_back(key);
			changed = true;
		}
		std::vector<CurveKey>& keys = aCurve.keys;

		const bool isColor = aChannels == 4;
		const float height = isColor ? 46.f : 96.f;
		const float width = std::max(80.f, ImGui::GetContentRegionAvail().x);
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1(p0.x + width, p0.y + height);
		ImGui::InvisibleButton("##area", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(p0, p1, IM_COL32(24, 24, 28, 255), 3.f);

		float low = 0.f, high = 1.f;
		if (!isColor)
		{
			for (const CurveKey& key : keys)
			{
				low = std::min(low, key.value[0]);
				high = std::max(high, key.value[0]);
			}
			const float pad = (high - low) * 0.08f;
			high += pad;
			if (low < 0.f) low -= pad;
		}
		const float range = std::max(1e-4f, high - low);
		const auto keyPosition = [&](const CurveKey& key)
		{
			if (isColor)
				return ImVec2(p0.x + key.time * width, p1.y - 6.f);
			return ImVec2(p0.x + key.time * width, p1.y - (key.value[0] - low) / range * height);
		};

		if (isColor)
		{
			const float barBottom = p1.y - 12.f;
			for (float x = 0.f; x < width; x += 3.f)
			{
				float out[4];
				aCurve.Evaluate(x / width, 4, out);
				const float a = std::clamp(out[3], 0.f, 1.f);
				const auto mix = [&](float c) { return std::clamp(c, 0.f, 1.f) * a + 0.1f * (1.f - a); };
				drawList->AddRectFilled(ImVec2(p0.x + x, p0.y), ImVec2(std::min(p0.x + x + 3.f, p1.x), barBottom), ImGui::ColorConvertFloat4ToU32(ImVec4(mix(out[0]), mix(out[1]), mix(out[2]), 1.f)));
				drawList->AddRectFilled(ImVec2(p0.x + x, barBottom), ImVec2(std::min(p0.x + x + 3.f, p1.x), p1.y), ImGui::ColorConvertFloat4ToU32(ImVec4(a, a, a, 1.f)));
			}
		}
		else
		{
			for (const float guide : { 0.f, 1.f })
			{
				const float y = p1.y - (guide - low) / range * height;
				drawList->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), IM_COL32(255, 255, 255, guide == 0.f ? 50 : 28));
			}
			ImVec2 previous(p0.x, keyPosition(keys.front()).y);
			for (const CurveKey& key : keys)
			{
				const ImVec2 position = keyPosition(key);
				drawList->AddLine(previous, position, IM_COL32(255, 190, 80, 255), 1.5f);
				previous = position;
			}
			drawList->AddLine(previous, ImVec2(p1.x, previous.y), IM_COL32(255, 190, 80, 255), 1.5f);
		}
		drawList->AddRect(p0, p1, IM_COL32(90, 90, 100, 255), 3.f);

		int active = storage->GetInt(activeId, -1);
		int selected = storage->GetInt(selectedId, -1);
		const ImVec2 mouse = ImGui::GetMousePos();
		int hoveredKey = -1;
		for (int i = 0; i < (int)keys.size(); ++i)
		{
			const ImVec2 position = keyPosition(keys[i]);
			const float dx = mouse.x - position.x, dy = mouse.y - position.y;
			if (dx * dx + dy * dy <= 64.f)
				hoveredKey = i;
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredKey >= 0)
		{
			active = hoveredKey;
			selected = hoveredKey;
		}
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hoveredKey < 0)
		{
			const float time = std::clamp((mouse.x - p0.x) / width, 0.f, 1.f);
			CurveKey key;
			key.time = time;
			aCurve.Evaluate(time, aChannels, key.value);
			if (!isColor)
				key.value[0] = low + (p1.y - mouse.y) / height * range;
			const auto after = std::find_if(keys.begin(), keys.end(), [&](const CurveKey& k) { return k.time > time; });
			selected = (int)(after - keys.begin());
			keys.insert(after, key);
			changed = true;
		}
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hoveredKey >= 0 && keys.size() > 2)
		{
			keys.erase(keys.begin() + hoveredKey);
			active = -1;
			selected = -1;
			changed = true;
		}
		if (active >= 0 && active < (int)keys.size() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			// A key cannot pass its neighbours, so the keys stay sorted.
			const float earliest = active > 0 ? keys[active - 1].time : 0.f;
			const float latest = active + 1 < (int)keys.size() ? keys[active + 1].time : 1.f;
			keys[active].time = std::clamp((mouse.x - p0.x) / width, earliest, latest);
			if (!isColor)
				keys[active].value[0] = low + (p1.y - mouse.y) / height * range;
			changed = true;
		}
		else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			active = -1;
		}

		for (int i = 0; i < (int)keys.size(); ++i)
		{
			const ImVec2 position = keyPosition(keys[i]);
			drawList->AddCircleFilled(position, 5.f, i == selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 160, 40, 255));
			drawList->AddCircle(position, 5.f, IM_COL32(0, 0, 0, 200));
		}
		if (!isColor && hoveredKey >= 0)
			ImGui::SetTooltip("time %.2f  value %.3f", keys[hoveredKey].time, keys[hoveredKey].value[0]);

		if (isColor && selected >= 0 && selected < (int)keys.size())
		{
			ImGui::SetNextItemWidth(-FLT_MIN);
			changed |= ImGui::ColorEdit4("##keycolor", keys[selected].value, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar);
		}

		storage->SetInt(activeId, active);
		storage->SetInt(selectedId, selected);
		ImGui::PopID();
		return changed;
	}

	bool DrawParam(const ParamDef& aDef, ParamValue& aValue, const std::vector<UserParameter>& someUserParameters)
	{
		bool changed = false;
		Row(aDef.name, aDef.tooltip);

		const bool bindable = aDef.type == ParamType::Float || aDef.type == ParamType::Vec3 || aDef.type == ParamType::Color;
		if (bindable && !aValue.binding.empty())
		{
			ImGui::TextColored(ImVec4(0.45f, 0.8f, 1.f, 1.f), ICON_LC_LINK " User.%s", aValue.binding.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton(ICON_LC_UNLINK "##unbind"))
			{
				aValue.binding.clear();
				changed = true;
			}
			return changed;
		}

		ImGui::SetNextItemWidth(bindable ? -34.f : -FLT_MIN);
		const bool limited = aDef.minValue != aDef.maxValue;
		const float minimum = limited ? aDef.minValue : 0.f;
		const float maximum = limited ? aDef.maxValue : 0.f;

		switch (aDef.type)
		{
		case ParamType::Float:
			changed |= ImGui::DragFloat("##v", &aValue.v[0], std::max(0.01f, std::fabs(aValue.v[0]) * 0.01f), minimum, maximum, "%.3f");
			break;
		case ParamType::Int:
		{
			int value = (int)std::lround(aValue.v[0]);
			if (ImGui::DragInt("##v", &value, 0.2f, (int)minimum, (int)maximum))
			{
				aValue.v[0] = (float)value;
				changed = true;
			}
			break;
		}
		case ParamType::Bool:
		{
			bool value = aValue.v[0] > 0.5f;
			if (ImGui::Checkbox("##v", &value))
			{
				aValue.v[0] = value ? 1.f : 0.f;
				changed = true;
			}
			break;
		}
		case ParamType::Vec3:
			changed |= ImGui::DragFloat3("##v", aValue.v, 0.05f, minimum, maximum, "%.2f");
			break;
		case ParamType::Color:
			changed |= ImGui::ColorEdit4("##v", aValue.v, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar);
			break;
		case ParamType::FloatRange:
			changed |= ImGui::DragFloat2("##v", aValue.v, 0.05f, minimum, maximum, "%.2f");
			break;
		case ParamType::Vec3Range:
			changed |= ImGui::DragFloat3("##min", &aValue.v[0], 0.05f, 0.f, 0.f, "%.2f");
			ImGui::SetNextItemWidth(-FLT_MIN);
			changed |= ImGui::DragFloat3("##max", &aValue.v[3], 0.05f, 0.f, 0.f, "%.2f");
			break;
		case ParamType::Curve:
			changed |= CurveWidget("curve", aValue.curve, 1);
			break;
		case ParamType::ColorCurve:
			changed |= CurveWidget("colorcurve", aValue.curve, 4);
			break;
		case ParamType::Enum:
		{
			int value = (int)std::lround(aValue.v[0]);
			if (ImGui::Combo("##v", &value, aDef.options))
			{
				aValue.v[0] = (float)value;
				changed = true;
			}
			break;
		}
		}

		if (bindable)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton(ICON_LC_LINK "##bind"))
				ImGui::OpenPopup("BindParameter");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Drive this value from a User Parameter");
			if (ImGui::BeginPopup("BindParameter"))
			{
				ImGui::TextDisabled("Bind to a User Parameter");
				int matches = 0;
				for (const UserParameter& user : someUserParameters)
				{
					if (user.type != aDef.type || user.name.empty())
						continue;
					++matches;
					if (ImGui::Selectable(user.name.c_str()))
					{
						aValue.binding = user.name;
						changed = true;
					}
				}
				if (matches == 0)
					ImGui::TextDisabled("No matching user parameter yet");
				ImGui::EndPopup();
			}
		}
		return changed;
	}

	std::string UniqueName(const std::string& aBase, const std::vector<std::string>& someTaken)
	{
		std::string name = aBase;
		for (int i = 2; std::find(someTaken.begin(), someTaken.end(), name) != someTaken.end(); ++i)
			name = aBase + " " + std::to_string(i);
		return name;
	}
}

ParticleSystemDocument::ParticleSystemDocument()
	: myPreview(SystemAsset{})
{
}

void ParticleSystemDocument::Init(std::string_view aPath)
{
	Document::Init(aPath);

	myViewport.Init();
	myGraphics = Editor::GetEditor()->GetEditorGraphics().CreateParticleGraphicsInterface();

	const std::filesystem::path path = aPath;
	myName = path.stem().string();

	// The Content Browser hands over a path relative to the asset root; a new asset arrives absolute.
	const std::string resolved = Ag::Settings::ResolveAssetPath(aPath);
	myResolvedPath = resolved.empty() ? std::string(aPath) : resolved;
	if (!myAsset.Load(myResolvedPath))
		myAsset = MakeDefaultSystem();
	myPreview.SetAsset(myAsset);

	char buffer[512];
	sprintf_s(buffer, "%s###Document:%s", myName.c_str(), std::string(aPath).c_str());
	myImGuiName = StringRegistry::RegisterOrGetString(buffer);

	const char* titles[(size_t)Panels::Count] = { "Preview", "System", "Details" };
	for (size_t i = 0; i < (size_t)Panels::Count; ++i)
	{
		sprintf_s(buffer, "%s##Document:%s", titles[i], std::string(aPath).c_str());
		myPanelWindowNames[i] = buffer;
	}

	Camera& camera = myViewport.GetCamera();
	const Vector2i resolution = myViewport.GetViewportSize();
	camera.SetPerspectiveProjection(60, { (float)resolution.x, (float)resolution.y }, 0.1f, 50000.0f);
	const Vector3f rotation = { 15, 30, 0 };
	camera.GetTransform().SetRotation(rotation);
	myViewport.SetCameraRotation(rotation);
	myViewport.SetCameraFocusDistance(7.f);
	camera.GetTransform().SetPosition(Vector3f(0.f, 2.f, 0.f) + camera.GetTransform().GetForward() * -myViewport.GetCameraFocusDistance());
}

void ParticleSystemDocument::Save()
{
	if (myAsset.Save(myResolvedPath))
		mySaveUndoStackSize = myUndoStackSize;
}

void ParticleSystemDocument::SetAsset(const SystemAsset& anAsset)
{
	myAsset = anAsset;
	myPreview.UpdateAsset(myAsset);
}

void ParticleSystemDocument::OnAction(CommandManager::Action action)
{
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

void ParticleSystemDocument::ClampSelection()
{
	const int emitterCount = (int)myAsset.emitters.size();
	if (mySelection.kind != SelectionKind::System && (mySelection.emitter < 0 || mySelection.emitter >= emitterCount))
	{
		mySelection = Selection{};
		return;
	}
	if (mySelection.kind == SelectionKind::Module)
	{
		const std::vector<ModuleInstance>& stack = myAsset.emitters[mySelection.emitter].Stack(mySelection.stage);
		if (mySelection.module < 0 || mySelection.module >= (int)stack.size())
		{
			mySelection.kind = SelectionKind::Emitter;
		}
	}
}

void ParticleSystemDocument::Update(float aTimeDelta, InputManager& inputManager)
{
	(void)inputManager;

	if (myGraphics)
	{
		ParticleEditorDrawParameters parameters = { &myViewport, &myPreview };
		myGraphics->Draw(parameters);
	}

	if (myPlaying)
	{
		myPreview.SetTransform(Matrix4x4f());
		myPreview.Update(aTimeDelta * myTimeScale);
		if (myLoopPreview && myPreview.IsComplete())
			myPreview.Reset();
	}

	char buffer[512];
	char asterix[2] = { 0, 0 };
	if (mySaveUndoStackSize != myUndoStackSize)
		asterix[0] = '*';
	sprintf_s(buffer, "%s%s###Document:%s", myName.c_str(), asterix, myPath.c_str());

	// The document dock space can still be {0,0} on a document's first frame; wait for a real size.
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

		const ImVec2 docSpaceSize = ImGui::GetContentRegionAvail();
		const ImGuiID dockSpaceId = ImGui::GetID("Particle Dockspace");
		ImGui::DockSpace(dockSpaceId, docSpaceSize, ImGuiDockNodeFlags_AutoHideTabBar, &myDocumentWindowClass);

		if (!myIsDockingInitialized && docSpaceSize.x > 0.0f && docSpaceSize.y > 0.0f)
		{
			ImGuiID center = 0, left = 0, right = 0;
			ImGui::DockBuilderRemoveNode(dockSpaceId);
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockSpaceId, docSpaceSize);
			center = dockSpaceId;
			ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.26f, &left, &center);
			ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, &right, &center);
			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::System].c_str(), left);
			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Details].c_str(), right);
			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Viewport].c_str(), center);
			if (ImGuiDockNode* previewNode = ImGui::DockBuilderGetNode(center))
				previewNode->SetLocalFlags(previewNode->LocalFlags | ImGuiDockNodeFlags_AutoHideTabBar);
			ImGui::DockBuilderFinish(dockSpaceId);
			myIsDockingInitialized = true;
		}
		ImGui::End();
	}

	// Edits from both panels are batched into one undo step per edit session.
	if (!myHasPendingEdit)
		myUndoSnapshot = myAsset;
	myChangedThisFrame = false;

	const Ag::Color clear = Ag::Application::GetInstance()->GetClearColor();
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(clear.myR, clear.myG, clear.myB, clear.myA));
	ImGui::SetNextWindowClass(&myDocumentWindowClass);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Viewport].c_str());
	ImGui::PopStyleVar(1);
	myViewport.DrawAndUpdateViewportWindow(aTimeDelta, *this);
	ImGui::End();
	ImGui::PopStyleColor();

	ImGui::SetNextWindowClass(&myDocumentWindowClass);
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::System].c_str());
	DrawSystemPanel();
	ImGui::End();

	ImGui::SetNextWindowClass(&myDocumentWindowClass);
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Details].c_str());
	DrawDetailsPanel();
	ImGui::End();

	if (myChangedThisFrame)
	{
		myHasPendingEdit = true;
		myPreview.UpdateAsset(myAsset);
	}
	// Commit once nothing is being dragged, so a whole drag is one undo entry.
	if (myHasPendingEdit && !ImGui::IsAnyItemActive())
	{
		CommandManager::DoCommand(std::make_shared<ChangeParticleSystemCommand>(*this, myAsset, myUndoSnapshot));
		myHasPendingEdit = false;
	}
}

void ParticleSystemDocument::DrawToolbar()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 5.f));
	ImGui::BeginChild("##ParticleToolbar", ImVec2(0.f, 34.f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
	ImGui::PopStyleVar();

	if (ImGui::Button(ICON_LC_SAVE " Save"))
		Save();
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	if (ImGui::Button(myPlaying ? ICON_LC_PAUSE : ICON_LC_PLAY))
		myPlaying = !myPlaying;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip(myPlaying ? "Pause the preview" : "Play the preview");
	ImGui::SameLine();
	if (ImGui::Button(ICON_LC_ROTATE_CCW " Restart"))
	{
		myPreview.Reset();
		myPlaying = true;
	}
	ImGui::SameLine();
	ImGui::Checkbox("Loop", &myLoopPreview);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Restart the preview when a one-shot system finishes");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.f);
	ImGui::SliderFloat("##speed", &myTimeScale, 0.1f, 2.f, "%.1fx");
	ImGui::SameLine();
	ImGui::TextDisabled("%zu particles", myPreview.AliveCount());

	ImGui::EndChild();
}

// ---------------------------------------------------------------- System panel

void ParticleSystemDocument::DrawSystemPanel()
{
	if (ImGui::Button(ICON_LC_PLUS " Emitter"))
	{
		std::vector<std::string> taken;
		for (const EmitterAsset& emitter : myAsset.emitters) taken.push_back(emitter.name);
		EmitterAsset emitter = MakeDefaultEmitter();
		emitter.name = UniqueName("Emitter", taken);
		myAsset.emitters.push_back(std::move(emitter));
		mySelection = Selection{ SelectionKind::Emitter, (int)myAsset.emitters.size() - 1, ModuleStage::ParticleUpdate, 0 };
		myChangedThisFrame = true;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%zu emitter(s)", myAsset.emitters.size());
	ImGui::Separator();

	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
		if (mySelection.kind == SelectionKind::System)
			flags |= ImGuiTreeNodeFlags_Selected;
		ImGui::TreeNodeEx(ICON_LC_SPARKLES " System", flags);
		if (ImGui::IsItemClicked())
			mySelection = Selection{};
	}

	myPending = PendingOp{};
	for (int i = 0; i < (int)myAsset.emitters.size(); ++i)
		DrawEmitterNode(i);

	ImGui::Spacing();
	DrawUserParameters();
	DrawAddModulePopup();

	// Structural edits run after the lists were drawn, so nothing is invalidated mid-loop.
	if (myPending.kind != PendingOp::Kind::None)
	{
		ApplyPendingOp();
		myChangedThisFrame = true;
	}
}

void ParticleSystemDocument::DrawEmitterNode(int anEmitterIndex)
{
	EmitterAsset& emitter = myAsset.emitters[anEmitterIndex];
	ImGui::PushID(anEmitterIndex);

	myChangedThisFrame |= ImGui::Checkbox("##enabled", &emitter.enabled);
	ImGui::SameLine();

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (mySelection.kind == SelectionKind::Emitter && mySelection.emitter == anEmitterIndex)
		flags |= ImGuiTreeNodeFlags_Selected;
	if (!emitter.enabled)
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	const bool open = ImGui::TreeNodeEx("##node", flags, ICON_LC_SPARKLES " %s", emitter.name.c_str());
	if (!emitter.enabled)
		ImGui::PopStyleColor();
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		mySelection = Selection{ SelectionKind::Emitter, anEmitterIndex, ModuleStage::ParticleUpdate, 0 };

	if (ImGui::BeginPopupContextItem("EmitterContext"))
	{
		if (ImGui::MenuItem(ICON_LC_COPY "  Duplicate"))
			myPending = PendingOp{ PendingOp::Kind::DuplicateEmitter, anEmitterIndex, ModuleStage::ParticleUpdate, 0, 0 };
		if (ImGui::MenuItem("Move Up", nullptr, false, anEmitterIndex > 0))
			myPending = PendingOp{ PendingOp::Kind::MoveEmitter, anEmitterIndex, ModuleStage::ParticleUpdate, 0, anEmitterIndex - 1 };
		if (ImGui::MenuItem("Move Down", nullptr, false, anEmitterIndex + 1 < (int)myAsset.emitters.size()))
			myPending = PendingOp{ PendingOp::Kind::MoveEmitter, anEmitterIndex, ModuleStage::ParticleUpdate, 0, anEmitterIndex + 1 };
		ImGui::Separator();
		if (ImGui::MenuItem(ICON_LC_TRASH_2 "  Delete"))
			myPending = PendingOp{ PendingOp::Kind::DeleteEmitter, anEmitterIndex, ModuleStage::ParticleUpdate, 0, 0 };
		ImGui::EndPopup();
	}

	if (open)
	{
		DrawStack(anEmitterIndex, ModuleStage::EmitterUpdate, StageTitle(ModuleStage::EmitterUpdate));
		DrawStack(anEmitterIndex, ModuleStage::ParticleSpawn, StageTitle(ModuleStage::ParticleSpawn));
		DrawStack(anEmitterIndex, ModuleStage::ParticleUpdate, StageTitle(ModuleStage::ParticleUpdate));

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.6f, 0.95f, 1.f));
		ImGui::TextUnformatted("Render");
		ImGui::PopStyleColor();
		ImGui::Indent(10.f);
		const bool selected = mySelection.kind == SelectionKind::Renderer && mySelection.emitter == anEmitterIndex;
		if (ImGui::Selectable(ICON_LC_IMAGE "  Sprite Renderer", selected))
			mySelection = Selection{ SelectionKind::Renderer, anEmitterIndex, ModuleStage::ParticleUpdate, 0 };
		ImGui::Unindent(10.f);
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void ParticleSystemDocument::DrawStack(int anEmitterIndex, ModuleStage aStage, const char* aTitle)
{
	std::vector<ModuleInstance>& stack = myAsset.emitters[anEmitterIndex].Stack(aStage);
	ImGui::PushID((int)aStage);

	ImGui::PushStyleColor(ImGuiCol_Text, StageColor(aStage));
	ImGui::TextUnformatted(aTitle);
	ImGui::PopStyleColor();
	ImGui::SameLine();
	if (ImGui::SmallButton(ICON_LC_PLUS "##add"))
	{
		myOpenAddPopup = true;
		myAddEmitter = anEmitterIndex;
		myAddStage = aStage;
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Add a module to %s", aTitle);

	ImGui::Indent(10.f);
	for (int j = 0; j < (int)stack.size(); ++j)
	{
		ModuleInstance& module = stack[j];
		const ModuleDef& def = GetModuleDef(module.type);
		ImGui::PushID(j);

		myChangedThisFrame |= ImGui::Checkbox("##on", &module.enabled);
		ImGui::SameLine();

		const bool selected = mySelection.kind == SelectionKind::Module && mySelection.emitter == anEmitterIndex
			&& mySelection.stage == aStage && mySelection.module == j;
		if (!module.enabled)
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		if (ImGui::Selectable(def.name, selected))
			mySelection = Selection{ SelectionKind::Module, anEmitterIndex, aStage, j };
		if (!module.enabled)
			ImGui::PopStyleColor();
		if (ImGui::IsItemHovered() && def.tooltip && *def.tooltip)
			ImGui::SetTooltip("%s", def.tooltip);

		if (selected && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
			myPending = PendingOp{ PendingOp::Kind::DeleteModule, anEmitterIndex, aStage, j, 0 };

		if (ImGui::BeginPopupContextItem("ModuleContext"))
		{
			if (ImGui::MenuItem(ICON_LC_COPY "  Duplicate"))
				myPending = PendingOp{ PendingOp::Kind::DuplicateModule, anEmitterIndex, aStage, j, 0 };
			if (ImGui::MenuItem("Move Up", nullptr, false, j > 0))
				myPending = PendingOp{ PendingOp::Kind::MoveModule, anEmitterIndex, aStage, j, j - 1 };
			if (ImGui::MenuItem("Move Down", nullptr, false, j + 1 < (int)stack.size()))
				myPending = PendingOp{ PendingOp::Kind::MoveModule, anEmitterIndex, aStage, j, j + 1 };
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_LC_TRASH_2 "  Delete", "Del"))
				myPending = PendingOp{ PendingOp::Kind::DeleteModule, anEmitterIndex, aStage, j, 0 };
			ImGui::EndPopup();
		}

		// Drag a module onto another to reorder within the same stack.
		if (ImGui::BeginDragDropSource())
		{
			const int payload[3] = { anEmitterIndex, (int)aStage, j };
			ImGui::SetDragDropPayload("particle-module", payload, sizeof(payload));
			ImGui::TextUnformatted(def.name);
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("particle-module"))
			{
				const int* source = static_cast<const int*>(payload->Data);
				if (source[0] == anEmitterIndex && source[1] == (int)aStage && source[2] != j)
					myPending = PendingOp{ PendingOp::Kind::MoveModule, anEmitterIndex, aStage, source[2], j };
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::PopID();
	}
	if (stack.empty())
		ImGui::TextDisabled("(empty)");
	ImGui::Unindent(10.f);
	ImGui::PopID();
}

void ParticleSystemDocument::DrawUserParameters()
{
	if (!ImGui::CollapsingHeader(ICON_LC_SLIDERS_HORIZONTAL " User Parameters", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	const auto add = [&](const char* aBase, ParamType aType)
	{
		std::vector<std::string> taken;
		for (const UserParameter& user : myAsset.userParameters) taken.push_back(user.name);
		UserParameter user;
		user.name = UniqueName(aBase, taken);
		user.type = aType;
		if (aType == ParamType::Color)
			for (float& channel : user.value) channel = 1.f;
		myAsset.userParameters.push_back(std::move(user));
		myChangedThisFrame = true;
	};
	if (ImGui::SmallButton("+ Float")) add("Float", ParamType::Float);
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Vector")) add("Vector", ParamType::Vec3);
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Color")) add("Color", ParamType::Color);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Gameplay sets these by name; any Float, Vector or Color module value can be bound to one.");

	int removeIndex = -1;
	for (int i = 0; i < (int)myAsset.userParameters.size(); ++i)
	{
		UserParameter& user = myAsset.userParameters[i];
		ImGui::PushID(i);

		char name[64];
		strncpy_s(name, user.name.c_str(), _TRUNCATE);
		ImGui::SetNextItemWidth(110.f);
		if (ImGui::InputText("##name", name, IM_ARRAYSIZE(name)) && name[0] != '\0' && user.name != name)
		{
			// Modules bound to the old name follow the rename.
			for (EmitterAsset& emitter : myAsset.emitters)
				for (const ModuleStage stage : { ModuleStage::EmitterUpdate, ModuleStage::ParticleSpawn, ModuleStage::ParticleUpdate })
					for (ModuleInstance& module : emitter.Stack(stage))
						for (ParamValue& value : module.params)
							if (value.binding == user.name)
								value.binding = name;
			user.name = name;
			myChangedThisFrame = true;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-30.f);
		if (user.type == ParamType::Float)
			myChangedThisFrame |= ImGui::DragFloat("##value", user.value, 0.05f);
		else if (user.type == ParamType::Vec3)
			myChangedThisFrame |= ImGui::DragFloat3("##value", user.value, 0.05f);
		else
			myChangedThisFrame |= ImGui::ColorEdit4("##value", user.value, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar);
		ImGui::SameLine();
		if (ImGui::SmallButton(ICON_LC_X "##remove"))
			removeIndex = i;
		ImGui::PopID();
	}
	if (removeIndex >= 0)
	{
		myAsset.userParameters.erase(myAsset.userParameters.begin() + removeIndex);
		myChangedThisFrame = true;
	}
}

void ParticleSystemDocument::DrawAddModulePopup()
{
	if (myOpenAddPopup)
	{
		ImGui::OpenPopup("AddModule");
		myOpenAddPopup = false;
	}
	if (!ImGui::BeginPopup("AddModule"))
		return;

	static char filter[64] = "";
	if (ImGui::IsWindowAppearing())
	{
		filter[0] = '\0';
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::SetNextItemWidth(260.f);
	ImGui::InputTextWithHint("##filter", ICON_LC_SEARCH " Search modules", filter, sizeof(filter));
	ImGui::Separator();

	std::string lowerFilter = filter;
	for (char& c : lowerFilter) c = (char)std::tolower((unsigned char)c);

	if (myAddEmitter >= 0 && myAddEmitter < (int)myAsset.emitters.size())
	{
		const char* category = "";
		for (int i = 0; i < (int)ModuleType::Count; ++i)
		{
			const ModuleDef& def = GetModuleDef((ModuleType)i);
			if (def.stage != myAddStage)
				continue;
			std::string lowerName = def.name;
			for (char& c : lowerName) c = (char)std::tolower((unsigned char)c);
			if (!lowerFilter.empty() && lowerName.find(lowerFilter) == std::string::npos)
				continue;
			if (std::string(category) != def.category)
			{
				category = def.category;
				ImGui::TextDisabled("%s", category);
			}
			if (ImGui::Selectable(def.name))
			{
				std::vector<ModuleInstance>& stack = myAsset.emitters[myAddEmitter].Stack(myAddStage);
				stack.push_back(CreateModule(def.type));
				mySelection = Selection{ SelectionKind::Module, myAddEmitter, myAddStage, (int)stack.size() - 1 };
				myChangedThisFrame = true;
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::IsItemHovered() && def.tooltip && *def.tooltip)
				ImGui::SetTooltip("%s", def.tooltip);
		}
	}
	ImGui::EndPopup();
}

void ParticleSystemDocument::ApplyPendingOp()
{
	const PendingOp op = myPending;
	myPending = PendingOp{};

	const auto moveWithin = [](auto& aVector, int aFrom, int aTo)
	{
		if (aFrom == aTo || aFrom < 0 || aTo < 0 || aFrom >= (int)aVector.size() || aTo >= (int)aVector.size())
			return false;
		auto item = std::move(aVector[aFrom]);
		aVector.erase(aVector.begin() + aFrom);
		aVector.insert(aVector.begin() + aTo, std::move(item));
		return true;
	};

	switch (op.kind)
	{
	case PendingOp::Kind::DeleteEmitter:
		myAsset.emitters.erase(myAsset.emitters.begin() + op.emitter);
		mySelection = Selection{};
		break;
	case PendingOp::Kind::DuplicateEmitter:
	{
		EmitterAsset copy = myAsset.emitters[op.emitter];
		copy.name += " Copy";
		myAsset.emitters.insert(myAsset.emitters.begin() + op.emitter + 1, std::move(copy));
		mySelection = Selection{ SelectionKind::Emitter, op.emitter + 1, ModuleStage::ParticleUpdate, 0 };
		break;
	}
	case PendingOp::Kind::MoveEmitter:
		if (moveWithin(myAsset.emitters, op.emitter, op.target))
			mySelection = Selection{ SelectionKind::Emitter, op.target, ModuleStage::ParticleUpdate, 0 };
		break;
	case PendingOp::Kind::DeleteModule:
	{
		std::vector<ModuleInstance>& stack = myAsset.emitters[op.emitter].Stack(op.stage);
		stack.erase(stack.begin() + op.index);
		mySelection = Selection{ SelectionKind::Emitter, op.emitter, ModuleStage::ParticleUpdate, 0 };
		break;
	}
	case PendingOp::Kind::DuplicateModule:
	{
		std::vector<ModuleInstance>& stack = myAsset.emitters[op.emitter].Stack(op.stage);
		ModuleInstance copy = stack[op.index];
		stack.insert(stack.begin() + op.index + 1, std::move(copy));
		mySelection = Selection{ SelectionKind::Module, op.emitter, op.stage, op.index + 1 };
		break;
	}
	case PendingOp::Kind::MoveModule:
		if (moveWithin(myAsset.emitters[op.emitter].Stack(op.stage), op.index, op.target))
			mySelection = Selection{ SelectionKind::Module, op.emitter, op.stage, op.target };
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------- Details panel

void ParticleSystemDocument::DrawDetailsPanel()
{
	ClampSelection();

	switch (mySelection.kind)
	{
	case SelectionKind::System:
	{
		ImGui::TextDisabled("System");
		if (PropertyEditor::BeginPropertyTable())
		{
			Row("Name", "");
			char name[128];
			strncpy_s(name, myAsset.name.c_str(), _TRUNCATE);
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputText("##name", name, IM_ARRAYSIZE(name)))
			{
				myAsset.name = name;
				myChangedThisFrame = true;
			}

			Row("Random Seed", "0 gives every instance its own random seed. Any other value makes the effect play out identically each time.");
			int seed = (int)myAsset.seed;
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragInt("##seed", &seed, 1.f, 0, INT_MAX))
			{
				myAsset.seed = (uint32_t)seed;
				myChangedThisFrame = true;
			}
			PropertyEditor::EndPropertyTable();
		}
		break;
	}
	case SelectionKind::Emitter:
		DrawEmitterDetails(myAsset.emitters[mySelection.emitter]);
		break;
	case SelectionKind::Renderer:
		DrawRendererDetails(myAsset.emitters[mySelection.emitter].renderer);
		break;
	case SelectionKind::Module:
		DrawModuleDetails(myAsset.emitters[mySelection.emitter].Stack(mySelection.stage)[mySelection.module]);
		break;
	}
}

void ParticleSystemDocument::DrawEmitterDetails(EmitterAsset& anEmitter)
{
	ImGui::TextDisabled("Emitter");
	if (!PropertyEditor::BeginPropertyTable())
		return;

	Row("Name", "");
	char name[128];
	strncpy_s(name, anEmitter.name.c_str(), _TRUNCATE);
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::InputText("##name", name, IM_ARRAYSIZE(name)))
	{
		anEmitter.name = name;
		myChangedThisFrame = true;
	}

	Row("Enabled", "");
	myChangedThisFrame |= ImGui::Checkbox("##enabled", &anEmitter.enabled);

	Row("Simulation Space", "World: particles stay where they were spawned. Local: they move with the object.");
	int space = (int)anEmitter.space;
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::Combo("##space", &space, "World\0Local\0"))
	{
		anEmitter.space = (SimulationSpace)space;
		myChangedThisFrame = true;
	}

	Row("Max Particles", "The pool size. Spawning stops while it is full.");
	ImGui::SetNextItemWidth(-FLT_MIN);
	myChangedThisFrame |= ImGui::DragInt("##max", &anEmitter.maxParticles, 1.f, 1, 100000);

	Row("Looping", "");
	myChangedThisFrame |= ImGui::Checkbox("##loop", &anEmitter.looping);

	Row("Loop Duration", "Seconds. Bursts fire at their time within this loop.");
	ImGui::SetNextItemWidth(-FLT_MIN);
	myChangedThisFrame |= ImGui::DragFloat("##duration", &anEmitter.loopDuration, 0.05f, 0.05f, 3600.f, "%.2f s");

	Row("Start Delay", "Seconds before the emitter starts.");
	ImGui::SetNextItemWidth(-FLT_MIN);
	myChangedThisFrame |= ImGui::DragFloat("##delay", &anEmitter.startDelay, 0.05f, 0.f, 3600.f, "%.2f s");

	PropertyEditor::EndPropertyTable();
}

void ParticleSystemDocument::DrawRendererDetails(RendererSettings& aRenderer)
{
	ImGui::TextDisabled("Sprite Renderer");
	if (!PropertyEditor::BeginPropertyTable())
		return;

	Row("Enabled", "");
	myChangedThisFrame |= ImGui::Checkbox("##enabled", &aRenderer.enabled);

	Row("Texture", "Drop a .dds here or pick one from the list.");
	{
		StringId texture = aRenderer.texture.empty() ? StringId() : StringRegistry::RegisterOrGetString(aRenderer.texture);
		if (PropertyEditor::AssetField("##texture", texture, { ".dds" }, "None"))
		{
			aRenderer.texture = texture.IsEmpty() ? std::string() : std::string(texture.GetString());
			myChangedThisFrame = true;
		}
	}

	Row("Blend Mode", "Alpha for smoke and dust, Additive for fire, sparks and glow.");
	int blend = (int)aRenderer.blend;
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::Combo("##blend", &blend, "Alpha\0Additive\0"))
	{
		aRenderer.blend = (BlendMode)blend;
		myChangedThisFrame = true;
	}

	Row("Facing", "Camera: always faces the viewer. Velocity Aligned: stretches along the motion. Vertical: stays upright.");
	int facing = (int)aRenderer.facing;
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::Combo("##facing", &facing, "Camera\0Velocity Aligned\0Vertical Billboard\0"))
	{
		aRenderer.facing = (FacingMode)facing;
		myChangedThisFrame = true;
	}
	if (aRenderer.facing == FacingMode::VelocityAligned)
	{
		Row("Velocity Stretch", "Extra length per unit of speed.");
		ImGui::SetNextItemWidth(-FLT_MIN);
		myChangedThisFrame |= ImGui::DragFloat("##stretch", &aRenderer.velocityStretch, 0.01f, 0.f, 10.f, "%.2f");
	}

	Row("Sorting", "Draw back to front so overlapping soft sprites blend correctly. Only matters for Alpha.");
	int sort = (int)aRenderer.sort;
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::Combo("##sort", &sort, "None\0View Depth\0"))
	{
		aRenderer.sort = (SortMode)sort;
		myChangedThisFrame = true;
	}

	Row("Flipbook Columns", "Set with Rows when the texture is a grid of frames; use the Sub UV Animation module to play them.");
	ImGui::SetNextItemWidth(-FLT_MIN);
	myChangedThisFrame |= ImGui::DragInt("##columns", &aRenderer.flipbookColumns, 0.1f, 1, 64);
	Row("Flipbook Rows", "");
	ImGui::SetNextItemWidth(-FLT_MIN);
	myChangedThisFrame |= ImGui::DragInt("##rows", &aRenderer.flipbookRows, 0.1f, 1, 64);

	Row("Brightness", "Luminance in cd/m2 of a white particle. The scene is photometric, so 1 would be blinding: about 2000 matches a sunlit cloud.");
	ImGui::SetNextItemWidth(-FLT_MIN);
	myChangedThisFrame |= ImGui::DragFloat("##brightness", &aRenderer.brightness, 10.f, 0.f, 1.0e7f, "%.0f cd/m2", ImGuiSliderFlags_Logarithmic);

	PropertyEditor::EndPropertyTable();
}

void ParticleSystemDocument::DrawModuleDetails(ModuleInstance& aModule)
{
	const ModuleDef& def = GetModuleDef(aModule.type);
	ImGui::TextColored(StageColor(def.stage), "%s", StageTitle(def.stage));
	ImGui::SameLine();
	ImGui::Text("%s", def.name);
	if (def.tooltip && *def.tooltip)
		ImGui::TextWrapped("%s", def.tooltip);

	if (!PropertyEditor::BeginPropertyTable())
		return;

	Row("Enabled", "");
	myChangedThisFrame |= ImGui::Checkbox("##enabled", &aModule.enabled);

	for (size_t i = 0; i < def.params.size() && i < aModule.params.size(); ++i)
	{
		ImGui::PushID((int)i);
		myChangedThisFrame |= DrawParam(def.params[i], aModule.params[i], myAsset.userParameters);
		ImGui::PopID();
	}
	PropertyEditor::EndPropertyTable();
}
