#include "stdafx.h"

#include <tge/editor/ScriptEditor/ScriptEditor.h>

#include <tge/script/ScriptRuntimeInstance.h>
#include <tge/script/ScriptNodeTypeRegistry.h>
#include <tge/script/ScriptManager.h>
#include <tge/script/JsonData.h>
#include <tge/script/Script.h>

#include <tge/editor/ScriptEditor/Commands/CreateLinkCommand.h>
#include <tge/editor/ScriptEditor/Commands/CreateNodeCommand.h>
#include <tge/editor/ScriptEditor/Commands/DestroyNodeAndLinksCommand.h>
#include <tge/editor/ScriptEditor/Commands/FixupSelectionCommand.h>
#include <tge/editor/ScriptEditor/Commands/MoveNodesCommand.h>
#include <tge/editor/ScriptEditor/Commands/SetOverridenValueCommand.h>

#include <tge/script/BaseProperties.h>
#include <tge/script/Contexts/ScriptUpdateContext.h>

#include <tge/ImGui/ImGuiInterface.h>
#include <tge/editor/CommandManager/CommandManager.h>
#include <tge/stringRegistry/StringRegistry.h>

#include <imgui_node_editor/imgui_node_editor.h>
#include <tge/editor/imgui_widgets/imgui_widgets.h>

namespace ed = ax::NodeEditor;

#include <IconFontHeaders/IconsLucide.h>

#include <sstream>
#include <fstream>
#include <filesystem>

using namespace Tga;

// Deleted assets are moved into "<asset root>/.trash" (so a delete can be undone). They
// are not part of the project and must not be found by a scan of it.
static bool IsInAssetTrash(const std::filesystem::path& path)
{
	for (const auto& part : path)
		if (part == ".trash") return true;
	return false;
}

static std::unique_ptr<ScriptRuntimeInstance> locScriptRuntimeInstance;
static int frameNumber = 0;

// Disables this library's own position/selection persistence file -- it
// would otherwise write "NodeEditor.json" into the process's working
// directory and collide across every open script, since each gets its own
// EditorContext. Node positions are already persisted ourselves via the
// script's own JSON (Script::WriteToJson / ScriptNode position fields).
static ed::EditorContext* CreateScriptEditorContext()
{
	ed::Config config;
	config.SettingsFile = nullptr;
	return ed::CreateEditor(&config);
}

EditorScriptManager& EditorScriptManager::GetInstance()
{
	static EditorScriptManager s_instance;

	return s_instance;
}

const uint8_t* Tga::GetScriptLinkColor(const ScriptPin& pin)
{
	switch (pin.type)
	{
	case ScriptLinkType::Unknown:
	{
		static constexpr uint8_t color[] = { 245, 0, 0 };
		return color;
	}
	case ScriptLinkType::Flow:
	{
		static constexpr uint8_t color[] = { 185, 185, 185 };
		return color;
	}
	case ScriptLinkType::Property:
	{
		if (pin.dataType == GetPropertyType<bool>())
		{
			static constexpr uint8_t color[] = { 126, 0, 0 };
			return color;
		}
		else if (pin.dataType == GetPropertyType<int>())
		{
			static constexpr uint8_t color[] = { 13, 206, 151 };
			return color;
		}
		else if (pin.dataType == GetPropertyType<float>())
		{
			static constexpr uint8_t color[] = { 137, 235, 43 };
			return color;
		}
		else if (pin.dataType == GetPropertyType<StringId>())
		{
			static constexpr uint8_t color[] = { 206, 43, 206 };
			return color;
		}
	}
	}

	static constexpr uint8_t color[] = { 43, 43, 206 };
	return color;
}

