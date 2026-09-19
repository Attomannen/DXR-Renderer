#include "stdafx.h"

#include <age/editor/ScriptEditor/ScriptEditor.h>

#include <age/script/ScriptRuntimeInstance.h>
#include <age/script/ScriptNodeTypeRegistry.h>
#include <age/script/JsonData.h>
#include <age/script/Script.h>

#include <age/editor/ScriptEditor/Commands/CreateLinkCommand.h>
#include <age/editor/ScriptEditor/Commands/CreateNodeCommand.h>
#include <age/editor/ScriptEditor/Commands/DestroyNodeAndLinksCommand.h>
#include <age/editor/ScriptEditor/Commands/FixupSelectionCommand.h>
#include <age/editor/ScriptEditor/Commands/MoveNodesCommand.h>
#include <age/editor/ScriptEditor/Commands/SetOverridenValueCommand.h>
#include <age/editor/ScriptEditor/ScriptEditorNodeCatalog.h>
#include <age/editor/CommandManager/CompositeCommand.h>
#include <age/editor/ObjectDefinition/Commands/ChangePropertiesCommand.h>
#include <age/editor/Editor.h>
#include <age/script/Nodes/CommentNode.h>
#include <age/script/Nodes/EventNode.h>
#include <age/scene/SceneObjectDefinitionManager.h>

#include <age/script/BaseProperties.h>
#include <age/script/Contexts/ScriptUpdateContext.h>

#include <age/ImGui/ImGuiInterface.h>
#include <age/editor/CommandManager/CommandManager.h>
#include <age/stringRegistry/StringRegistry.h>

#include <imgui_node_editor/imgui_node_editor.h>
#include <age/editor/imgui_widgets/imgui_widgets.h>

namespace ed = ax::NodeEditor;

#include <IconFontHeaders/IconsLucide.h>

#include <sstream>
#include <fstream>
#include <filesystem>

using namespace Ag;

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

const uint8_t* Ag::GetScriptLinkColor(const ScriptPin& pin)
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

const uint8_t* Ag::GetScriptLinkHoverColor(const ScriptPin& pin)
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

const uint8_t* Ag::GetScriptLinkSelectedColor(const ScriptPin& pin)
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


Ag::ScriptGraphEditor::ScriptGraphEditor()
{
	myState.nodeEditorContext = CreateScriptEditorContext();
}

Ag::ScriptGraphEditor::~ScriptGraphEditor()
{
	if (myState.nodeEditorContext)
		ed::DestroyEditor(myState.nodeEditorContext);
}

// The node editor turns node, pin and link ids into the same kind of Dear ImGui id, so a node 1 and
// a pin 1 collide ("items with conflicting ID"). Each kind gets its own range, and an id of 0 stays
// valid (the library treats a plain 0 as "none").
static constexpr unsigned int kNodeIdSpace = 0x10000000u, kPinIdSpace = 0x20000000u, kLinkIdSpace = 0x30000000u, kEdIdMask = 0x0FFFFFFFu;
static ed::NodeId EdNode(unsigned int id) { return ed::NodeId(id | kNodeIdSpace); }
static ed::PinId EdPin(unsigned int id) { return ed::PinId(id | kPinIdSpace); }
static ed::LinkId EdLink(unsigned int id) { return ed::LinkId(id | kLinkIdSpace); }
static unsigned int FromEd(uintptr_t id) { return (unsigned int)id & kEdIdMask; }

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


namespace
{
	// ---- how a node looks: header colour by what it does, compact when it is only data ----

	struct NodeLook
	{
		ImU32 header;
		bool pure;   // no flow pins: a value, not an action
		bool event;  // starts a chain
	};

	NodeLook GetNodeLook(const Script& script, ScriptNodeId id, std::string_view title)
	{
		size_t inCount, outCount;
		const ScriptPinId* inPins = script.GetInputPins(id, inCount);
		const ScriptPinId* outPins = script.GetOutputPins(id, outCount);

		bool hasFlowInput = false;
		int flowOutputs = 0;
		for (size_t i = 0; i < inCount; i++)
			hasFlowInput |= script.GetPin(inPins[i]).type == ScriptLinkType::Flow;
		for (size_t i = 0; i < outCount; i++)
			flowOutputs += script.GetPin(outPins[i]).type == ScriptLinkType::Flow ? 1 : 0;

		NodeLook look = {};
		look.event = script.GetNode(id).ShouldExecuteAtStart() || title == "Trigger" || dynamic_cast<const EventNode*>(&script.GetNode(id)) != nullptr;
		look.pure = !hasFlowInput && flowOutputs == 0 && !look.event;

		const bool isVariable = (title.rfind("Read ", 0) == 0 || title.rfind("Write ", 0) == 0) && title.find("Property") != std::string_view::npos;
		if (look.event)
			look.header = IM_COL32(176, 40, 40, 255);
		else if (isVariable)
			look.header = IM_COL32(112, 72, 168, 255);
		else if (look.pure)
			look.header = IM_COL32(44, 128, 62, 255);
		else if (flowOutputs > 1 || title == "Branch" || title == "Delay")
			look.header = IM_COL32(98, 98, 110, 255);
		else
			look.header = IM_COL32(42, 102, 178, 255);
		return look;
	}

	// ---- compile: what is wrong with the script ----

	struct ScriptIssue
	{
		bool isError;
		ScriptNodeId node;
		std::string message;
	};

	bool IsPropertyNode(std::string_view title, bool& outIsWrite)
	{
		outIsWrite = title.rfind("Write ", 0) == 0;
		return (outIsWrite || title.rfind("Read ", 0) == 0) && title.find("Property") != std::string_view::npos;
	}

