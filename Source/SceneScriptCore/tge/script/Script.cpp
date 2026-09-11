#include <stdafx.h>

#include "Script.h"
#include "ScriptNodeTypeRegistry.h"
#include "JsonData.h"

#include <tge/stringRegistry/StringRegistry.h>

#include <sstream>
using namespace Tga;

Script::Script() = default;
Script::~Script() = default;

void Script::Clear()
{
	mySequenceNumber = 0;

	myNodes.clear();
	myFreeNodes.clear();

	myLinks.clear();
	myFreeLinks.clear();

	myPins.clear();
	myFreePins.clear();

	myInstanceToId.clear();
}

void Script::LoadFromJson(const JsonData& data)
{
	assert("Trying to load into a script that is not empty" && GetFirstNodeId().id == ScriptNodeId::InvalidId);
	assert("Trying to load into a script that is not empty" && GetFirstPinId().id == ScriptPinId::InvalidId);
	assert("Trying to load into a script that is not empty" && GetFirstLinkId().id == ScriptLinkId::InvalidId);

	using namespace nlohmann;

	const json& nodes = data.json["nodes"];
	const json& pins = data.json["pins"];
	const json& links = data.json["links"];

	for (json::const_iterator it = nodes.begin(); it != nodes.end(); ++it)
	{
		const json& nodeData = *it;
		ScriptNodeId id = { nodeData["id"].get<unsigned int>() };
		const json& posArray = nodeData["position"];
		Vector2 pos = { posArray[0].get<float>(), posArray[1].get<float>() };
		std::string typeName = nodeData["type"];
		std::string instanceName = nodeData["name"];
		const json& customNodeData = nodeData["customData"];

		ScriptNodeTypeId typeId = ScriptNodeTypeRegistry::GetTypeId(typeName);

		std::unique_ptr<ScriptNodeBase> nodePtr;
		if (typeId.id == ScriptNodeTypeId::InvalidId)
		{
			// todo save the name somehow also, so the old name isn't lost
			ScriptNodeTypeId errorNodeId = ScriptNodeTypeRegistry::GetTypeId("ERROR");
			assert("Error node could not be found, call RegisterCommonNodes() before loading scripts" && errorNodeId.id != ScriptNodeTypeId::InvalidId);

			typeId = errorNodeId;
		}

		CreateNodeWithReusedId(id, typeId, std::move(ScriptNodeTypeRegistry::CreateNode(typeId)), pos);
		ScriptNodeBase& node = EditNode(id);
		if (!instanceName.empty())
			SetName(id, StringRegistry::RegisterOrGetString(instanceName));

		node.LoadFromJson({ customNodeData });
	}

	for (json::const_iterator it = pins.begin(); it != pins.end(); ++it)
	{
		const json& pinData = *it;
		ScriptPinId id = { pinData["id"].get<unsigned int>() };
		ScriptNodeId nodeId = { pinData["node"].get<unsigned int>() };
		std::string name = pinData["name"];

		std::string typeString = pinData["type"];
		ScriptLinkType type = ScriptLinkType::Unknown;
		for (int i = 0; i < (int)ScriptLinkType::Count; i++)
		{
			if (typeString == ScriptLinkTypeNames[i])
			{
				type = (ScriptLinkType)i;
				break;
			}
		}

		const PropertyTypeBase* dataType = nullptr;
		if (pinData.contains("datatype") && pinData["datatype"].is_string())
		{
			std::string_view dataTypeString = pinData["datatype"];
			StringId dataTypeStringId = StringRegistry::RegisterOrGetString(dataTypeString);
			dataType = PropertyTypeRegistry::GetPropertyType(dataTypeStringId);
		}

		Property overridenValue = pinData.contains("value") && !pinData["value"].is_null() ? Property::CreateFromJson(dataType, {pinData["value"]}) : Property{};

		std::string roleString = pinData["role"];
		ScriptPinRole role;
		if (roleString == "Output")
			role = ScriptPinRole::Output;
		else
			role = ScriptPinRole::Input;

		CreatePinWithReusedId(id, { role, INT_MAX, nodeId, StringRegistry::RegisterOrGetString(name), type, dataType, Property{}, overridenValue });
	}

	for (json::const_iterator it = links.begin(); it != links.end(); ++it)
	{
		const json& linkData = *it;
		ScriptLinkId id = { linkData["id"].get<unsigned int>() };
		ScriptPinId sourcePin = { linkData["sourcePin"].get<unsigned int>() };
		ScriptPinId targetPin = { linkData["targetPin"].get<unsigned int>() };

		CreateLinkWithReusedId(id, { sourcePin, targetPin });
	}

	for (ScriptNodeId nodeId = GetFirstNodeId(); nodeId.id != ScriptNodeId::InvalidId; nodeId = GetNextNodeId(nodeId))
	{
		ScriptNodeBase& node = EditNode(nodeId);
		ScriptCreationContext context(*this, nodeId);
		node.Init(context);
	}
}
void Script::WriteToJson(JsonData& result)
{
	using namespace nlohmann;

	result.json["nodes"] = json::array();
	json& nodes = result.json["nodes"];

	for (ScriptNodeId nodeId = GetFirstNodeId(); nodeId.id != ScriptNodeId::InvalidId; nodeId = GetNextNodeId(nodeId))
	{
		const ScriptNodeBase& node = GetNode(nodeId);
		ScriptNodeTypeId type = GetType(nodeId);
		StringId name = GetName(nodeId);
		Tga::Vector2f pos = GetPosition(nodeId);

		JsonData nodeResult;
		node.WriteToJson(nodeResult);


		nodes.push_back(
			{
				{"id", nodeId.id},
				{"type", ScriptNodeTypeRegistry::GetNodeTypeShortName(type).data()},
				{"name", name != StringId::Invalid ? name.GetString() : ""},
				{"customData", nodeResult.json},
				{"position", {pos.x, pos.y} }
			}
		);
	}

	result.json["pins"] = json::array();
	json& pins = result.json["pins"];

	for (ScriptPinId pinId = GetFirstPinId(); pinId.id != ScriptPinId::InvalidId; pinId = GetNextPinId(pinId))
	{
		const ScriptPin& pin = GetPin(pinId);

		// Skip unconnected pins where default value hasn't been overriden. They will be recreated when the script is loaded.
		size_t connectedLinksCount;
		GetConnectedLinks(pinId, connectedLinksCount);
		if (connectedLinksCount == 0 && !pin.overridenValue.HasValue())
			continue;

		JsonData overridenValue = {};

		if (pin.overridenValue.HasValue())
			pin.overridenValue.WriteToJsonWithoutType(overridenValue);

		json dataType = {};

		if (pin.type == ScriptLinkType::Property)
			dataType = pin.dataType->GetName().GetString();

		pins.push_back(
			{
				{"id", pinId.id},
				{"node", pin.node.id},
				{"name", pin.name.GetString()},
				{"role", pin.role == ScriptPinRole::Input ? "Input" : "Output" },
				{"value", overridenValue.json},
				{"type", ScriptLinkTypeNames[(int)pin.type]},
				{"datatype", dataType}
			});

	}

	result.json["links"] = json::array();
	json& links = result.json["links"];

	for (ScriptLinkId linkId = GetFirstLinkId(); linkId.id != ScriptLinkId::InvalidId; linkId = GetNextLinkId(linkId))
	{

		const ScriptLink& link = GetLink(linkId);

		links.push_back(
			{
				{"id", linkId.id},
				{"sourcePin", link.sourcePinId.id},
				{"targetPin", link.targetPinId.id},
			});
	}
}

