#include <tge/editor/ScriptEditor/Commands/CreateNodeCommand.h>

#include <tge/script/Script.h>
#include <tge/script/ScriptNodeTypeRegistry.h>

using namespace Tga;

void CreateNodeCommand::ExecuteImpl()
{
	if (myNodeId.id == ScriptNodeId::InvalidId)
	{
		myNodeId = myScript.CreateNode(myNodeData.typeId, ScriptNodeTypeRegistry::CreateNode(myNodeData.typeId), myNodeData.pos);
		ScriptNodeBase& node = (myScript.EditNode(myNodeId));
		ScriptCreationContext context(myScript, myNodeId);
		node.Init(context);
	}
	else
	{
		myScript.CreateNodeWithReusedId(myNodeId, myNodeData.typeId, std::move(myNodeData.node), myNodeData.pos);
		for (std::pair<const ScriptPinId, ScriptPin>& pin : myPins)
		{
			myScript.CreatePinWithReusedId(pin.first, pin.second);
		}
	}
}
void CreateNodeCommand::UndoImpl()
{
	{
		size_t inputPinCount;
		const ScriptPinId* pins = myScript.GetInputPins(myNodeId, inputPinCount);

		for (int pinIndex = (int)inputPinCount - 1; pinIndex >= 0; pinIndex--)
		{
			ScriptPinId pin = pins[pinIndex];
			myPins[pin] = myScript.GetPin(pin);
			myScript.RemovePin(pin);
		}
	}

	{
		size_t inputPinCount;
		const ScriptPinId* pins = myScript.GetOutputPins(myNodeId, inputPinCount);

		for (int pinIndex = (int)inputPinCount - 1; pinIndex >= 0; pinIndex--)
		{
			ScriptPinId pin = pins[pinIndex];
			myPins[pin] = myScript.GetPin(pin);
			myScript.RemovePin(pin);
		}
	}

	myNodeData.node = myScript.RemoveNode(myNodeId);
}