	void ValidateScript(const Script& script, const SceneObjectDefinition* definition, std::vector<ScriptIssue>& issues)
	{
		for (ScriptNodeId id = script.GetFirstNodeId(); id.id != ScriptNodeId::InvalidId; id = script.GetNextNodeId(id))
		{
			const std::string_view title = ScriptNodeTypeRegistry::GetNodeTypeShortName(script.GetType(id));
			if (title == "ERROR")
			{
				issues.push_back({ true, id, "Unknown node type: the script uses a node that no longer exists." });
				continue;
			}
			if (title == "Comment")
				continue;

			size_t inCount, outCount;
			const ScriptPinId* inPins = script.GetInputPins(id, inCount);
			const ScriptPinId* outPins = script.GetOutputPins(id, outCount);

			bool hasFlowInput = false, flowInputConnected = false, hasFlowOutput = false, flowOutputConnected = false;
			for (size_t i = 0; i < inCount; i++)
			{
				const ScriptPin& pin = script.GetPin(inPins[i]);
				if (pin.type != ScriptLinkType::Flow)
					continue;
				hasFlowInput = true;
				size_t links;
				script.GetConnectedLinks(inPins[i], links);
				flowInputConnected |= links > 0;
			}
			for (size_t i = 0; i < outCount; i++)
			{
				const ScriptPin& pin = script.GetPin(outPins[i]);
				if (pin.type != ScriptLinkType::Flow)
					continue;
				hasFlowOutput = true;
				size_t links;
				script.GetConnectedLinks(outPins[i], links);
				flowOutputConnected |= links > 0;
			}

			const bool isEvent = script.GetNode(id).ShouldExecuteAtStart() || title == "Trigger" || dynamic_cast<const EventNode*>(&script.GetNode(id)) != nullptr;
			if (hasFlowInput && !flowInputConnected && !isEvent)
				issues.push_back({ false, id, std::string(title) + ": never runs, nothing triggers it." });

			bool isWrite = false;
			if (!IsPropertyNode(title, isWrite))
				continue;
			for (size_t i = 0; i < inCount; i++)
			{
				const ScriptPin& pin = script.GetPin(inPins[i]);
				if (pin.name != "Name"_tgaid)
					continue;
				size_t links;
				script.GetConnectedLinks(inPins[i], links);
				if (links > 0)
					break; // the name comes from another node, nothing to check
				const Property& value = pin.overridenValue.HasValue() ? pin.overridenValue : pin.defaultValue;
				const StringId* name = value.Get<StringId>();
				if (!name || name->IsEmpty())
				{
					issues.push_back({ false, id, std::string(title) + ": no variable name set." });
					break;
				}
				if (!definition)
					break;
				const ScenePropertyDefinition* found = nullptr;
				for (const ScenePropertyDefinition& property : definition->GetProperties())
					if (property.name == *name)
						found = &property;
				if (!found)
					issues.push_back({ true, id, std::string(title) + ": the object has no variable '" + name->GetString() + "'." });
				else if (isWrite && (found->flags & ScenePropertyFlags::IsDynamic) == ScenePropertyFlags::None)
					issues.push_back({ false, id, std::string(title) + ": '" + name->GetString() + "' is not dynamic, so writing it does nothing." });
				break;
			}
		}

		for (ScriptLinkId id = script.GetFirstLinkId(); id.id != ScriptLinkId::InvalidId; id = script.GetNextLinkId(id))
		{
			const ScriptLink& link = script.GetLink(id);
			const ScriptPin& source = script.GetPin(link.sourcePinId);
			const ScriptPin& target = script.GetPin(link.targetPinId);
			if (source.type != target.type || source.dataType != target.dataType)
				issues.push_back({ true, target.node, "A wire joins pins of different types." });
		}
	}

	// ---- promote a pin to a variable ----

	const char* ReadNodeNameForType(const PropertyTypeBase* type)
	{
		if (!type)
			return nullptr;
		const std::string_view name = type->GetName().GetString();
		if (name == "Bool") return "Read Bool Property";
		if (name == "Int") return "Read Int Property";
		if (name == "Float") return "Read Float Property";
		if (name == "Float2") return "Read Float2 Property";
		if (name == "Float3") return "Read Float3 Property";
		if (name == "Float4") return "Read Float4 Property";
		if (name == "Color") return "Read Color Property";
		if (name == "StringId") return "Read String Property";
		return nullptr;
	}

	// Adds a dynamic variable named after the pin to the object, plus a Read node feeding the pin.
	bool PromotePinToVariable(Script& script, ScriptEditorSelection& selection, SceneObjectDefinition& definition, ScriptPinId pinId, float pinOffsetY)
	{
		const ScriptPin& pin = script.GetPin(pinId);
		const char* readName = ReadNodeNameForType(pin.dataType);
		if (pin.type != ScriptLinkType::Property || pin.role != ScriptPinRole::Input || !readName)
			return false;
		const ScriptNodeTypeId readType = ScriptNodeTypeRegistry::GetTypeId(readName);
		if (readType.id == ScriptNodeTypeId::InvalidId)
			return false;

		// "Velocity (cm/s)" becomes "Velocity"; unique among the object's variables.
		std::string base;
		for (const char c : std::string_view(pin.name.GetString()))
		{
			if (c == '(')
				break;
			if (std::isalnum((unsigned char)c) || c == '_')
				base += c;
		}
		if (base.empty())
			base = "Variable";
		std::string name = base;
		for (int suffix = 2;; ++suffix)
		{
			bool taken = false;
			for (const ScenePropertyDefinition& property : definition.GetProperties())
				taken |= std::string_view(property.name.GetString()) == name;
			if (!taken)
				break;
			name = base + std::to_string(suffix);
		}
		const StringId nameId = StringRegistry::RegisterOrGetString(name);

		ScenePropertyDefinition property = {};
		property.name = nameId;
		property.type = pin.dataType;
		property.value = pin.overridenValue.HasValue() ? pin.overridenValue : pin.defaultValue;
		property.flags = ScenePropertyFlags::IsDynamic | ScenePropertyFlags::IsPerInstance;
		property.groupName = StringRegistry::RegisterOrGetString("Variables");

		auto composite = std::make_shared<CompositeCommand>("Promote to Variable");
		composite->Do(std::make_shared<ChangePropertiesCommand>(definition, ChangePropertiesCommand::Action::Add, property, ScenePropertyDefinition{}));

		// The Read node goes to the left of the pin's node, level with the pin.
		const Vector2f nodePosition = script.GetPosition(pin.node);
		auto create = std::make_shared<CreateNodeCommand>(script, selection, readType, Vector2f{ nodePosition.x - 300.f, nodePosition.y + pinOffsetY - 24.f });
		composite->Do(create);
		const ScriptNodeId readNode = create->GetNodeId();

		size_t count;
		const ScriptPinId* inputs = script.GetInputPins(readNode, count);
		for (size_t i = 0; i < count; i++)
			if (script.GetPin(inputs[i]).name == "Name"_tgaid)
				composite->Do(std::make_shared<SetOverridenValueCommand>(script, selection, inputs[i], Property::Create<StringId>(nameId)));

		const ScriptPinId* outputs = script.GetOutputPins(readNode, count);
		if (count > 0)
			composite->Do(std::make_shared<CreateLinkCommand>(script, selection, ScriptLink{ outputs[0], pinId }));

		CommandManager::DoCommand(composite);
		return true;
	}

	// ---- creating nodes ----

	// Creates a node and, when it came from a dragged wire, connects it to that pin: one undo step.
	ScriptNodeId CreateNodeAndLink(Script& script, ScriptEditorSelection& selection, ScriptNodeTypeId type, Vector2f position, ScriptPinId linkPin, int catalogPinIndex)
	{
		auto composite = std::make_shared<CompositeCommand>("Create Node");
		auto create = std::make_shared<CreateNodeCommand>(script, selection, type, position);
		composite->Do(create);
		const ScriptNodeId newNode = create->GetNodeId();

		if (linkPin.id != ScriptPinId::InvalidId && catalogPinIndex >= 0)
		{
			for (const NodeCatalogEntry& entry : GetNodeCatalog())
			{
				if (entry.type != type || catalogPinIndex >= (int)entry.pins.size())
					continue;
				const NodeCatalogPin& catalogPin = entry.pins[catalogPinIndex];

				int ordinal = 0; // which input (or output) of the new node this is
				for (int i = 0; i < catalogPinIndex; i++)
					ordinal += entry.pins[i].isInput == catalogPin.isInput ? 1 : 0;

				size_t count;
				const ScriptPinId* pins = catalogPin.isInput ? script.GetInputPins(newNode, count) : script.GetOutputPins(newNode, count);
				if (ordinal < (int)count)
				{
					const ScriptLink link = catalogPin.isInput ? ScriptLink{ linkPin, pins[ordinal] } : ScriptLink{ pins[ordinal], linkPin };
					composite->Do(std::make_shared<CreateLinkCommand>(script, selection, link));
				}
				break;
			}
		}

		CommandManager::DoCommand(composite);
		return newNode;
	}