ScriptNodeId Script::CreateNode(ScriptNodeTypeId typeId)
{
	UpdateSequenceNumber();

	if (myFreeNodes.empty())
	{
		ScriptNodeId id = { (unsigned int)myNodes.size() };
		myNodes.push_back({ false, typeId, StringId::Invalid });
		return id;
	}
	ScriptNodeId id = myFreeNodes.back();
	myFreeNodes.pop_back();

	myNodes[id.id] = { false, typeId, StringId::Invalid };
	return id;
}

ScriptLinkId Script::CreateLink()
{
	UpdateSequenceNumber();

	if (myFreeLinks.empty())
	{
		ScriptLinkId id = { (unsigned int)myLinks.size() };
		myLinks.push_back({});
		return id;
	}
	ScriptLinkId id = myFreeLinks.back();
	myFreeLinks.pop_back();

	myLinks[id.id] = {};
	return id;
}

ScriptPinId Script::CreatePin()
{
	UpdateSequenceNumber();

	if (myFreePins.empty())
	{
		ScriptPinId id = { (unsigned int)myPins.size() };
		myPins.push_back({});
		return id;
	}
	ScriptPinId id = myFreePins.back();
	myFreePins.pop_back();

	myPins[id.id] = {};
	return id;
}

ScriptNodeId Script::CreateNode(ScriptNodeTypeId typeId, std::unique_ptr<ScriptNodeBase>&& newNode, Tga::Vector2f pos)
{
	UpdateSequenceNumber();

	ScriptNodeId id = CreateNode(typeId);
	NodeInternalData& nodeData = myNodes[id.id];
	nodeData.node = std::move(newNode);
	nodeData.pos = pos;
	return id;
}

