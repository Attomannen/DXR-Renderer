#include <tge/editor/ScriptEditor/Commands/DestroyNodeAndLinksCommand.h>

#include <tge/script/Script.h>
#include <tge/script/ScriptNodeBase.h>

using namespace Tga;

void DestroyNodeAndLinksCommand::Add(ScriptNodeId nodeId)
{
	myNodes[nodeId] = {};
}

void DestroyNodeAndLinksCommand::Add(ScriptLinkId linkId)
{
	myLinks[linkId] = {};
}


void DestroyNodeAndLinksCommand::ExecuteImpl()
{
	// figure out which pins and links have to be removed when nodes are removed 
	for (std::pair<const ScriptNodeId, CommandNodeData>& node : myNodes)
	{
		ScriptNodeId nodeId = node.first;
		{
			size_t inputPinCount;
			const ScriptPinId* pins = myScript.GetInputPins(nodeId, inputPinCount);

			for (int pinIndex = 0; pinIndex < inputPinCount; pinIndex++)
			{
				ScriptPinId pin = pins[pinIndex];

				size_t linkCount;

				const ScriptLinkId* links = myScript.GetConnectedLinks(pin, linkCount);
				for (int linkIndex = 0; linkIndex < linkCount; linkIndex++)
				{
					ScriptLinkId link = links[linkIndex];
					myLinks[link] = {};
				}

				myPins[pin] = {};
			}
		}

		{
			size_t inputPinCount;
			const ScriptPinId* pins = myScript.GetOutputPins(nodeId, inputPinCount);

			for (int pinIndex = 0; pinIndex < inputPinCount; pinIndex++)
			{
				ScriptPinId pin = pins[pinIndex];

				size_t linkCount;

				const ScriptLinkId* links = myScript.GetConnectedLinks(pin, linkCount);
				for (int linkIndex = 0; linkIndex < linkCount; linkIndex++)
				{
					ScriptLinkId link = links[linkIndex];
					myLinks[link] = {};
				}

				myPins[pin] = {};
			}
		}
	}

	// remove links and remove any error pins won't have any links left
	for (std::pair<const ScriptLinkId, ScriptLink>& link : myLinks)
	{
		ScriptLinkId linkId = link.first;
		ScriptLink& linkData = link.second;
		linkData = myScript.GetLink(linkId);

		myScript.RemoveLink(linkId);

		// add unknown pins for removal if all their links are removed
		{
			const ScriptPin& pin = myScript.GetPin(linkData.sourcePinId);
			if (pin.type == ScriptLinkType::Unknown)
			{
				size_t linkCount;
				myScript.GetConnectedLinks(linkData.sourcePinId, linkCount);

				if (linkCount == 0)
					myPins[linkData.sourcePinId] = {};
			}
		}

		{
			const ScriptPin& pin = myScript.GetPin(linkData.targetPinId);
			if (pin.type == ScriptLinkType::Unknown && myPins.find(linkData.targetPinId) == myPins.end())
			{
				size_t linkCount;
				myScript.GetConnectedLinks(linkData.targetPinId, linkCount);

				if (linkCount == 0)
					myPins[linkData.targetPinId] = {};
			}
		}
	}

	// remove pins
	for (std::pair<const ScriptPinId, ScriptPin>& pin : myPins)
	{
		myPins[pin.first] = myScript.GetPin(pin.first);
		myScript.RemovePin(pin.first);
	}

	// remove nodes
	for (std::pair<const ScriptNodeId, CommandNodeData>& node : myNodes)
	{
		ScriptNodeId nodeId = node.first;
		CommandNodeData& nodeData = node.second;

		nodeData.typeId = myScript.GetType(nodeId);
		nodeData.pos = myScript.GetPosition(nodeId);
		nodeData.instanceName = myScript.GetName(nodeId);
		nodeData.node = myScript.RemoveNode(nodeId);
	}
}

void DestroyNodeAndLinksCommand::UndoImpl()
{
	for (std::pair<const ScriptNodeId, CommandNodeData>& node : myNodes)
	{
		ScriptNodeId nodeId = node.first;
		CommandNodeData& nodeData = node.second;

		myScript.CreateNodeWithReusedId(nodeId, nodeData.typeId, std::move(nodeData.node), nodeData.pos);
		myScript.SetName(nodeId, nodeData.instanceName);
	}

	for (std::pair<const ScriptPinId, ScriptPin>& pin : myPins)
	{
		myScript.CreatePinWithReusedId(pin.first, pin.second);
	}

	for (std::pair<const ScriptLinkId, ScriptLink>& link : myLinks)
	{
		myScript.CreateLinkWithReusedId(link.first, link.second);
	}

}