	// ---- delete, optionally keeping the flow connected ----

	void DoDelete(Script& script, ScriptEditorSelection& selection, const std::shared_ptr<DestroyNodeAndLinksCommand>& destroy, const std::vector<ScriptNodeId>& nodes, bool reconnect)
	{
		if (!reconnect || nodes.empty())
		{
			CommandManager::DoCommand(destroy);
			return;
		}

		auto isDeleted = [&](ScriptPinId pin)
		{
			const ScriptNodeId owner = script.GetPin(pin).node;
			for (ScriptNodeId node : nodes)
				if (node == owner)
					return true;
			return false;
		};

		// Whatever ran into a deleted node now runs into whatever it ran next.
		std::vector<ScriptLink> bridges;
		for (ScriptNodeId node : nodes)
		{
			size_t inCount, outCount;
			const ScriptPinId* inPins = script.GetInputPins(node, inCount);
			const ScriptPinId* outPins = script.GetOutputPins(node, outCount);

			std::vector<ScriptPinId> sources, targets;
			for (size_t i = 0; i < inCount; i++)
			{
				if (script.GetPin(inPins[i]).type != ScriptLinkType::Flow)
					continue;
				size_t links;
				const ScriptLinkId* ids = script.GetConnectedLinks(inPins[i], links);
				for (size_t l = 0; l < links; l++)
					sources.push_back(script.GetLink(ids[l]).sourcePinId);
			}
			for (size_t i = 0; i < outCount && targets.empty(); i++)
			{
				if (script.GetPin(outPins[i]).type != ScriptLinkType::Flow)
					continue;
				size_t links;
				const ScriptLinkId* ids = script.GetConnectedLinks(outPins[i], links);
				for (size_t l = 0; l < links; l++)
					targets.push_back(script.GetLink(ids[l]).targetPinId);
			}
			for (ScriptPinId source : sources)
				for (ScriptPinId target : targets)
					if (!isDeleted(source) && !isDeleted(target))
						bridges.push_back({ source, target });
		}

		auto composite = std::make_shared<CompositeCommand>("Delete Nodes");
		composite->Do(destroy);
		for (const ScriptLink& link : bridges)
			composite->Do(std::make_shared<CreateLinkCommand>(script, selection, link));
		CommandManager::DoCommand(composite);
	}

	// ---- copy / paste ----

	struct ClipboardNode
	{
		ScriptNodeTypeId type;
		Vector2f position;
		nlohmann::json custom;
		std::vector<std::pair<std::string, nlohmann::json>> inputValues; // input pin name, value
	};
	// Pins are matched by name, not position: a node that went through undo / redo can have its
	// pins in a different order.
	struct ClipboardLink
	{
		int sourceNode, targetNode;
		std::string sourcePin, targetPin;
	};
	std::vector<ClipboardNode> locClipboardNodes;
	std::vector<ClipboardLink> locClipboardLinks;

	ScriptPinId FindPinByName(const Script& script, ScriptNodeId node, bool output, const std::string& name)
	{
		size_t count;
		const ScriptPinId* pins = output ? script.GetOutputPins(node, count) : script.GetInputPins(node, count);
		for (size_t i = 0; i < count; i++)
			if (name == script.GetPin(pins[i]).name.GetString())
				return pins[i];
		return { ScriptPinId::InvalidId };
	}

	void CopyNodes(const Script& script, const std::vector<ScriptNodeId>& selected)
	{
		locClipboardNodes.clear();
		locClipboardLinks.clear();
		if (selected.empty())
			return;

		std::unordered_map<unsigned int, int> indexOf;
		for (ScriptNodeId id : selected)
		{
			ClipboardNode node;
			node.type = script.GetType(id);
			node.position = script.GetPosition(id);
			JsonData custom;
			script.GetNode(id).WriteToJson(custom);
			node.custom = custom.json;

			size_t count;
			const ScriptPinId* inputs = script.GetInputPins(id, count);
			for (size_t i = 0; i < count; i++)
			{
				const ScriptPin& pin = script.GetPin(inputs[i]);
				if (!pin.overridenValue.HasValue())
					continue;
				JsonData value;
				pin.overridenValue.WriteToJsonWithoutType(value);
				node.inputValues.emplace_back(pin.name.GetString(), value.json);
			}
			indexOf[id.id] = (int)locClipboardNodes.size();
			locClipboardNodes.push_back(std::move(node));
		}

		for (ScriptLinkId id = script.GetFirstLinkId(); id.id != ScriptLinkId::InvalidId; id = script.GetNextLinkId(id))
		{
			const ScriptLink& link = script.GetLink(id);
			const auto source = indexOf.find(script.GetPin(link.sourcePinId).node.id);
			const auto target = indexOf.find(script.GetPin(link.targetPinId).node.id);
			if (source == indexOf.end() || target == indexOf.end())
				continue;
			locClipboardLinks.push_back({ source->second, target->second, script.GetPin(link.sourcePinId).name.GetString(), script.GetPin(link.targetPinId).name.GetString() });
		}
	}

	std::vector<ScriptNodeId> PasteNodes(Script& script, ScriptEditorSelection& selection, Vector2f target)
	{
		std::vector<ScriptNodeId> created;
		if (locClipboardNodes.empty())
			return created;

		Vector2f origin = locClipboardNodes[0].position;
		for (const ClipboardNode& node : locClipboardNodes)
		{
			origin.x = std::min(origin.x, node.position.x);
			origin.y = std::min(origin.y, node.position.y);
		}

		auto composite = std::make_shared<CompositeCommand>("Paste Nodes");
		for (const ClipboardNode& node : locClipboardNodes)
		{
			auto create = std::make_shared<CreateNodeCommand>(script, selection, node.type,
				Vector2f{ node.position.x - origin.x + target.x, node.position.y - origin.y + target.y });
			composite->Do(create);
			const ScriptNodeId id = create->GetNodeId();
			created.push_back(id);

			// Some nodes (comments) keep their own data; load it, then let Init add any pins it implies.
			ScriptNodeBase& instance = script.EditNode(id);
			instance.LoadFromJson(JsonData{ node.custom });
			ScriptCreationContext context(script, id);
			instance.Init(context);

			for (const auto& value : node.inputValues)
			{
				const ScriptPinId pinId = FindPinByName(script, id, false, value.first);
				if (pinId.id == ScriptPinId::InvalidId)
					continue;
				const ScriptPin& pin = script.GetPin(pinId);
				if (pin.dataType)
					composite->Do(std::make_shared<SetOverridenValueCommand>(script, selection, pinId, Property::CreateFromJson(pin.dataType, JsonData{ value.second })));
			}
		}

		for (const ClipboardLink& link : locClipboardLinks)
		{
			const ScriptPinId source = FindPinByName(script, created[link.sourceNode], true, link.sourcePin);
			const ScriptPinId targetPin = FindPinByName(script, created[link.targetNode], false, link.targetPin);
			if (source.id != ScriptPinId::InvalidId && targetPin.id != ScriptPinId::InvalidId)
				composite->Do(std::make_shared<CreateLinkCommand>(script, selection, ScriptLink{ source, targetPin }));
		}

		CommandManager::DoCommand(composite);
		return created;
	}

