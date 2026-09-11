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

#include <imnodes/imnodes.h>
#include <imnodes/imnodes_internal.h>

#include <IconFontHeaders/IconsLucide.h>

#include <sstream>
#include <fstream>
#include <filesystem>

using namespace Tga;

static std::unique_ptr<ScriptRuntimeInstance> locScriptRuntimeInstance;
static int frameNumber = 0;

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
	myOpenScripts.insert({ std::string(aName), EditorScriptData{ScriptManager::GetEditableScript(aName), {}, ImNodes::EditorContextCreate()} });
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
		if (entry.path().extension() != ".tgscript")
			continue;

		std::filesystem::path relPath = fs::relative(entry.path(), Settings::GameAssetRoot());
		relPath.replace_extension("");
		std::string relPathString = relPath.string();
		Script* script = ScriptManager::GetEditableScript(relPathString);

		if (!script)
			continue;

		EditorScriptData data{ script, {}, ImNodes::EditorContextCreate() };
		data.latestSavedSequenceNumber = script->GetSequenceNumber();

		myOpenScripts.insert({ relPathString, data});
	}
}

void Tga::EditorScriptManager::SaveAll()
{
	// todo: perforce integration!

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

	ImNodes::EditorContextSet(activeScript.nodeEditorContext);
	Script& script = *activeScript.script;



	// todo keep track of selection changes!
	// sync our list with imnodes

	for (ScriptNodeId currentNodeId = script.GetFirstNodeId(); currentNodeId.id != ScriptNodeId::InvalidId; currentNodeId = script.GetNextNodeId(currentNodeId))
	{
		Vector2f pos = script.GetPosition(currentNodeId);
		ImNodes::SetNodeGridSpacePos(currentNodeId.id, { pos.x, pos.y});
	}


	// if a link is in progress, set default style color to match the link color
	// all regular links will have their colors overriden so this does not affect them
	if (activeScript.inProgressLinkPin.id != ScriptPinId::InvalidId)
	{
		// todo check pin node also
		// if it is wrong type, highlight link as red

		const ScriptPin& pin = script.GetPin(activeScript.inProgressLinkPin);
		ImNodesStyle& style = ImNodes::GetStyle();

		const uint8_t* linkColor = GetScriptLinkColor(pin);
		const uint8_t* linkHoverColor = GetScriptLinkHoverColor(pin);
		const uint8_t* linkSelectedColor = GetScriptLinkSelectedColor(pin);

		style.Colors[ImNodesCol_Link] = IM_COL32(linkColor[0], linkColor[1], linkColor[2], 255);
		style.Colors[ImNodesCol_LinkSelected] = IM_COL32(linkHoverColor[0], linkHoverColor[1], linkHoverColor[2], 255);
		style.Colors[ImNodesCol_LinkHovered] = IM_COL32(linkSelectedColor[0], linkSelectedColor[1], linkSelectedColor[2], 255);
	}

	ImNodes::BeginNodeEditor();

	for (ScriptNodeId currentNodeId = script.GetFirstNodeId(); currentNodeId.id != ScriptNodeId::InvalidId; currentNodeId = script.GetNextNodeId(currentNodeId))
	{
		// todo: tooltip

		ImNodes::BeginNode(currentNodeId.id);

		bool isNodeHighlighted = ImNodes::IsNodeSelected(currentNodeId.id) || activeScript.hoveredNode == currentNodeId;

		{
			ImNodes::BeginNodeTitleBar();

			ScriptNodeTypeId dataTypeId = script.GetType(currentNodeId);
			std::string_view string = ScriptNodeTypeRegistry::GetNodeTypeShortName(dataTypeId);
			ImGui::TextUnformatted(string.data());

			ImNodes::EndNodeTitleBar();
		}

		ImVec2 cursorPos = ImGui::GetCursorPos();

		float widthLeft = 100.f;
		size_t inCount;
		const ScriptPinId* inPins = script.GetInputPins(currentNodeId, inCount);

		if (inCount == 0)
			widthLeft = 0.f;

		for (int i = 0; i < inCount; i++)
		{
			ScriptPinId pinId = inPins[i];
			const ScriptPin& pin = script.GetPin(pinId);
			std::string_view string = pin.name.GetString();
			const float labelWidth = ImGui::CalcTextSize(string.data()).x;

			widthLeft = std::max(widthLeft, labelWidth);
		}

		float widthRight = 0.f;

		size_t outCount;
		const ScriptPinId* outPins = script.GetOutputPins(currentNodeId, outCount);
		for (int i = 0; i < outCount; i++)
		{
			ScriptPinId pinId = outPins[i];
			const ScriptPin& pin = script.GetPin(pinId);
			std::string_view string = pin.name.GetString();
			const float labelWidth = ImGui::CalcTextSize(string.data()).x;

			widthRight = std::max(widthRight, labelWidth);
		}

		if (widthLeft > 0.f && widthRight > 0.f)
			widthRight += 20.f;


		{
			for (int i = 0; i < inCount; i++)
			{
				ScriptPinId pinId = inPins[i];
				const ScriptPin& pin = script.GetPin(pinId);

				const uint8_t* linkColor = GetScriptLinkColor(pin);
				const uint8_t* linkHoverColor = GetScriptLinkHoverColor(pin);
				const uint8_t* linkSelectedColor = GetScriptLinkSelectedColor(pin);

				if (isNodeHighlighted)
					ImNodes::PushColorStyle(ImNodesCol_Pin, IM_COL32(linkSelectedColor[0], linkSelectedColor[1], linkSelectedColor[2], 255));
				else
					ImNodes::PushColorStyle(ImNodesCol_Pin, IM_COL32(linkColor[0], linkColor[1], linkColor[2], 255));

				ImNodes::PushColorStyle(ImNodesCol_PinHovered, IM_COL32(linkHoverColor[0], linkHoverColor[1], linkHoverColor[2], 255));

				ImNodes::BeginInputAttribute(pinId.id);
				std::string_view string = pin.name.GetString();

				const float labelWidth = ImGui::CalcTextSize(string.data()).x;
				ImGui::TextUnformatted(string.data());

				size_t connectionCount;
				script.GetConnectedLinks(pinId, connectionCount);
				if (connectionCount == 0)
				{

					bool hasOverridenValue = pin.overridenValue.GetType() != nullptr;
					Property pinCurrentValue = hasOverridenValue ? pin.overridenValue : pin.defaultValue;

					ImGui::PushItemWidth(std::max(20.f, widthLeft - labelWidth));

					if (pin.type == ScriptLinkType::Property)
					{

						ImGui::SameLine();

						if (pinCurrentValue.ShowImGuiEditor())
						{
							CommandManager::DoCommand(std::make_shared<SetOverridenValueCommand>(script, activeScript.selection, pinId, pinCurrentValue));
						}
					}

					ImGui::PopItemWidth();
				}

				ImNodes::EndInputAttribute();

				ImNodes::PopColorStyle();
				ImNodes::PopColorStyle();
			}
		}

		ImGui::SetCursorPos(cursorPos);

		{

			for (int i = 0; i < outCount; i++)
			{
				ScriptPinId pinId = outPins[i];
				const ScriptPin& pin = script.GetPin(pinId);

				const uint8_t* linkColor = GetScriptLinkColor(pin);
				const uint8_t* linkHoverColor = GetScriptLinkHoverColor(pin);
				const uint8_t* linkSelectedColor = GetScriptLinkSelectedColor(pin);

				if (isNodeHighlighted)
					ImNodes::PushColorStyle(ImNodesCol_Pin, IM_COL32(linkSelectedColor[0], linkSelectedColor[1], linkSelectedColor[2], 255));
				else
					ImNodes::PushColorStyle(ImNodesCol_Pin, IM_COL32(linkColor[0], linkColor[1], linkColor[2], 255));

				ImNodes::PushColorStyle(ImNodesCol_PinHovered, IM_COL32(linkHoverColor[0], linkHoverColor[1], linkHoverColor[2], 255));

				ImNodes::BeginOutputAttribute(pinId.id);
				std::string_view string = pin.name.GetString();

				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + widthLeft + widthRight - ImGui::CalcTextSize(string.data()).x);

				ImGui::TextUnformatted(string.data());
				ImNodes::EndInputAttribute();


				ImNodes::PopColorStyle();
				ImNodes::PopColorStyle();
			}
		}

		ImNodes::EndNode();
	}

	for (ScriptLinkId linkId = script.GetFirstLinkId(); linkId.id != ScriptLinkId::InvalidId; linkId = script.GetNextLinkId(linkId))
	{
		const ScriptLink& link = script.GetLink(linkId);

		const ScriptPin& sourcePin = script.GetPin(link.sourcePinId);
		const ScriptPin& targetPin = script.GetPin(link.targetPinId);

		ScriptPin pin = sourcePin;
		if (sourcePin.type != targetPin.type || sourcePin.dataType != targetPin.dataType)
			pin.type = ScriptLinkType::Unknown;

		const uint8_t* linkColor = GetScriptLinkColor(pin);
		const uint8_t* linkHoverColor = GetScriptLinkHoverColor(pin);
		const uint8_t* linkSelectedColor = GetScriptLinkSelectedColor(pin);

		ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(linkColor[0], linkColor[1], linkColor[2], 255));
		ImNodes::PushColorStyle(ImNodesCol_LinkSelected, IM_COL32(linkHoverColor[0], linkHoverColor[1], linkHoverColor[2], 255));
		ImNodes::PushColorStyle(ImNodesCol_LinkHovered, IM_COL32(linkSelectedColor[0], linkSelectedColor[1], linkSelectedColor[2], 255));


		ImNodes::Link(linkId.id, link.sourcePinId.id, link.targetPinId.id);

		ImNodes::PopColorStyle();
		ImNodes::PopColorStyle();
		ImNodes::PopColorStyle();
	}

	ImNodes::EndNodeEditor();

	{
		int startedLinkPinId;
		if (ImNodes::IsLinkStarted(&startedLinkPinId))
		{
			activeScript.inProgressLinkPin = { (unsigned int)startedLinkPinId };
		}
		else
		{
			activeScript.inProgressLinkPin = { ScriptPinId::InvalidId };
		}
	}

	{
		int hoveredNodeId;
		if (ImNodes::IsNodeHovered(&hoveredNodeId))
		{
			activeScript.hoveredNode = { (unsigned int)hoveredNodeId };
		}
		else
		{
			activeScript.hoveredNode = { ScriptNodeId::InvalidId };
		}
	}

	for (ScriptNodeId currentNodeId = script.GetFirstNodeId(); currentNodeId.id != ScriptNodeId::InvalidId; currentNodeId = script.GetNextNodeId(currentNodeId))
	{
		Vector2f oldPos = script.GetPosition(currentNodeId);
		ImVec2 newPos = ImNodes::GetNodeGridSpacePos(currentNodeId.id);

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

	int startId, endId;
	if (ImNodes::IsLinkCreated(&startId, &endId))
	{
		ScriptPinId sourcePinId = { (unsigned int)startId };
		ScriptPinId targetPinId = { (unsigned int)endId };
		const ScriptPin& sourcePin = script.GetPin(sourcePinId);
		const ScriptPin& targetPin = script.GetPin(targetPinId);

		if (sourcePin.type == targetPin.type && sourcePin.dataType == targetPin.dataType && sourcePin.type != ScriptLinkType::Unknown)
		{
			std::shared_ptr<CreateLinkCommand> command = std::make_shared<CreateLinkCommand>(script, activeScript.selection, ScriptLink{ sourcePinId,targetPinId });
			Tga::CommandManager::DoCommand(command);
		}
	}

	int linkId;
	if (ImNodes::IsLinkDestroyed(&linkId))
	{
		std::shared_ptr<DestroyNodeAndLinksCommand> command = std::make_shared<DestroyNodeAndLinksCommand>(script, activeScript.selection);
		command->Add(ScriptLinkId{ (unsigned int)linkId });
		Tga::CommandManager::DoCommand(command);
	}

	if ((ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))
		&& !ImGui::IsAnyItemActive())
	{
		int numSelectedLinks = ImNodes::NumSelectedLinks();
		int numSelectedNodes = ImNodes::NumSelectedNodes();

		if (numSelectedLinks > 0 || numSelectedNodes > 0)
		{
			std::shared_ptr<DestroyNodeAndLinksCommand> command = std::make_shared<DestroyNodeAndLinksCommand>(script, activeScript.selection);

			if (numSelectedLinks > 0)
			{
				std::vector<int> selectedLinks;
				selectedLinks.resize(static_cast<size_t>(numSelectedLinks));
				ImNodes::GetSelectedLinks(selectedLinks.data());
				for (int i = 0; i < selectedLinks.size(); i++)
				{
					command->Add(ScriptLinkId{ (unsigned int)selectedLinks[i] });
				}
			}

			if (numSelectedNodes > 0)
			{
				static std::vector<int> selectedNodes;

				selectedNodes.resize(static_cast<size_t>(numSelectedNodes));
				ImNodes::GetSelectedNodes(selectedNodes.data());
				for (int i = 0; i < selectedNodes.size(); i++)
				{
					command->Add(ScriptNodeId{ (unsigned int)selectedNodes[i] });
				}
			}

			Tga::CommandManager::DoCommand(command);
		}
	}

	// on right click Either we clicked on a Pin and should offer to execute from that pin
	// If not, we will show the add node UI
	{
		struct Attribute{int id; enum class Type {Pin, Link} type; } static attribute;
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			if (ImNodes::IsPinHovered(&attribute.id))
			{
				if (aIsRunning)
				{
					attribute.type = Attribute::Type::Pin;
					ScriptPinId pinid{ .id = (unsigned int)attribute.id };
					if (script.GetPin(pinid).type == ScriptLinkType::Flow)
					{
						ImGui::OpenPopup("Trigger Node");
					}
				}
			}
			else if (ImNodes::IsLinkHovered(&attribute.id))
			{
				if (aIsRunning)
				{
					attribute.type = Attribute::Type::Link;
					ScriptLinkId linkid = { .id = (unsigned int)attribute.id };
					ScriptPinId pinid = script.GetLink(linkid).sourcePinId;
					if (script.GetPin(pinid).type == ScriptLinkType::Flow)
					{
						ImGui::OpenPopup("Trigger Node");
					}
				}
			}
			else if (ImGui::IsWindowHovered(ImGuiFocusedFlags_RootAndChildWindows))
			{
				ImGui::OpenPopup("Node Type Selection");
			}
		}

		// todo: Probably this should not all be in script editor. The clicking on pin/link makes sense, but the actual running could be doen in object definition document or similar?
		if (ImGui::BeginPopup("Trigger Node"))
		{
			char buf[128];
			sprintf_s(buf, "%s Execute", ICON_LC_PLAY);
			if (ImGui::Selectable(buf))
			{
				ScriptPinId pinid { .id = (unsigned int)attribute.id };
				if (attribute.type == Attribute::Type::Link)
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
			const ImVec2 clickPos = ImGui::GetMousePosOnOpeningCurrentPopup() - GImNodes->CanvasOriginScreenSpace - ImNodes::EditorContextGet().Panning;

			const ScriptNodeTypeRegistry::CategoryInfo& category = ScriptNodeTypeRegistry::GetRootCategory();
			ScriptNodeTypeId typeToCreate = ShowNodeTypeSelectorForCategory(category);

			if (typeToCreate.id != ScriptNodeTypeId::InvalidId)
			{
				std::shared_ptr<CreateNodeCommand> command = std::make_shared<CreateNodeCommand>(script, activeScript.selection, typeToCreate, Vector2f{ clickPos.x, clickPos.y });
				Tga::CommandManager::DoCommand(command);
			}

			ImGui::EndPopup();
		}
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("property_payload"))
		{
			struct Payload { PropertyTypeId type; StringId name; } data;
			memcpy(&data, (const uint8_t*)payload->Data, payload->DataSize);

			const ImVec2 dropPos = ImGui::GetMousePos() - GImNodes->CanvasOriginScreenSpace - ImNodes::EditorContextGetPanning();

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