const uint8_t* Tga::GetScriptLinkHoverColor(const ScriptPin& pin)
{
	switch (pin.type)
	{
	case ScriptLinkType::Unknown:
	{
		static constexpr uint8_t color[] = {255, 50, 50};
		return color;
	}
	case ScriptLinkType::Flow:
	{
		static constexpr uint8_t color[] = { 255, 255, 255 };
		return color;
	}
	case ScriptLinkType::Property:
	{
		if (pin.dataType == GetPropertyType<bool>())
		{
			static constexpr uint8_t color[] = { 196, 50, 50 };
			return color;
		}
		else if (pin.dataType == GetPropertyType<int>())
		{
			static constexpr uint8_t color[] = { 83, 255, 221 };
			return color;
		}
		else if (pin.dataType == GetPropertyType<float>())
		{
			static constexpr uint8_t color[] = { 207, 255, 113 };
			return color;
		}
		else if (pin.dataType == GetPropertyType<StringId>())
		{
			static constexpr uint8_t color[] = { 255, 113, 255 };
			return color;
		}
	}
	}
	static constexpr uint8_t color[] = { 113, 113, 255 };
	return color;
}

const uint8_t* Tga::GetScriptLinkSelectedColor(const ScriptPin& pin)
{
	switch (pin.type)
	{
	case ScriptLinkType::Unknown:
	{
		static constexpr uint8_t color[] = { 255, 50, 50 };
		return color;
	}
	case ScriptLinkType::Flow:
	{
		static constexpr uint8_t color[] = { 255, 255, 255 };
		return color;
	}
	case ScriptLinkType::Property:
	{
		if (pin.dataType == GetPropertyType<bool>())
		{
			static constexpr uint8_t color[] = { 196, 50, 50 };
			return color;
		}
		else if (pin.dataType == GetPropertyType<int>())
		{
			static constexpr uint8_t color[] = { 83, 255, 221 };
			return color;
		}
		else if (pin.dataType == GetPropertyType<float>())
		{
			static constexpr uint8_t color[] = { 207, 255, 113 };
			return color;
		}
		else if (pin.dataType == GetPropertyType<StringId>())
		{
			static constexpr uint8_t color[] = { 255, 113, 255 };
			return color;
		}
	}
	}

	static constexpr uint8_t color[] = { 113, 113, 255 };
	return color;
}


Tga::EditorScriptManager::EditorScriptManager()
{}

Tga::EditorScriptManager::~EditorScriptManager()
{}

Script& Tga::EditorScriptManager::CreateNewScript(const std::string_view& aName)
{
	ScriptManager::AddEditableScript(aName, std::make_unique<Script>());
	myOpenScripts.insert({ std::string(aName), EditorScriptData{ScriptManager::GetEditableScript(aName), {}, CreateScriptEditorContext()} });
	return *ScriptManager::GetEditableScript(aName);
}

void Tga::EditorScriptManager::MarkScriptAsRemoved(const std::string_view aName)
{
	myOpenScripts.find(aName)->second.hasBeenRemoved = true;
}

void Tga::EditorScriptManager::MarkScriptAsAdded(const std::string_view aName)
{
	myOpenScripts.find(aName)->second.hasBeenRemoved = false;
}

ScriptEditorSelection& Tga::EditorScriptManager::GetSelection(const std::string_view& aName)
{
	return myOpenScripts.find(aName)->second.selection;
}


void Tga::EditorScriptManager::GetAllScriptsThatStartsWithPath(const std::string_view path, std::vector<std::string_view>& scripts)
{
	// all scripts with path, will be just after the path in the
	auto it = myOpenScripts.upper_bound(path);

	// Loop as long as we have a sub paths
	for (; it != myOpenScripts.end() && it->first.compare(0, path.size(), path) == 0; ++it) 
	{
		if (it->second.hasBeenRemoved)
			continue;

		scripts.push_back(it->first);
	}
}