ScriptLinkId Script::CreateLink(const ScriptLink& linkData)
{
	UpdateSequenceNumber();

	ScriptLinkId id = CreateLink();
	SetLink(id, linkData);
	return id;
}

ScriptPinId Script::CreatePin(const ScriptPin& pinData)
{
	UpdateSequenceNumber();

	ScriptPinId id = CreatePin();
	SetPin(id, pinData);
	return id;
}

void Script::CreateNodeWithReusedId(ScriptNodeId id, ScriptNodeTypeId typeId, std::unique_ptr<ScriptNodeBase>&& newNode, Tga::Vector2f pos)
{
	UpdateSequenceNumber();

	while (id.id >= myNodes.size())
	{
		// reserve removed nodes up to size;
		myNodes.push_back({ true, 0, StringId::Invalid });
	}
	assert(myNodes[id.id].isRemoved);

	for (int i = 0; i < myFreeNodes.size(); i++)
	{
		if (myFreeNodes[i] == id)
		{
			myFreeNodes.erase(myFreeNodes.begin() + i);
			break;
		}
	}

	NodeInternalData& nodeData = myNodes[id.id];
	nodeData = {};
	nodeData.typeId = typeId;
	nodeData.node = std::move(newNode);
	nodeData.pos = pos;
}

void Script::CreateLinkWithReusedId(ScriptLinkId id, const ScriptLink& newScriptLinkData)
{
	UpdateSequenceNumber();

	if (id.id >= myLinks.size())
	{
		// reserve removed nodes up to size;
		myLinks.resize(id.id + 1, { true });
	}
	assert(myLinks[id.id].isRemoved);

	for (int i = 0; i < myFreeLinks.size(); i++)
	{
		if (myFreeLinks[i] == id)
		{
			myFreeLinks.erase(myFreeLinks.begin() + i);
			break;
		}
	}

	myLinks[id.id] = {};
	SetLink(id, newScriptLinkData);
}

void Script::CreatePinWithReusedId(ScriptPinId id, const ScriptPin& newScriptPinData)
{
	UpdateSequenceNumber();

	if (id.id >= myPins.size())
	{
		// reserve removed nodes up to size;
		myPins.resize(id.id + 1, { true });
	}
	assert(myPins[id.id].isRemoved);

	for (int i = 0; i < myFreePins.size(); i++)
	{
		if (myFreePins[i] == id)
		{
			myFreePins.erase(myFreePins.begin() + i);
			break;
		}
	}

	myPins[id.id] = {};
	SetPin(id, newScriptPinData);
}

