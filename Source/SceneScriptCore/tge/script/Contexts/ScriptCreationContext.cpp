#include <stdafx.h>

#include <tge/script/Contexts/ScriptCreationContext.h>
#include <tge/script/Script.h>

#include <cassert>

using namespace Tga;

ScriptCreationContext::ScriptCreationContext(Script& script, ScriptNodeId nodeId)
	: myScript(script)
	, myNodeId(nodeId)
{}

ScriptPinId ScriptCreationContext::FindOrCreatePin(ScriptPin pinData) const
{
	assert(pinData.node == myNodeId);

	assert(pinData.role != ScriptPinRole::Input || pinData.dataType == pinData.defaultValue.GetType());

	size_t count;
	const ScriptPinId* pins;
	if (pinData.role == ScriptPinRole::Input)
	{
		pins = myScript.GetInputPins(myNodeId, count);
	}
	else
	{
		pins = myScript.GetOutputPins(myNodeId, count);
	}

	for (int i = 0; i < count; i++)
	{
		ScriptPinId id = pins[i];
		const ScriptPin& oldPin = myScript.GetPin(id);
		if (oldPin.name == pinData.name)
		{
			if (oldPin.overridenValue.GetType() == pinData.defaultValue.GetType())
			{
				pinData.overridenValue = oldPin.overridenValue;
			}

			myScript.SetPin(id, pinData);
			return id;
		}
	}

	return myScript.CreatePin(pinData);
}