void Tga::EditorScriptManager::Init()
{
	// Load all scripts in the data/scripts folder:

	for (const auto& entry : std::filesystem::recursive_directory_iterator(Settings::GameAssetRoot()))
	{
		if (entry.path().extension() != ".tgscript" || IsInAssetTrash(entry.path()))
			continue;

		std::filesystem::path relPath = fs::relative(entry.path(), Settings::GameAssetRoot());
		relPath.replace_extension("");
		std::string relPathString = relPath.string();
		Script* script = ScriptManager::GetEditableScript(relPathString);

		if (!script)
			continue;

		EditorScriptData data{ script, {}, CreateScriptEditorContext() };
		data.latestSavedSequenceNumber = script->GetSequenceNumber();

		myOpenScripts.insert({ relPathString, data});
	}
}

void Tga::EditorScriptManager::SaveAll()
{
	for (auto& p : myOpenScripts)
	{
		FilePathStream pathStream;
		pathStream << Tga::Settings::GameAssetRoot() << "/" << p.first << ".tgscript";
		pathStream.NormalizePath();
		std::filesystem::path path(pathStream.GetData());
		if (p.second.hasBeenRemoved)
		{
			std::filesystem::remove(path);
		}
		else
		{
			JsonData jsonData;
			p.second.script->WriteToJson(jsonData);

			if (fs::exists(path))
				fs::permissions(path, fs::perms::all);

			std::ofstream out(path, std::ios::trunc);
			out << jsonData.json.dump(2);
			out.close();

			p.second.latestSavedSequenceNumber = p.second.script->GetSequenceNumber();
		}
	}
}

static ImU32 ColorU32(const uint8_t* aColor) { return IM_COL32(aColor[0], aColor[1], aColor[2], 255); }
static ImVec4 ColorVec4(const uint8_t* aColor) { return ImVec4(aColor[0] / 255.f, aColor[1] / 255.f, aColor[2] / 255.f, 1.f); }

ScriptNodeTypeId ShowNodeTypeSelectorForCategory(const ScriptNodeTypeRegistry::CategoryInfo& category)
{
	// todo: tooltip

	ScriptNodeTypeId result = { ScriptNodeTypeId::InvalidId };

	for (const ScriptNodeTypeRegistry::CategoryInfo& childCategory : category.childCategories)
	{
		std::string_view name = childCategory.name.GetString();

		if (ImGui::BeginMenu(name.data()))
		{
			ScriptNodeTypeId type = ShowNodeTypeSelectorForCategory(childCategory);
			if (type.id != ScriptNodeTypeId::InvalidId)
				result = type;

			ImGui::EndMenu();
		}
	}

	for (ScriptNodeTypeId type : category.nodeTypes)
	{
		std::string_view name = ScriptNodeTypeRegistry::GetNodeTypeShortName(type);
		// "ERROR" (CommonNodes.cpp) is an internal placeholder Script substitutes
		// in when a saved node's type can no longer be found on load -- not
		// something a user should ever deliberately add themselves.
		if (name == "ERROR")
			continue;
		if (ImGui::MenuItem(name.data()))
		{
			result = type;
		}
	}

	return result;
}