std::unique_ptr<ScriptNodeBase> Script::RemoveNode(ScriptNodeId id)
{
	UpdateSequenceNumber();

	assert(id.id < myNodes.size());
	if (myNodes[id.id].isRemoved)
		return nullptr;

	std::unique_ptr<ScriptNodeBase> node = std::move(myNodes[id.id].node);
	SetName(id, {});
	myNodes[id.id] = {};
	myNodes[id.id].isRemoved = true;
	myFreeNodes.push_back(id);

	return node;
}

void Script::RemoveLink(ScriptLinkId id)
{
	UpdateSequenceNumber();

	assert(id.id < myLinks.size());
	if (myLinks[id.id].isRemoved)
		return;

	SetLink(id, {});
	myLinks[id.id].isRemoved = true;
	myFreeLinks.push_back(id);
}

void Script::RemovePin(ScriptPinId id)
{
	UpdateSequenceNumber();

	assert(id.id < myPins.size());
	if (myPins[id.id].isRemoved)
		return;

	SetPin(id, {});
	myPins[id.id].isRemoved = true;
	myFreePins.push_back(id);
}

bool Script::Exists(ScriptNodeId id)
{
	UpdateSequenceNumber();

	if (id.id >= myNodes.size())
		return false;

	return !myNodes[id.id].isRemoved;
}

bool Script::Exists(ScriptLinkId id)
{
	UpdateSequenceNumber();

	if (id.id >= myLinks.size())
		return false;

	return !myLinks[id.id].isRemoved;
}

bool Script::Exists(ScriptPinId id)
{
	UpdateSequenceNumber();

	if (id.id >= myPins.size())
		return false;

	return !myPins[id.id].isRemoved;
}


ScriptNodeTypeId Script::GetType(ScriptNodeId id) const
{
	assert(id.id < myNodes.size());
	assert(myNodes[id.id].isRemoved == false);

	return myNodes[id.id].typeId;
}

const ScriptNodeBase& Script::GetNode(ScriptNodeId id) const
{
	assert(id.id < myNodes.size());
	assert(myNodes[id.id].isRemoved == false);

	return *myNodes[id.id].node;
}

ScriptNodeBase& Script::EditNode(ScriptNodeId id)
{
	UpdateSequenceNumber();

	assert(id.id < myNodes.size());
	assert(myNodes[id.id].isRemoved == false);

	return *myNodes[id.id].node;
}

StringId Script::GetName(ScriptNodeId id) const
{
	assert(id.id < myNodes.size());
	assert(myNodes[id.id].isRemoved == false);

	return myNodes[id.id].instanceName;
}
Tga::Vector2f Script::GetPosition(ScriptNodeId id) const
{
	assert(id.id < myNodes.size());
	assert(myNodes[id.id].isRemoved == false);

	return myNodes[id.id].pos;
}

const ScriptPinId* Script::GetInputPins(ScriptNodeId id, size_t& outCount) const
{
	assert(id.id < myNodes.size());
	assert(myNodes[id.id].isRemoved == false);

	outCount = myNodes[id.id].inputPins.size();
	return myNodes[id.id].inputPins.data();
}

const ScriptPinId* Script::GetOutputPins(ScriptNodeId id, size_t& outCount) const
{
	assert(id.id < myNodes.size());
	assert(myNodes[id.id].isRemoved == false);

	outCount = myNodes[id.id].outputPins.size();
	return myNodes[id.id].outputPins.data();
}

const ScriptLink& Script::GetLink(ScriptLinkId id) const
{
	assert(id.id < myLinks.size());
	assert(myLinks[id.id].isRemoved == false);

	return myLinks[id.id].link;
}

const ScriptPin& Script::GetPin(ScriptPinId id) const
{
	assert(id.id < myPins.size());
	assert(myPins[id.id].isRemoved == false);

	return myPins[id.id].pin;
}

const ScriptLinkId* Script::GetConnectedLinks(ScriptPinId id, size_t& outCount) const
{
	assert(id.id < myPins.size());
	assert(myPins[id.id].isRemoved == false);

	outCount = myPins[id.id].connectedLinks.size();
	return myPins[id.id].connectedLinks.data();
}

