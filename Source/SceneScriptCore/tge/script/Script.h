#pragma once

#include "ScriptCommon.h"

#include <tge/math/vector2.h>

#include <vector>
#include <unordered_map>

namespace Tga
{
class Script
{
	Script(const Script&) = delete;
	Script& operator=(const Script&) = delete;

	struct NodeInternalData
	{
		bool isRemoved;
		ScriptNodeTypeId typeId;
		StringId instanceName;
		Tga::Vector2f pos;
		std::unique_ptr<ScriptNodeBase> node;
		std::vector<ScriptPinId> inputPins;
		std::vector<ScriptPinId> outputPins;
	};

	struct LinkInternalData
	{
		bool isRemoved;
		ScriptLink link;
	};

	struct PinInternalData
	{
		bool isRemoved;
		ScriptPin pin;
		std::vector<ScriptLinkId> connectedLinks;
	};

	int mySequenceNumber = 0;

	std::vector<NodeInternalData> myNodes;
	std::vector<ScriptNodeId> myFreeNodes;

	std::vector<LinkInternalData> myLinks;
	std::vector<ScriptLinkId> myFreeLinks;

	std::vector<PinInternalData> myPins;
	std::vector<ScriptPinId> myFreePins;

	std::unordered_multimap<StringId, ScriptNodeId> myInstanceToId;

	void UpdateSequenceNumber() { mySequenceNumber++; }

public:
	Script();
	~Script();

	void Clear();

	void LoadFromJson(const JsonData& data);
	void WriteToJson(JsonData& data);

	ScriptNodeId CreateNode(ScriptNodeTypeId typeId);
	ScriptLinkId CreateLink();
	ScriptPinId CreatePin();

	ScriptNodeId CreateNode(ScriptNodeTypeId typeId, std::unique_ptr<ScriptNodeBase>&& node, Tga::Vector2f pos);
	ScriptLinkId CreateLink(const ScriptLink& newScriptLinkData);
	ScriptPinId CreatePin(const ScriptPin& newScriptPinData);

	/// <summary>
	/// Creates a node and reuses an existing ID. The ID may not be in use when this is called. Useful for implementing undo/redo and serialization
	/// </summary>
	void CreateNodeWithReusedId(ScriptNodeId id, ScriptNodeTypeId typeId, std::unique_ptr<ScriptNodeBase>&& node, Tga::Vector2f pos);
	/// <summary>
	/// Creates a link and reuses an existing ID.The ID may not be in use when this is called. Useful for implementing undo/redo and serialization
	/// </summary>
	void CreateLinkWithReusedId(ScriptLinkId id, const ScriptLink& newScriptLinkData);
	/// <summary>
	/// Creates a Pin and reuses an existing ID. The ID may not be in use when this is called. Useful for implementing undo/redo and serialization
	/// </summary>
	void CreatePinWithReusedId(ScriptPinId id, const ScriptPin& newScriptPinData);

	std::unique_ptr<ScriptNodeBase> RemoveNode(ScriptNodeId id);
	void RemoveLink(ScriptLinkId id);
	void RemovePin(ScriptPinId id);

	bool Exists(ScriptNodeId id);
	bool Exists(ScriptLinkId id);
	bool Exists(ScriptPinId id);

	ScriptNodeTypeId GetType(ScriptNodeId id) const;
	const ScriptNodeBase& GetNode(ScriptNodeId id) const;
	ScriptNodeBase& EditNode(ScriptNodeId id);
	StringId GetName(ScriptNodeId id) const;
	Tga::Vector2f GetPosition(ScriptNodeId id) const;

	const ScriptPinId* GetInputPins(ScriptNodeId node, size_t& outCount) const;
	const ScriptPinId* GetOutputPins(ScriptNodeId node, size_t& outCount) const;

	const ScriptLink& GetLink(ScriptLinkId id) const;
	const ScriptPin& GetPin(ScriptPinId id) const;

	const ScriptLinkId* GetConnectedLinks(ScriptPinId pin, size_t& outCount) const;

	void SetNode(ScriptNodeId id, std::unique_ptr<ScriptNodeBase>&& newNodeData);
	void SetName(ScriptNodeId id, StringId name);
	void SetPosition(ScriptNodeId id, Tga::Vector2f pos);

	void SetLink(ScriptLinkId id, const ScriptLink& newScriptLinkData);
	void SetPin(ScriptPinId id, const ScriptPin& newScriptPinData);

	ScriptNodeId GetFirstNodeId() const;
	ScriptNodeId GetNextNodeId(ScriptNodeId previousId) const;
	ScriptNodeId GetLastNodeId() const;

	ScriptLinkId GetFirstLinkId() const;
	ScriptLinkId GetNextLinkId(ScriptLinkId previousId) const;
	ScriptLinkId GetLastLinkId() const;

	ScriptPinId GetFirstPinId() const;
	ScriptPinId GetNextPinId(ScriptPinId previousId) const;
	ScriptPinId GetLastPinId() const;


	/// <summary>
	/// Finds a node by instance name. Returns an invalid id if there are multiple nodes with the same name
	/// </summary>
	/// <param name="stringId"></param>
	/// <returns></returns>
	ScriptNodeId FindNodeByInstanceName(StringId stringId) const;

	/// <summary>
	/// Used to track changes to the script. Each version of a script has a unique sequence number, except for during the execution of commands and similar constructs that alter the sequence number. 
	/// </summary>
	/// <returns></returns>
	int GetSequenceNumber() const { return mySequenceNumber; };

	/// <summary>
	/// Should only be used to implement functionality such as Commands and Undo, that combines multiple changes into one or restores older versions of the script
	/// </summary>
	/// <param name="sequenceNumber"></param>
	void SetSequenceNumber(int sequenceNumber) { mySequenceNumber = sequenceNumber; }
};
} // namespace Tga