void Tga::EditorScriptManager::DisplayEditor(const std::string_view& aActiveScript, ScriptPinId &aPinToTrigger, bool aIsRunning)
{
	EditorScriptData& activeScript = myOpenScripts.find(aActiveScript)->second;

	ed::SetCurrentEditor(activeScript.nodeEditorContext);
	Script& script = *activeScript.script;

	// todo keep track of selection changes!
	// sync our list with imnodes

	// Reapplied every frame (not guarded to "once per node" like
	// MaterialDocument's graph) -- safe here because this loop, the read-back
	// loop near the bottom, and every place a *new* node gets created all
	// iterate script's own node list in a fixed order relative to each other
	// within one frame: position is set here, drawn, then read back below,
	// all before any node created this frame could exist yet. See
	// MaterialDocument.cpp's DrawGraph() for what goes wrong if a node's
	// position is read back before the library has ever had it BeginNode()'d.
	for (ScriptNodeId currentNodeId = script.GetFirstNodeId(); currentNodeId.id != ScriptNodeId::InvalidId; currentNodeId = script.GetNextNodeId(currentNodeId))
	{
		Vector2f pos = script.GetPosition(currentNodeId);
		ed::SetNodePosition(ed::NodeId(currentNodeId.id), ImVec2(pos.x, pos.y));
	}

	ed::Begin("ScriptGraph");

	for (ScriptNodeId currentNodeId = script.GetFirstNodeId(); currentNodeId.id != ScriptNodeId::InvalidId; currentNodeId = script.GetNextNodeId(currentNodeId))
	{
		// todo: tooltip

		ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(14.f, 10.f, 14.f, 12.f));
		ed::BeginNode(ed::NodeId(currentNodeId.id));

		bool isNodeHighlighted = ed::IsNodeSelected(ed::NodeId(currentNodeId.id)) || activeScript.hoveredNode == currentNodeId;

		const float nodeLeftScreenX = ImGui::GetCursorScreenPos().x;
		float contentWidth = 0.f;

		ScriptNodeTypeId dataTypeId = script.GetType(currentNodeId);
		std::string_view titleString = ScriptNodeTypeRegistry::GetNodeTypeShortName(dataTypeId);
		ImGui::TextUnformatted(titleString.data());
		contentWidth = std::max(contentWidth, ImGui::GetItemRectMax().x - nodeLeftScreenX);
		ImGui::Dummy(ImVec2(0.f, 4.f));

		size_t inCount;
		const ScriptPinId* inPins = script.GetInputPins(currentNodeId, inCount);
		size_t outCount;
		const ScriptPinId* outPins = script.GetOutputPins(currentNodeId, outCount);

		float widthRight = 0.f;
		for (size_t i = 0; i < outCount; i++)
			widthRight = std::max(widthRight, ImGui::CalcTextSize(script.GetPin(outPins[i]).name.GetString()).x);
		constexpr float kPinIconAndGap = 22.f;

		const ImVec2 pinRowsStart = ImGui::GetCursorPos();

		// Input pins first, so their actual drawn extent (including each
		// unconnected pin's inline default-value editor, whose width varies
		// by property type and isn't knowable up front the way
		// MaterialDocument's fixed-width DragFloats are) feeds directly into
		// contentWidth, rather than needing to be pre-measured.
		for (size_t i = 0; i < inCount; i++)
		{
			ScriptPinId pinId = inPins[i];
			const ScriptPin& pin = script.GetPin(pinId);
			std::string_view pinName = pin.name.GetString();

			const uint8_t* linkColor = GetScriptLinkColor(pin);
			const uint8_t* linkHoverColor = GetScriptLinkHoverColor(pin);
			const uint8_t* linkSelectedColor = GetScriptLinkSelectedColor(pin);
			const bool isPinHovered = ed::GetHoveredPin() == ed::PinId(pinId.id);
			const ImU32 iconColor = isPinHovered ? ColorU32(linkHoverColor) : isNodeHighlighted ? ColorU32(linkSelectedColor) : ColorU32(linkColor);
			size_t connectionCount;
			script.GetConnectedLinks(pinId, connectionCount);

			ed::BeginPin(ed::PinId(pinId.id), ed::PinKind::Input);
			NodeEditorPinIcon(iconColor, connectionCount > 0);
			const ImVec2 iconMin = ImGui::GetItemRectMin();
			const ImVec2 iconMax = ImGui::GetItemRectMax();
			const ImVec2 iconCenter((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f);
			ImGui::SameLine();
			ImGui::TextUnformatted(pinName.data());

			if (connectionCount == 0)
			{
				bool hasOverridenValue = pin.overridenValue.GetType() != nullptr;
				Property pinCurrentValue = hasOverridenValue ? pin.overridenValue : pin.defaultValue;

				if (pin.type == ScriptLinkType::Property)
				{
					ImGui::SameLine();
					ImGui::SetNextItemWidth(120.f);
					if (pinCurrentValue.ShowImGuiEditor())
						CommandManager::DoCommand(std::make_shared<SetOverridenValueCommand>(script, activeScript.selection, pinId, pinCurrentValue));
				}
			}

			ed::PinRect(iconMin, iconMax);
			ed::PinPivotRect(iconCenter, iconCenter);
			ed::EndPin();
			contentWidth = std::max(contentWidth, ImGui::GetItemRectMax().x - nodeLeftScreenX);
		}
		const float inputColumnEndY = ImGui::GetCursorPosY();

		const float desiredWidth = std::max(contentWidth + kPinIconAndGap, widthRight + kPinIconAndGap * 2.f);
		ImGui::Dummy(ImVec2(desiredWidth, 0.f));

		ImVec2 rowPos = pinRowsStart;
		rowPos.x = pinRowsStart.x + desiredWidth - widthRight - kPinIconAndGap;
		for (size_t i = 0; i < outCount; i++)
		{
			ScriptPinId pinId = outPins[i];
			const ScriptPin& pin = script.GetPin(pinId);
			std::string_view pinName = pin.name.GetString();

			const uint8_t* linkColor = GetScriptLinkColor(pin);
			const uint8_t* linkHoverColor = GetScriptLinkHoverColor(pin);
			const uint8_t* linkSelectedColor = GetScriptLinkSelectedColor(pin);
			const bool isPinHovered = ed::GetHoveredPin() == ed::PinId(pinId.id);
			const ImU32 iconColor = isPinHovered ? ColorU32(linkHoverColor) : isNodeHighlighted ? ColorU32(linkSelectedColor) : ColorU32(linkColor);
			size_t connectionCount;
			script.GetConnectedLinks(pinId, connectionCount);

			ImGui::SetCursorPos(rowPos);
			ed::BeginPin(ed::PinId(pinId.id), ed::PinKind::Output);
			ImGui::SetCursorPosX(rowPos.x + (widthRight - ImGui::CalcTextSize(pinName.data()).x));
			ImGui::TextUnformatted(pinName.data());
			ImGui::SameLine();
			NodeEditorPinIcon(iconColor, connectionCount > 0);
			const ImVec2 iconMin = ImGui::GetItemRectMin();
			const ImVec2 iconMax = ImGui::GetItemRectMax();
			const ImVec2 iconCenter((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f);
			ed::PinRect(iconMin, iconMax);
			ed::PinPivotRect(iconCenter, iconCenter);
			ed::EndPin();
			rowPos.y = ImGui::GetCursorPosY();
		}

		ImGui::SetCursorPos(ImVec2(pinRowsStart.x, std::max(inputColumnEndY, rowPos.y)));
		ed::EndNode();
		ed::PopStyleVar();
	}

	for (ScriptLinkId linkId = script.GetFirstLinkId(); linkId.id != ScriptLinkId::InvalidId; linkId = script.GetNextLinkId(linkId))
	{
		const ScriptLink& link = script.GetLink(linkId);

		const ScriptPin& sourcePin = script.GetPin(link.sourcePinId);
		const ScriptPin& targetPin = script.GetPin(link.targetPinId);

		ScriptPin pin = sourcePin;
		if (sourcePin.type != targetPin.type || sourcePin.dataType != targetPin.dataType)
			pin.type = ScriptLinkType::Unknown;

		// Simplification versus the imnodes version: that pushed three
		// separate style colors (Link/LinkSelected/LinkHovered) so imnodes'
		// own hover/selection state changed which one showed; this library
		// takes one fixed color straight as a Link() argument instead, and
		// leaves hover/selection feedback to its own default border style
		// (StyleColor_(Hov|Sel)LinkBorder) rather than fully replicating the
		// three-way color swap.
		ed::Link(ed::LinkId(linkId.id), ed::PinId(link.sourcePinId.id), ed::PinId(link.targetPinId.id), ColorVec4(GetScriptLinkColor(pin)), 2.0f);
	}

	// Link creation is a multi-frame gesture (drag from a pin, hover a
	// target, release) -- BeginCreate()/QueryNewLink() reports it every
	// frame it's in progress, which doubles as this frame's answer to "is a
	// link being dragged from this pin right now" (activeScript.
	// inProgressLinkPin, used above to color that pin while it's the source
	// of a drag -- imnodes' IsLinkStarted() query this replaces was a
	// separate one-shot call after EndNodeEditor()).
	activeScript.inProgressLinkPin = { ScriptPinId::InvalidId };
	if (ed::BeginCreate())
	{
		ed::PinId startPinId, endPinId;
		if (ed::QueryNewLink(&startPinId, &endPinId))
		{
			const ScriptPinId sourcePinId = { (unsigned int)startPinId.Get() };
			activeScript.inProgressLinkPin = sourcePinId;
			if (endPinId)
			{
				const ScriptPinId targetPinId = { (unsigned int)endPinId.Get() };
				const ScriptPin& sourcePin = script.GetPin(sourcePinId);
				const ScriptPin& targetPin = script.GetPin(targetPinId);
				const bool compatible = sourcePin.type == targetPin.type && sourcePin.dataType == targetPin.dataType && sourcePin.type != ScriptLinkType::Unknown;
				const ImVec4 previewColor = ColorVec4(compatible ? GetScriptLinkColor(sourcePin) : GetScriptLinkColor(ScriptPin{ .type = ScriptLinkType::Unknown }));
				if (compatible && ed::AcceptNewItem(previewColor, 2.0f))
				{
					std::shared_ptr<CreateLinkCommand> command = std::make_shared<CreateLinkCommand>(script, activeScript.selection, ScriptLink{ sourcePinId, targetPinId });
					Tga::CommandManager::DoCommand(command);
				}
				else if (!compatible)
				{
					ed::RejectNewItem(previewColor, 2.0f);
				}
			}
		}
	}
	ed::EndCreate();

	// Covers both a link dragged off its pin to delete it (only detectable
	// via this event-query pattern) and the Delete key (BeginDelete() wires
	// that internally) -- both funnel into the same undo-able command here,
	// where imnodes' IsLinkDestroyed() and the Delete-key branch below used
	// to build two separate commands for what's really one user action.
	// Backspace isn't wired internally by this library, so it's still
	// handled separately, below, the same way the original code did it.
	{
		std::shared_ptr<DestroyNodeAndLinksCommand> command;
		auto ensureCommand = [&]()
		{
			if (!command)
				command = std::make_shared<DestroyNodeAndLinksCommand>(script, activeScript.selection);
			return command;
		};
		if (ed::BeginDelete())
		{
			ed::LinkId deletedLinkId;
			while (ed::QueryDeletedLink(&deletedLinkId))
			{
				if (ed::AcceptDeletedItem())
					ensureCommand()->Add(ScriptLinkId{ (unsigned int)deletedLinkId.Get() });
			}
			ed::NodeId deletedNodeId;
			while (ed::QueryDeletedNode(&deletedNodeId))
			{
				if (ed::AcceptDeletedItem())
					ensureCommand()->Add(ScriptNodeId{ (unsigned int)deletedNodeId.Get() });
			}
		}
		ed::EndDelete();
		if (command)
			Tga::CommandManager::DoCommand(command);
	}

	activeScript.hoveredNode = { (unsigned int)ed::GetHoveredNode().Get() };

	// Persist wherever the user actually dragged each node to -- safe to run
	// unconditionally here (unlike MaterialDocument's equivalent loop, which
	// has to skip nodes it hasn't drawn yet this session) because any node
	// created this frame is added further down, after this loop runs.
	for (ScriptNodeId currentNodeId = script.GetFirstNodeId(); currentNodeId.id != ScriptNodeId::InvalidId; currentNodeId = script.GetNextNodeId(currentNodeId))
	{
		Vector2f oldPos = script.GetPosition(currentNodeId);
		ImVec2 newPos = ed::GetNodePosition(ed::NodeId(currentNodeId.id));

		if (newPos.x != oldPos.x || newPos.y != oldPos.y)
		{
			if (activeScript.inProgressMove == nullptr)
			{
				activeScript.inProgressMove = std::make_shared<MoveNodesCommand>(script, activeScript.selection);
				Tga::CommandManager::DoCommand(activeScript.inProgressMove);
			}

			script.SetPosition(currentNodeId, { newPos.x, newPos.y });
			activeScript.inProgressMove->SetPosition(currentNodeId, oldPos, { newPos.x, newPos.y });
		}
	}

	// clear in progress move if dragging ends
	if (!ImGui::IsMouseDown(0) && activeScript.inProgressMove)
	{
		activeScript.inProgressMove = nullptr;
	}

	// This library doesn't wire Backspace as a delete key itself (only
	// Delete, handled above via BeginDelete()), so it's kept as the same
	// explicit selection-based path the original imnodes code used for both
	// keys.
	if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !ImGui::IsAnyItemActive())
	{
		const int maxSelected = ed::GetSelectedObjectCount();
		std::vector<ed::NodeId> selectedNodeIds(maxSelected);
		selectedNodeIds.resize(ed::GetSelectedNodes(selectedNodeIds.data(), maxSelected));
		std::vector<ed::LinkId> selectedLinkIds(maxSelected);
		selectedLinkIds.resize(ed::GetSelectedLinks(selectedLinkIds.data(), maxSelected));

		if (!selectedNodeIds.empty() || !selectedLinkIds.empty())
		{
			std::shared_ptr<DestroyNodeAndLinksCommand> command = std::make_shared<DestroyNodeAndLinksCommand>(script, activeScript.selection);
			for (ed::LinkId id : selectedLinkIds) command->Add(ScriptLinkId{ (unsigned int)id.Get() });
			for (ed::NodeId id : selectedNodeIds) command->Add(ScriptNodeId{ (unsigned int)id.Get() });
			Tga::CommandManager::DoCommand(command);
		}
	}

	// on right click Either we clicked on a Pin and should offer to execute from that pin
	// If not, we will show the add node UI
	//
	// Uses this library's own ShowPinContextMenu()/ShowLinkContextMenu()/
	// ShowBackgroundContextMenu() instead of imnodes-era raw
	// IsMouseClicked(Right) + IsPinHovered()/IsLinkHovered() detection --
	// right-click is also this library's default pan gesture, and reacting
	// to the raw mouse-down the old way stole every right-click before the
	// library could tell a click from the start of a drag (see
	// MaterialDocument.cpp's DrawGraph() for where that broke panning during
	// this same migration). These three queries do that disambiguation
	// internally, the same as the background one already fixed there.
	bool hasPendingCreate = false;
	ScriptNodeTypeId pendingCreateType = { ScriptNodeTypeId::InvalidId };
	ImVec2 pendingCreateScreenPos{};

	static struct { int id; enum class Type { Pin, Link } type; } attribute;
	ed::Suspend();
	ed::PinId contextPinId; ed::LinkId contextLinkId;
	if (ed::ShowPinContextMenu(&contextPinId))
	{
		if (aIsRunning)
		{
			attribute.id = (int)contextPinId.Get();
			attribute.type = decltype(attribute)::Type::Pin;
			ScriptPinId pinid{ .id = (unsigned int)attribute.id };
			if (script.GetPin(pinid).type == ScriptLinkType::Flow)
				ImGui::OpenPopup("Trigger Node");
		}
	}
	else if (ed::ShowLinkContextMenu(&contextLinkId))
	{
		if (aIsRunning)
		{
			attribute.id = (int)contextLinkId.Get();
			attribute.type = decltype(attribute)::Type::Link;
			ScriptLinkId linkid = { .id = (unsigned int)attribute.id };
			ScriptPinId pinid = script.GetLink(linkid).sourcePinId;
			if (script.GetPin(pinid).type == ScriptLinkType::Flow)
				ImGui::OpenPopup("Trigger Node");
		}
	}
	else if (ed::ShowBackgroundContextMenu())
	{
		ImGui::OpenPopup("Node Type Selection");
	}

	// todo: Probably this should not all be in script editor. The clicking on pin/link makes sense, but the actual running could be doen in object definition document or similar?
	if (ImGui::BeginPopup("Trigger Node"))
	{
		char buf[128];
		sprintf_s(buf, "%s Execute", ICON_LC_PLAY);
		if (ImGui::Selectable(buf))
		{
			ScriptPinId pinid { .id = (unsigned int)attribute.id };
			if (attribute.type == decltype(attribute)::Type::Link)
			{
				ScriptLinkId linkid = { .id = (unsigned int)attribute.id };
				pinid = script.GetLink(linkid).sourcePinId;
			}
			if (pinid.id != pinid.InvalidId)
			{
				aPinToTrigger.id = pinid.id;
				// todo: notify script that pin should fire
				//		Then for example in ObjectDefinitionDocument which has the script runtime instance can call TriggerPin on it
			}
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("Node Type Selection"))
	{
		const ScriptNodeTypeRegistry::CategoryInfo& category = ScriptNodeTypeRegistry::GetRootCategory();
		ScriptNodeTypeId typeToCreate = ShowNodeTypeSelectorForCategory(category);

		if (typeToCreate.id != ScriptNodeTypeId::InvalidId)
		{
			hasPendingCreate = true;
			pendingCreateType = typeToCreate;
			pendingCreateScreenPos = ImGui::GetMousePosOnOpeningCurrentPopup();
		}

		ImGui::EndPopup();
	}
	ed::Resume();

	ed::End();

	// ScreenToCanvas() deliberately runs out here, after End() -- calling it
	// while suspended, inside the popup above, produced a canvas position
	// far outside the visible view for a new material graph node during this
	// same migration (see MaterialDocument.cpp's DrawGraph()); this avoids
	// that class of bug here too.
	if (hasPendingCreate)
	{
		ed::SetCurrentEditor(activeScript.nodeEditorContext);
		const ImVec2 clickPos = ed::ScreenToCanvas(pendingCreateScreenPos);
		std::shared_ptr<CreateNodeCommand> command = std::make_shared<CreateNodeCommand>(script, activeScript.selection, pendingCreateType, Vector2f{ clickPos.x, clickPos.y });
		Tga::CommandManager::DoCommand(command);
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("property_payload"))
		{
			struct Payload { PropertyTypeId type; StringId name; } data;
			memcpy(&data, (const uint8_t*)payload->Data, payload->DataSize);

			ed::SetCurrentEditor(activeScript.nodeEditorContext);
			const ImVec2 dropPos = ed::ScreenToCanvas(ImGui::GetMousePos());

			ScriptNodeTypeId typeToCreate;

			// todo: Perhaps this mapping could be more automatic?
			if (data.type == PropertyTypeRegistry::GetPropertyType("Color"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId("Read Color Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Bool"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId("Read Bool Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Int"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId("Read Int Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Float4"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId("Read Float4 Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Float"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId("Read Float Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Animation Clip"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId("Read Animation Clip Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Float2"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId("Read Float2 Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Float3"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId("Read Float3 Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("StringId"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId("Read String Property"); }

			if (typeToCreate.id != ScriptNodeTypeId::InvalidId)
			{
				std::shared_ptr<CreateNodeCommand> command = std::make_shared<CreateNodeCommand>(script, activeScript.selection, typeToCreate, Vector2f{ dropPos.x, dropPos.y });
				Tga::CommandManager::DoCommand(command);

				Tga::ScriptNodeId nodeid = script.GetLastNodeId();

				size_t cnt;
				const Tga::ScriptPinId* inPins = script.GetInputPins(nodeid, cnt);
				for (size_t i=0; i<cnt; i++)
				{
					Tga::ScriptPin pin = script.GetPin(inPins[i]);
					if (pin.name == "Name"_tgaid)
					{
						SetOverridenValueCommand cmd(script, activeScript.selection, inPins[i], Property::Create<StringId>(data.name));
						cmd.Execute();
						break;
					}
				}
			}
		}
	}
}