	// ---- straighten (Q): move the selected nodes up or down so linked pins line up ----

	void StraightenNodes(Script& script, ScriptEditorSelection& selection, const std::unordered_map<unsigned int, float>& pinY, const std::vector<ScriptNodeId>& selected)
	{
		if (selected.empty())
			return;

		std::unordered_map<unsigned int, Vector2f> position;
		std::unordered_map<unsigned int, bool> isSelected;
		for (ScriptNodeId id : selected)
		{
			position[id.id] = script.GetPosition(id);
			isSelected[id.id] = true;
		}
		std::vector<ScriptNodeId> order = selected;
		std::sort(order.begin(), order.end(), [&](ScriptNodeId a, ScriptNodeId b) { return position[a.id].x < position[b.id].x; });

		// Pin height relative to its node, measured while the node sat at its script position.
		auto offsetOf = [&](ScriptPinId pin, bool& ok)
		{
			const auto it = pinY.find(pin.id);
			ok = it != pinY.end();
			return ok ? it->second - script.GetPosition(script.GetPin(pin).node).y : 0.f;
		};
		auto currentPosition = [&](ScriptNodeId id) { return position.count(id.id) ? position[id.id] : script.GetPosition(id); };

		std::unordered_map<unsigned int, bool> placed;
		bool anyMoved = false;
		for (ScriptNodeId node : order)
		{
			bool done = false;
			for (int pass = 0; pass < 2 && !done; ++pass) // wires of flow first, then data
			{
				for (int direction = 0; direction < 2 && !done; ++direction)
				{
					size_t count;
					const ScriptPinId* pins = direction == 0 ? script.GetInputPins(node, count) : script.GetOutputPins(node, count);
					for (size_t p = 0; p < count && !done; ++p)
					{
						const ScriptPin& pin = script.GetPin(pins[p]);
						if ((pass == 0) != (pin.type == ScriptLinkType::Flow))
							continue;
						size_t linkCount;
						const ScriptLinkId* links = script.GetConnectedLinks(pins[p], linkCount);
						for (size_t l = 0; l < linkCount && !done; ++l)
						{
							const ScriptLink& link = script.GetLink(links[l]);
							const ScriptPinId otherPin = direction == 0 ? link.sourcePinId : link.targetPinId;
							const ScriptNodeId other = script.GetPin(otherPin).node;
							if (other == node)
								continue;
							if (isSelected.count(other.id) && !placed.count(other.id))
								continue; // not settled yet, wait for it

							bool okThis, okOther;
							const float thisOffset = offsetOf(pins[p], okThis), otherOffset = offsetOf(otherPin, okOther);
							if (!okThis || !okOther)
								continue;
							const float thisY = currentPosition(node).y + thisOffset;
							const float otherY = currentPosition(other).y + otherOffset;
							if (std::abs(otherY - thisY) > 0.01f)
							{
								position[node.id].y += otherY - thisY;
								anyMoved = true;
							}
							done = true;
						}
					}
				}
			}
			placed[node.id] = true;
		}

		if (!anyMoved)
			return;
		auto move = std::make_shared<MoveNodesCommand>(script, selection);
		CommandManager::DoCommand(move);
		for (ScriptNodeId id : selected)
		{
			const Vector2f oldPosition = script.GetPosition(id);
			const Vector2f newPosition = position[id.id];
			if (std::abs(oldPosition.y - newPosition.y) > 0.01f)
			{
				script.SetPosition(id, newPosition);
				move->SetPosition(id, oldPosition, newPosition);
			}
		}
	}

	// The comment box is part of the script (it saves with it) but not something the runtime sees.
	void MarkScriptChanged(Script& script, ScriptNodeId id)
	{
		script.SetPosition(id, script.GetPosition(id));
	}

	// Extra room a comment's node has around the size stored for it (title and padding); measured
	// while the box is not being resized, so a resize can be turned back into a stored size.
	std::unordered_map<unsigned int, ImVec2> locCommentOverhead;
}