void Script::SetNode(ScriptNodeId id, std::unique_ptr<ScriptNodeBase>&& newNodeData)
{
	UpdateSequenceNumber();

	assert(id.id < myNodes.size());
	assert(myNodes[id.id].isRemoved == false);
	myNodes[id.id].node = std::move(newNodeData);
}

void Script::SetName(ScriptNodeId id, StringId name)
{
	UpdateSequenceNumber();

	assert(id.id < myNodes.size());
	assert(myNodes[id.id].isRemoved == false);

	StringId oldName = myNodes[id.id].instanceName;
	if (oldName != StringId::Invalid)
	{
		auto pair = myInstanceToId.equal_range(oldName);
		auto it = pair.first;
		for (; it != pair.second; ++it)
		{
			if (it->second.id == id.id)
			{
				myInstanceToId.erase(it);
				break;
			}
		}
	}
	if (name != StringId::Invalid)
	{
		myInstanceToId.insert(std::make_pair(name, id));
	}

	myNodes[id.id].instanceName = name;

}

void Script::SetPosition(ScriptNodeId id, Tga::Vector2f pos)
{
	UpdateSequenceNumber();

	assert(id.id < myNodes.size());
	assert(myNodes[id.id].isRemoved == false);

	myNodes[id.id].pos = pos;
}

void Script::SetLink(ScriptLinkId id, const ScriptLink& newScriptLinkData)
{
	UpdateSequenceNumber();

	assert(id.id < myLinks.size());
	assert(myLinks[id.id].isRemoved == false);

	ScriptPinId oldSourcePin = myLinks[id.id].link.sourcePinId;
	ScriptPinId oldTargetPin = myLinks[id.id].link.targetPinId;

	ScriptPinId newSourcePin = newScriptLinkData.sourcePinId;
	ScriptPinId newTargetPin = newScriptLinkData.targetPinId;

	if (oldSourcePin.id != newSourcePin.id)
	{
		if (oldSourcePin.id < myPins.size() && myPins[oldSourcePin.id].isRemoved == false)
		{
			PinInternalData& pinData = myPins[oldSourcePin.id];
			for (int i = 0; i < pinData.connectedLinks.size(); i++)
			{
				if (pinData.connectedLinks[i] == id)
				{
					pinData.connectedLinks.erase(pinData.connectedLinks.begin() + i);
					break;
				}
			}
		}

		if (newSourcePin.id < myPins.size() && myPins[newSourcePin.id].isRemoved == false)
			myPins[newSourcePin.id].connectedLinks.push_back(id);
	}

	if (oldTargetPin.id != newTargetPin.id)
	{
		if (oldTargetPin.id < myPins.size() && myPins[oldTargetPin.id].isRemoved == false)
		{
			PinInternalData& pinData = myPins[oldTargetPin.id];
			for (int i = 0; i < pinData.connectedLinks.size(); i++)
			{
				if (pinData.connectedLinks[i] == id)
				{
					pinData.connectedLinks.erase(pinData.connectedLinks.begin() + i);
					break;
				}
			}
		}

		if (newTargetPin.id < myPins.size() && myPins[newTargetPin.id].isRemoved == false)
			myPins[newTargetPin.id].connectedLinks.push_back(id);
	}

	myLinks[id.id].link = newScriptLinkData;
}

void Script::SetPin(ScriptPinId id, const ScriptPin& newScriptPinData)
{
	UpdateSequenceNumber();

	assert(id.id < myPins.size());
	assert(myPins[id.id].isRemoved == false);

	ScriptPinRole oldNodeRole = myPins[id.id].pin.role;
	ScriptPinRole newNodeRole = newScriptPinData.role;
	ScriptNodeId oldNodeId = myPins[id.id].pin.node;
	ScriptNodeId newNodeId = newScriptPinData.node;
	int oldSortingNumber = myPins[id.id].pin.sortingNumber;
	int newSortingNumber = newScriptPinData.sortingNumber;

	if (oldNodeId != newNodeId || oldNodeRole != newNodeRole || oldSortingNumber != newSortingNumber)
	{
		if (oldNodeId.id < myNodes.size() && myNodes[oldNodeId.id].isRemoved == false)
		{
			std::vector<ScriptPinId>& pinsToEdit = oldNodeRole == ScriptPinRole::Input ? myNodes[oldNodeId.id].inputPins : myNodes[oldNodeId.id].outputPins;
			for (int i = 0; i < pinsToEdit.size(); i++)
			{
				if (pinsToEdit[i] == id)
				{
					pinsToEdit.erase(pinsToEdit.begin() + i);
					break;
				}
			}
		}

		if (newNodeId.id < myNodes.size() && myNodes[newNodeId.id].isRemoved == false)
		{
			std::vector<ScriptPinId>& pinsToEdit = newNodeRole == ScriptPinRole::Input ? myNodes[newNodeId.id].inputPins : myNodes[newNodeId.id].outputPins;
			pinsToEdit.push_back(id);

			std::sort(begin(pinsToEdit), end(pinsToEdit), [&](ScriptPinId a, ScriptPinId b)
				{
					return myPins[a.id].pin.sortingNumber < myPins[b.id].pin.sortingNumber;
				});
		}
	}

	myPins[id.id].pin = newScriptPinData;
}

ScriptNodeId Script::GetFirstNodeId() const
{
	for (unsigned int i = 0; i < myNodes.size(); i++)
	{
		if (!myNodes[i].isRemoved)
			return { i };
	}

	return { ScriptNodeId::InvalidId };
}
ScriptNodeId Script::GetNextNodeId(ScriptNodeId previousId) const
{
	for (unsigned int i = (unsigned int)previousId.id + 1; i < myNodes.size(); i++)
	{
		if (!myNodes[i].isRemoved)
			return { i };
	}

	return { ScriptNodeId::InvalidId };
}
ScriptNodeId Script::GetLastNodeId() const
{
	for (unsigned int i = (unsigned int)myNodes.size(); i > 0; i--)
	{
		if (!myNodes[i - 1].isRemoved)
			return { i - 1 };
	}

	return { ScriptNodeId::InvalidId };
}

ScriptLinkId Script::GetFirstLinkId() const
{
	for (unsigned int i = 0; i < myLinks.size(); i++)
	{
		if (!myLinks[i].isRemoved)
			return { i };
	}

	return { ScriptLinkId::InvalidId };
}

ScriptLinkId Script::GetNextLinkId(ScriptLinkId previousId) const
{
	for (unsigned int i = (unsigned int)previousId.id + 1; i < myLinks.size(); i++)
	{
		if (!myLinks[i].isRemoved)
			return { i };
	}

	return { ScriptLinkId::InvalidId };
}

ScriptLinkId Script::GetLastLinkId() const
{
	for (unsigned int i = (unsigned int)myLinks.size(); i > 0; i--)
	{
		if (!myLinks[i - 1].isRemoved)
			return { i - 1 };
	}

	return { ScriptLinkId::InvalidId };
}

ScriptPinId Script::GetFirstPinId() const
{
	for (unsigned int i = 0; i < myPins.size(); i++)
	{
		if (!myPins[i].isRemoved)
			return { i };
	}

	return { ScriptPinId::InvalidId };
}
ScriptPinId Script::GetNextPinId(ScriptPinId previousId) const
{
	for (unsigned int i = (unsigned int)previousId.id + 1; i < myPins.size(); i++)
	{
		if (!myPins[i].isRemoved)
			return { i };
	}

	return { ScriptPinId::InvalidId };
}

ScriptPinId Script::GetLastPinId() const
{
	for (unsigned int i = 0; i < myPins.size(); i++)
	{
		if (!myPins[i].isRemoved)
			return { i };
	}

	return { ScriptPinId::InvalidId };
}

ScriptNodeId Script::FindNodeByInstanceName(StringId stringId) const
{
	auto pair = myInstanceToId.equal_range(stringId);
	if (pair.first == pair.second)
		return { ScriptNodeId::InvalidId };

	auto it = pair.first;
	it++;
	if (it == pair.second)
		return pair.first->second;

	return { ScriptNodeId::InvalidId };
}