void Ag::ScriptGraphEditor::Display(Script& script, SceneObjectDefinition* definition, ScriptPinId& aPinToTrigger, bool aIsRunning)
{
	State& activeScript = myState;

	ed::SetCurrentEditor(activeScript.nodeEditorContext);

	// Compile: checks the script every frame and lists what is wrong; click a row to jump to it.
	{
		std::vector<ScriptIssue> issues;
		ValidateScript(script, definition, issues);
		int errors = 0, warnings = 0;
		for (const ScriptIssue& issue : issues)
			(issue.isError ? errors : warnings)++;

		if (ImGui::Button("Compile"))
			activeScript.showIssues = true;
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Checks the script for problems. Runs on every change; this opens the list.");
		ImGui::SameLine();
		if (issues.empty())
		{
			ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.f), ICON_LC_CIRCLE_CHECK " No problems");
		}
		else
		{
			char label[96];
			sprintf_s(label, "%s %d error%s, %d warning%s", errors > 0 ? ICON_LC_CIRCLE_ALERT : ICON_LC_TRIANGLE_ALERT,
				errors, errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s");
			ImGui::PushStyleColor(ImGuiCol_Text, errors > 0 ? ImVec4(0.95f, 0.4f, 0.4f, 1.f) : ImVec4(0.95f, 0.75f, 0.3f, 1.f));
			if (ImGui::SmallButton(label))
				activeScript.showIssues = !activeScript.showIssues;
			ImGui::PopStyleColor();
		}

		if (activeScript.showIssues && !issues.empty())
		{
			const float listHeight = std::min(150.f, 8.f + 22.f * (float)issues.size());
			if (ImGui::BeginChild("##issues", ImVec2(0.f, listHeight), ImGuiChildFlags_Borders))
			{
				for (size_t i = 0; i < issues.size(); i++)
				{
					ImGui::PushID((int)i);
					ImGui::PushStyleColor(ImGuiCol_Text, issues[i].isError ? ImVec4(0.95f, 0.5f, 0.5f, 1.f) : ImVec4(0.95f, 0.8f, 0.4f, 1.f));
					if (ImGui::Selectable(issues[i].message.c_str()))
						activeScript.pendingFocusNode = issues[i].node;
					ImGui::PopStyleColor();
					ImGui::PopID();
				}
			}
			ImGui::EndChild();
		}
		else if (issues.empty())
		{
			activeScript.showIssues = false;
		}
	}
	activeScript.pinCanvasY.clear();

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
		const Vector2f pos = script.GetPosition(currentNodeId);
		const auto known = activeScript.lastKnownNodePos.find(currentNodeId.id);
		if (known == activeScript.lastKnownNodePos.end() || known->second.first != pos.x || known->second.second != pos.y)
		{
			ed::SetNodePosition(EdNode(currentNodeId.id), ImVec2(pos.x, pos.y));
			activeScript.lastKnownNodePos[currentNodeId.id] = { pos.x, pos.y };
		}
	}

	ed::Begin("ScriptGraph");

	if (activeScript.pendingFocusNode.id != ScriptNodeId::InvalidId)
	{
		ed::ClearSelection();
		ed::SelectNode(EdNode(activeScript.pendingFocusNode.id));
		ed::NavigateToSelection();
		activeScript.pendingFocusNode = { ScriptNodeId::InvalidId };
	}
	ScriptNodeId openCommentEditFor = { ScriptNodeId::InvalidId };

	for (ScriptNodeId currentNodeId = script.GetFirstNodeId(); currentNodeId.id != ScriptNodeId::InvalidId; currentNodeId = script.GetNextNodeId(currentNodeId))
	{
		if (CommentNode* comment = dynamic_cast<CommentNode*>(&script.EditNode(currentNodeId)))
		{
			// A comment box: drawn behind the nodes it covers, resizable, no pins.
			ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(255, 255, 255, 26));
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(255, 255, 255, 96));
			ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(10.f, 8.f, 10.f, 8.f));
			ed::BeginNode(EdNode(currentNodeId.id));
			ImGui::PushID((int)currentNodeId.id);
			ImGui::TextUnformatted(comment->text.c_str());
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
				openCommentEditFor = currentNodeId;
			ed::Group(ImVec2(comment->width, comment->height));
			ImGui::PopID();
			ed::EndNode();
			ed::PopStyleVar();
			ed::PopStyleColor(2);
			continue;
		}

		const std::string_view titleString = ScriptNodeTypeRegistry::GetNodeTypeShortName(script.GetType(currentNodeId));
		const NodeLook look = GetNodeLook(script, currentNodeId, titleString);
		// Data-only nodes are drawn smaller, like Blueprint's pure nodes.
		const ImVec4 nodePadding = look.pure ? ImVec4(10.f, 6.f, 10.f, 8.f) : ImVec4(14.f, 10.f, 14.f, 12.f);

		ed::PushStyleVar(ed::StyleVar_NodePadding, nodePadding);
		ed::BeginNode(EdNode(currentNodeId.id));

		bool isNodeHighlighted = ed::IsNodeSelected(EdNode(currentNodeId.id)) || activeScript.hoveredNode == currentNodeId;

		const float nodeLeftScreenX = ImGui::GetCursorScreenPos().x;
		float contentWidth = 0.f;

		char titleText[160];
		sprintf_s(titleText, look.event ? ICON_LC_ZAP "  %s" : "%s", titleString.data());
		ImGui::TextUnformatted(titleText);
		contentWidth = std::max(contentWidth, ImGui::GetItemRectMax().x - nodeLeftScreenX);
		ImGui::Dummy(ImVec2(0.f, look.pure ? 2.f : 4.f));

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
			const bool isPinHovered = ed::GetHoveredPin() == EdPin(pinId.id);
			const ImU32 iconColor = isPinHovered ? ColorU32(linkHoverColor) : isNodeHighlighted ? ColorU32(linkSelectedColor) : ColorU32(linkColor);
			size_t connectionCount;
			script.GetConnectedLinks(pinId, connectionCount);

			ed::BeginPin(EdPin(pinId.id), ed::PinKind::Input);
			NodeEditorPinIcon(iconColor, connectionCount > 0);
			const ImVec2 iconMin = ImGui::GetItemRectMin();
			const ImVec2 iconMax = ImGui::GetItemRectMax();
			const ImVec2 iconCenter((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f);
			activeScript.pinCanvasY[pinId.id] = iconCenter.y;
			ImGui::SameLine();
			ImGui::TextUnformatted(pinName.data());
			const ImVec2 labelMax = ImGui::GetItemRectMax();

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

			// Grab area: the icon plus its label and some margin, so a wire can be started or dropped
			// on the pin without aiming at the small circle. Links still attach at the circle.
			ed::PinRect(ImVec2(iconMin.x - 16.f, iconMin.y - 6.f), ImVec2(labelMax.x + 8.f, iconMax.y + 6.f));
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
			const bool isPinHovered = ed::GetHoveredPin() == EdPin(pinId.id);
			const ImU32 iconColor = isPinHovered ? ColorU32(linkHoverColor) : isNodeHighlighted ? ColorU32(linkSelectedColor) : ColorU32(linkColor);
			size_t connectionCount;
			script.GetConnectedLinks(pinId, connectionCount);

			ImGui::SetCursorPos(rowPos);
			ed::BeginPin(EdPin(pinId.id), ed::PinKind::Output);
			ImGui::SetCursorPosX(rowPos.x + (widthRight - ImGui::CalcTextSize(pinName.data()).x));
			ImGui::TextUnformatted(pinName.data());
			const ImVec2 labelMin = ImGui::GetItemRectMin();
			ImGui::SameLine();
			NodeEditorPinIcon(iconColor, connectionCount > 0);
			const ImVec2 iconMin = ImGui::GetItemRectMin();
			const ImVec2 iconMax = ImGui::GetItemRectMax();
			const ImVec2 iconCenter((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f);
			activeScript.pinCanvasY[pinId.id] = iconCenter.y;
			ed::PinRect(ImVec2(labelMin.x - 8.f, iconMin.y - 6.f), ImVec2(iconMax.x + 16.f, iconMax.y + 6.f));
			ed::PinPivotRect(iconCenter, iconCenter);
			ed::EndPin();
			rowPos.y = ImGui::GetCursorPosY();
		}

		ImGui::SetCursorPos(ImVec2(pinRowsStart.x, std::max(inputColumnEndY, rowPos.y)));
		ed::EndNode();
		ed::PopStyleVar();

		// Header colour by kind (event / action / data / flow control / variable), drawn on the
		// node's own background layer so it sits under the text.
		{
			const ed::NodeId nodeId = EdNode(currentNodeId.id);
			ImDrawList* background = ed::GetNodeBackgroundDrawList(nodeId);
			const ImVec2 nodePosition = ed::GetNodePosition(nodeId);
			const ImVec2 nodeSize = ed::GetNodeSize(nodeId);
			if (background && nodeSize.x > 0.f)
			{
				const float headerHeight = nodePadding.y + ImGui::GetTextLineHeight() + (look.pure ? 4.f : 6.f);
				background->AddRectFilled(nodePosition, ImVec2(nodePosition.x + nodeSize.x, nodePosition.y + headerHeight),
					look.header, ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop);
			}
		}
	}

	// Select nodes that were just created. This has to come after they are drawn: the node editor
	// only knows a node once it has been drawn, and selecting an unknown one crashes it.
	if (!activeScript.pendingSelect.empty())
	{
		ed::ClearSelection();
		for (ScriptNodeId id : activeScript.pendingSelect)
			if (script.Exists(id))
				ed::SelectNode(EdNode(id.id), true);
		activeScript.pendingSelect.clear();
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
		ed::Link(EdLink(linkId.id), EdPin(link.sourcePinId.id), EdPin(link.targetPinId.id), ColorVec4(GetScriptLinkColor(pin)), 2.0f);
	}

	// Link creation is a multi-frame gesture (drag from a pin, hover a
	// target, release) -- BeginCreate()/QueryNewLink() reports it every
	// frame it's in progress, which doubles as this frame's answer to "is a
	// link being dragged from this pin right now" (activeScript.
	// inProgressLinkPin, used above to color that pin while it's the source
	// of a drag -- imnodes' IsLinkStarted() query this replaces was a
	// separate one-shot call after EndNodeEditor()).
	activeScript.inProgressLinkPin = { ScriptPinId::InvalidId };
	ScriptPinId openDragCreatePin = { ScriptPinId::InvalidId };
	if (ed::BeginCreate())
	{
		ed::PinId startPinId, endPinId;
		if (ed::QueryNewLink(&startPinId, &endPinId))
		{
			const ScriptPinId sourcePinId = { FromEd(startPinId.Get()) };
			activeScript.inProgressLinkPin = sourcePinId;
			if (endPinId)
			{
				const ScriptPinId targetPinId = { FromEd(endPinId.Get()) };
				const ScriptPin& sourcePin = script.GetPin(sourcePinId);
				const ScriptPin& targetPin = script.GetPin(targetPinId);
				const bool compatible = sourcePin.type == targetPin.type && sourcePin.dataType == targetPin.dataType && sourcePin.type != ScriptLinkType::Unknown;
				const ImVec4 previewColor = ColorVec4(compatible ? GetScriptLinkColor(sourcePin) : GetScriptLinkColor(ScriptPin{ .type = ScriptLinkType::Unknown }));
				if (compatible && ed::AcceptNewItem(previewColor, 2.0f))
				{
					std::shared_ptr<CreateLinkCommand> command = std::make_shared<CreateLinkCommand>(script, activeScript.selection, ScriptLink{ sourcePinId, targetPinId });
					Ag::CommandManager::DoCommand(command);
				}
				else if (!compatible)
				{
					ed::RejectNewItem(previewColor, 2.0f);
				}
			}
		}
		else
		{
			// A wire let go over empty space: offer the nodes that fit that pin, and connect the pick.
			ed::PinId dragPinId;
			if (ed::QueryNewNode(&dragPinId))
			{
				activeScript.inProgressLinkPin = ScriptPinId{ FromEd(dragPinId.Get()) };
				if (ed::AcceptNewItem())
					openDragCreatePin = ScriptPinId{ FromEd(dragPinId.Get()) };
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
		std::vector<ScriptNodeId> deletedNodes;
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
					ensureCommand()->Add(ScriptLinkId{ FromEd(deletedLinkId.Get()) });
			}
			ed::NodeId deletedNodeId;
			while (ed::QueryDeletedNode(&deletedNodeId))
			{
				if (ed::AcceptDeletedItem())
				{
					ensureCommand()->Add(ScriptNodeId{ FromEd(deletedNodeId.Get()) });
					deletedNodes.push_back(ScriptNodeId{ FromEd(deletedNodeId.Get()) });
				}
			}
		}
		ed::EndDelete();
		// Shift + Delete removes the node and joins what ran into it to what it ran next.
		if (command)
			DoDelete(script, activeScript.selection, command, deletedNodes, ImGui::GetIO().KeyShift);
	}

	activeScript.hoveredNode = ed::GetHoveredNode() ? ScriptNodeId{ FromEd(ed::GetHoveredNode().Get()) } : ScriptNodeId{ ScriptNodeId::InvalidId };

	// Persist wherever the user actually dragged each node to -- safe to run
	// unconditionally here (unlike MaterialDocument's equivalent loop, which
	// has to skip nodes it hasn't drawn yet this session) because any node
	// created this frame is added further down, after this loop runs.
	for (ScriptNodeId currentNodeId = script.GetFirstNodeId(); currentNodeId.id != ScriptNodeId::InvalidId; currentNodeId = script.GetNextNodeId(currentNodeId))
	{
		Vector2f oldPos = script.GetPosition(currentNodeId);
		ImVec2 newPos = ed::GetNodePosition(EdNode(currentNodeId.id));

		if (newPos.x != oldPos.x || newPos.y != oldPos.y)
		{
			if (activeScript.inProgressMove == nullptr)
			{
				activeScript.inProgressMove = std::make_shared<MoveNodesCommand>(script, activeScript.selection);
				Ag::CommandManager::DoCommand(activeScript.inProgressMove);
			}

			script.SetPosition(currentNodeId, { newPos.x, newPos.y });
			activeScript.inProgressMove->SetPosition(currentNodeId, oldPos, { newPos.x, newPos.y });
		}
		activeScript.lastKnownNodePos[currentNodeId.id] = { newPos.x, newPos.y };
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
			for (ed::LinkId id : selectedLinkIds) command->Add(ScriptLinkId{ FromEd(id.Get()) });
			std::vector<ScriptNodeId> nodes;
			for (ed::NodeId id : selectedNodeIds)
			{
				command->Add(ScriptNodeId{ FromEd(id.Get()) });
				nodes.push_back(ScriptNodeId{ FromEd(id.Get()) });
			}
			DoDelete(script, activeScript.selection, command, nodes, ImGui::GetIO().KeyShift);
		}
	}

	// Comment boxes: keep their stored size in step with the size the user drags them to.
	{
		const bool interacting = ImGui::IsMouseDown(0) || ImGui::IsMouseReleased(0);
		for (ScriptNodeId id = script.GetFirstNodeId(); id.id != ScriptNodeId::InvalidId; id = script.GetNextNodeId(id))
		{
			CommentNode* comment = dynamic_cast<CommentNode*>(&script.EditNode(id));
			if (!comment)
				continue;
			const ImVec2 nodeSize = ed::GetNodeSize(EdNode(id.id));
			if (nodeSize.x <= 0.f)
				continue;
			if (!interacting)
			{
				locCommentOverhead[id.id] = ImVec2(nodeSize.x - comment->width, nodeSize.y - comment->height);
			}
			else if (locCommentOverhead.count(id.id))
			{
				const ImVec2 overhead = locCommentOverhead[id.id];
				const float width = std::max(80.f, nodeSize.x - overhead.x), height = std::max(50.f, nodeSize.y - overhead.y);
				if (std::abs(width - comment->width) > 0.5f || std::abs(height - comment->height) > 0.5f)
				{
					comment->width = width;
					comment->height = height;
					MarkScriptChanged(script, id);
				}
			}
		}
	}

	// Shortcuts (Blueprint's): Ctrl+C/V/D copy, paste, duplicate; Q straighten; C comment box;
	// hold B/D/S/T/P and click empty space to create Branch/Delay/Sequence/Update/Start there.
	struct ShortcutRequest
	{
		bool paste = false, duplicate = false, comment = false;
		ScriptNodeTypeId quickCreate = { ScriptNodeTypeId::InvalidId };
		ImVec2 mouseScreen = {};
		ImVec2 selectionMin = {}, selectionMax = {};
		std::vector<ScriptNodeId> selected;
	} shortcut;
	{
		const ImGuiIO& io = ImGui::GetIO();
		const bool allowed = !io.WantTextInput && !ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		const int selectedCount = ed::GetSelectedObjectCount();
		std::vector<ed::NodeId> selectedIds(selectedCount);
		selectedIds.resize(ed::GetSelectedNodes(selectedIds.data(), selectedCount));
		for (ed::NodeId id : selectedIds)
			shortcut.selected.push_back(ScriptNodeId{ FromEd(id.Get()) });

		shortcut.mouseScreen = ImGui::GetMousePos();
		if (allowed)
		{
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
				CopyNodes(script, shortcut.selected);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
				shortcut.paste = true;
			if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_D, false) || ImGui::IsKeyPressed(ImGuiKey_W, false)))
			{
				CopyNodes(script, shortcut.selected);
				shortcut.duplicate = true;
			}
			// F frames the selection, Home frames the whole graph (Unreal's Blueprint editor).
			if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F, false) && !shortcut.selected.empty())
				ed::NavigateToSelection(false, 0.25f);
			if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
				ed::NavigateToContent(0.25f);
			if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Q, false))
				StraightenNodes(script, activeScript.selection, activeScript.pinCanvasY, shortcut.selected);
			if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_C, false) && !shortcut.selected.empty())
			{
				shortcut.comment = true;
				shortcut.selectionMin = ImVec2(FLT_MAX, FLT_MAX);
				shortcut.selectionMax = ImVec2(-FLT_MAX, -FLT_MAX);
				for (ed::NodeId id : selectedIds)
				{
					const ImVec2 position = ed::GetNodePosition(id), size = ed::GetNodeSize(id);
					shortcut.selectionMin = ImVec2(std::min(shortcut.selectionMin.x, position.x), std::min(shortcut.selectionMin.y, position.y));
					shortcut.selectionMax = ImVec2(std::max(shortcut.selectionMax.x, position.x + size.x), std::max(shortcut.selectionMax.y, position.y + size.y));
				}
			}

			if (!io.KeyCtrl && ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered()
				&& !ed::GetHoveredNode() && !ed::GetHoveredPin() && !ed::GetHoveredLink())
			{
				static const struct { ImGuiKey key; const char* type; } kQuickCreate[] = {
					{ ImGuiKey_B, "Branch" }, { ImGuiKey_D, "Delay" }, { ImGuiKey_S, "Sequence" }, { ImGuiKey_T, "Update" }, { ImGuiKey_P, "Start" },
				};
				for (const auto& entry : kQuickCreate)
					if (ImGui::IsKeyDown(entry.key))
						shortcut.quickCreate = ScriptNodeTypeRegistry::GetTypeId(entry.type);
			}
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
	ed::NodeId contextNodeId;
	if (ed::ShowPinContextMenu(&contextPinId))
	{
		attribute.id = (int)FromEd(contextPinId.Get());
		attribute.type = decltype(attribute)::Type::Pin;
		const ScriptPinId pinid{ .id = (unsigned int)attribute.id };
		if (aIsRunning && script.GetPin(pinid).type == ScriptLinkType::Flow)
		{
			ImGui::OpenPopup("Trigger Node");
		}
		else
		{
			activeScript.contextPin = pinid;
			ImGui::OpenPopup("Pin Menu");
		}
	}
	else if (ed::ShowNodeContextMenu(&contextNodeId))
	{
		if (dynamic_cast<CommentNode*>(&script.EditNode(ScriptNodeId{ FromEd(contextNodeId.Get()) })))
			openCommentEditFor = ScriptNodeId{ FromEd(contextNodeId.Get()) };
	}
	else if (ed::ShowLinkContextMenu(&contextLinkId))
	{
		if (aIsRunning)
		{
			attribute.id = (int)FromEd(contextLinkId.Get());
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
		activeScript.searchJustOpened = true;
	}

	if (openDragCreatePin.id != ScriptPinId::InvalidId)
	{
		activeScript.dragPin = openDragCreatePin;
		ImGui::OpenPopup("Create Node From Pin");
		activeScript.searchJustOpened = true;
	}
	if (openCommentEditFor.id != ScriptNodeId::InvalidId)
	{
		activeScript.editingComment = openCommentEditFor;
		ImGui::OpenPopup("Edit Comment");
	}
	ScriptPinId promoteRequest = { ScriptPinId::InvalidId };
	ScriptPinId dragCreateLinkPin = { ScriptPinId::InvalidId };
	int dragCreatePinIndex = -1;

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
	if (ImGui::BeginPopup("Pin Menu"))
	{
		const ScriptPinId pinId = activeScript.contextPin;
		if (script.Exists(pinId))
		{
			const ScriptPin& pin = script.GetPin(pinId);
			size_t linkCount;
			const ScriptLinkId* links = script.GetConnectedLinks(pinId, linkCount);

			const bool canPromote = pin.type == ScriptLinkType::Property && pin.role == ScriptPinRole::Input && linkCount == 0
				&& definition && ReadNodeNameForType(pin.dataType);
			ImGui::BeginDisabled(!canPromote);
			if (ImGui::Selectable("Promote to Variable"))
				promoteRequest = pinId;
			ImGui::EndDisabled();
			if (!canPromote && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Needs an unconnected data input on a script that belongs to an object.");

			ImGui::BeginDisabled(linkCount == 0);
			if (ImGui::Selectable("Break Links"))
			{
				std::shared_ptr<DestroyNodeAndLinksCommand> command = std::make_shared<DestroyNodeAndLinksCommand>(script, activeScript.selection);
				for (size_t i = 0; i < linkCount; i++)
					command->Add(links[i]);
				Ag::CommandManager::DoCommand(command);
			}
			ImGui::EndDisabled();
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("Node Type Selection"))
	{
		int pinIndex = -1;
		bool showTree = false;
		ScriptNodeTypeId typeToCreate = DrawNodeSearch(activeScript.searchJustOpened, NodePinFilter{}, pinIndex, showTree);
		activeScript.searchJustOpened = false;
		if (showTree)
			typeToCreate = ShowNodeTypeSelectorForCategory(ScriptNodeTypeRegistry::GetRootCategory());

		if (typeToCreate.id != ScriptNodeTypeId::InvalidId)
		{
			hasPendingCreate = true;
			pendingCreateType = typeToCreate;
			pendingCreateScreenPos = ImGui::GetMousePosOnOpeningCurrentPopup();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("Create Node From Pin"))
	{
		if (script.Exists(activeScript.dragPin))
		{
			const ScriptPin& pin = script.GetPin(activeScript.dragPin);
			NodePinFilter filter;
			filter.active = true;
			filter.wantInput = pin.role == ScriptPinRole::Output; // an output feeds the new node's input
			filter.linkType = pin.type;
			filter.dataType = pin.dataType;

			int pinIndex = -1;
			bool showTree = false;
			const ScriptNodeTypeId typeToCreate = DrawNodeSearch(activeScript.searchJustOpened, filter, pinIndex, showTree);
			activeScript.searchJustOpened = false;
			if (typeToCreate.id != ScriptNodeTypeId::InvalidId)
			{
				hasPendingCreate = true;
				pendingCreateType = typeToCreate;
				pendingCreateScreenPos = ImGui::GetMousePosOnOpeningCurrentPopup();
				dragCreateLinkPin = activeScript.dragPin;
				dragCreatePinIndex = pinIndex;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("Edit Comment"))
	{
		CommentNode* comment = script.Exists(activeScript.editingComment) ? dynamic_cast<CommentNode*>(&script.EditNode(activeScript.editingComment)) : nullptr;
		if (comment)
		{
			static char buffer[256];
			if (ImGui::IsWindowAppearing())
			{
				strncpy_s(buffer, comment->text.c_str(), _TRUNCATE);
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::SetNextItemWidth(300.f);
			if (ImGui::InputText("##commenttext", buffer, sizeof(buffer), ImGuiInputTextFlags_AutoSelectAll))
			{
				comment->text = buffer;
				MarkScriptChanged(script, activeScript.editingComment);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape))
				ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	if (promoteRequest.id != ScriptPinId::InvalidId && definition)
	{
		const auto pinY = activeScript.pinCanvasY.find(promoteRequest.id);
		const float offsetY = pinY != activeScript.pinCanvasY.end() ? pinY->second - script.GetPosition(script.GetPin(promoteRequest).node).y : 30.f;
		PromotePinToVariable(script, activeScript.selection, *definition, promoteRequest, offsetY);
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
		const ScriptNodeId created = CreateNodeAndLink(script, activeScript.selection, pendingCreateType, Vector2f{ clickPos.x, clickPos.y }, dragCreateLinkPin, dragCreatePinIndex);
		activeScript.pendingSelect = { created };
	}

	// Shortcut results that need canvas positions.
	{
		ed::SetCurrentEditor(activeScript.nodeEditorContext);
		const ImVec2 mouse = ed::ScreenToCanvas(shortcut.mouseScreen);

		if (shortcut.quickCreate.id != ScriptNodeTypeId::InvalidId)
			activeScript.pendingSelect = { CreateNodeAndLink(script, activeScript.selection, shortcut.quickCreate, Vector2f{ mouse.x, mouse.y }, { ScriptPinId::InvalidId }, -1) };

		if (shortcut.paste || shortcut.duplicate)
		{
			// Paste lands at the cursor; duplicate a little down and right of the original.
			Vector2f target = { mouse.x, mouse.y };
			if (shortcut.duplicate && !locClipboardNodes.empty())
			{
				Vector2f origin = locClipboardNodes[0].position;
				for (const ClipboardNode& node : locClipboardNodes)
				{
					origin.x = std::min(origin.x, node.position.x);
					origin.y = std::min(origin.y, node.position.y);
				}
				target = { origin.x + 40.f, origin.y + 40.f };
			}
			const std::vector<ScriptNodeId> pasted = PasteNodes(script, activeScript.selection, target);
			if (!pasted.empty())
				activeScript.pendingSelect = pasted;
		}

		if (shortcut.comment)
		{
			auto composite = std::make_shared<CompositeCommand>("Add Comment");
			auto create = std::make_shared<CreateNodeCommand>(script, activeScript.selection, ScriptNodeTypeRegistry::GetTypeId("Comment"),
				Vector2f{ shortcut.selectionMin.x - 30.f, shortcut.selectionMin.y - 64.f });
			composite->Do(create);
			if (CommentNode* comment = dynamic_cast<CommentNode*>(&script.EditNode(create->GetNodeId())))
			{
				comment->width = shortcut.selectionMax.x - shortcut.selectionMin.x + 60.f;
				comment->height = shortcut.selectionMax.y - shortcut.selectionMin.y + 100.f;
			}
			Ag::CommandManager::DoCommand(composite);
			activeScript.editingComment = create->GetNodeId();
			activeScript.pendingSelect = { create->GetNodeId() };
		}
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("property_payload"))
		{
			struct Payload { PropertyTypeId type; StringId name; } data;
			memcpy(&data, (const uint8_t*)payload->Data, payload->DataSize);

			ed::SetCurrentEditor(activeScript.nodeEditorContext);
			const ImVec2 dropPos = ed::ScreenToCanvas(ImGui::GetMousePos());

			// Dropping a variable makes a Get node; with Alt held, a Set node.
			const char* accessKind = ImGui::GetIO().KeyAlt ? "Write" : "Read";
			ScriptNodeTypeId typeToCreate;

			// todo: Perhaps this mapping could be more automatic?
			if (data.type == PropertyTypeRegistry::GetPropertyType("Color"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId(std::string(accessKind) + " Color Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Bool"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId(std::string(accessKind) + " Bool Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Int"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId(std::string(accessKind) + " Int Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Float4"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId(std::string(accessKind) + " Float4 Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Float"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId(std::string(accessKind) + " Float Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Animation Clip"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId(std::string(accessKind) + " Animation Clip Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Float2"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId(std::string(accessKind) + " Float2 Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("Float3"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId(std::string(accessKind) + " Float3 Property"); }
			if (data.type == PropertyTypeRegistry::GetPropertyType("StringId"_tgaid)->GetTypeId()) { typeToCreate = ScriptNodeTypeRegistry::GetTypeId(std::string(accessKind) + " String Property"); }

			if (typeToCreate.id != ScriptNodeTypeId::InvalidId)
			{
				std::shared_ptr<CreateNodeCommand> command = std::make_shared<CreateNodeCommand>(script, activeScript.selection, typeToCreate, Vector2f{ dropPos.x, dropPos.y });
				Ag::CommandManager::DoCommand(command);

				Ag::ScriptNodeId nodeid = script.GetLastNodeId();

				size_t cnt;
				const Ag::ScriptPinId* inPins = script.GetInputPins(nodeid, cnt);
				for (size_t i=0; i<cnt; i++)
				{
					Ag::ScriptPin pin = script.GetPin(inPins[i]);
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