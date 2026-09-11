#include <stdafx.h>

#include <tge/script/Contexts/ScriptExecutionContext.h>
#include <tge/script/Script.h>
#include <tge/script/ScriptRuntimeInstance.h>
#include "tge/script/ScriptNodeTypeRegistry.h"
#include <tge/log/Log.h>

using namespace Tga;

ScriptUpdateContext& ScriptExecutionContext::GetUpdateContext()
{
	return myUpdateContext;
}

ScriptExecutionContext::ScriptExecutionContext(ScriptRuntimeInstance& scriptRuntimeInstance, ScriptUpdateContext& updateContext, ScriptNodeId nodeId, char* nodeRuntimeInstance)
	: myScriptRuntimeInstance(scriptRuntimeInstance)
	, myUpdateContext(updateContext)
	, myNodeId(nodeId)
	, myNodeRuntimeInstance(nodeRuntimeInstance)
{}

ScriptExecutionContext::~ScriptExecutionContext()
{
	const Script& script = myScriptRuntimeInstance.GetScript();

	for (int i = 0; i < myTriggeredOutputCount; i++)
	{
		ScriptPinId pinId = myTriggeredOutputQueue[i];

		size_t count;
		const ScriptLinkId* linkIds = script.GetConnectedLinks(pinId, count);

		assert("Trying to trigger an output pin that isn't of type flow" && script.GetPin(pinId).type == ScriptLinkType::Flow);
		assert("Only one link allowed on Flow out pins" && count <= 1);

		if (count == 0)
			return;

		const ScriptLink& link = script.GetLink(linkIds[0]);
		ScriptPinId targetPinId = link.targetPinId;
		const ScriptPin& targetPin = script.GetPin(targetPinId);

		ScriptNodeId nodeId = targetPin.node;

		ScriptExecutionContext executionContext(myScriptRuntimeInstance, myUpdateContext, nodeId, myScriptRuntimeInstance.GetRuntimeInstance(nodeId));

		const ScriptNodeBase& node = script.GetNode(nodeId);

		ScriptNodeResult result = node.Execute(executionContext, targetPinId);
		if (result == ScriptNodeResult::KeepRunning)
		{
			myScriptRuntimeInstance.ActivateNode(nodeId);
		}
		else
		{
			myScriptRuntimeInstance.DeactivateNode(nodeId);
		}
	}
}

void ScriptExecutionContext::TriggerOutputPin(ScriptPinId pinId)
{
	assert(myTriggeredOutputCount < MAX_TRIGGERED_OUTPUTS);
	
	if (myTriggeredOutputCount < MAX_TRIGGERED_OUTPUTS)
	{
		myTriggeredOutputQueue[myTriggeredOutputCount] = pinId;
		myTriggeredOutputCount++;
	}
}

Property ScriptExecutionContext::ReadInputPin(ScriptPinId pinId)
{
	const Script& script = myScriptRuntimeInstance.GetScript();

	size_t count;
	const ScriptLinkId* linkIds = script.GetConnectedLinks(pinId, count);

	const ScriptPin& pin = script.GetPin(pinId);

	assert("Trying to read from a flow pin" && pin.type != ScriptLinkType::Flow);
	assert("Trying to read from a pin with unknown type" && pin.type != ScriptLinkType::Unknown);
	assert("Only one link allowed on value input pins" && count <= 1);

	if (count != 0)
	{
		const ScriptLink& link = script.GetLink(linkIds[0]);
		ScriptPinId sourcePinId = link.sourcePinId;
		const ScriptPin& sourcePin = script.GetPin(sourcePinId);

		ScriptNodeId nodeId = sourcePin.node;

		ScriptExecutionContext executionContext(*this);
		executionContext.myNodeId = nodeId;
		executionContext.myNodeRuntimeInstance = myScriptRuntimeInstance.GetRuntimeInstance(nodeId);

		const ScriptNodeBase& node = script.GetNode(nodeId);

		Property result = node.ReadPin(executionContext, sourcePinId);

		if (result.HasValue() && result.GetType() == pin.dataType)
		{
			return result;
		}
		else
		{
			// This will never happen with a correctly implemented graph and nodes, but can happen
			// if node types are missing or implemented incorrectly
			std::string nodeName(ScriptNodeTypeRegistry::GetNodeTypeShortName(script.GetType(nodeId)));
			ERROR_PRINT("Failed reading pin on node: %s. Expected: %s, got: %s",
				nodeName.c_str(),
				pin.dataType->GetName().GetString(),
				(result.HasValue() ? result.GetType()->GetName().GetString() : "empty"));
		}
	}

	if (pin.overridenValue.GetType() != nullptr)
		return pin.overridenValue;

	return pin.defaultValue;
}


void* ScriptExecutionContext::GetNodeRuntimeDataPtr() const
{
	return myNodeRuntimeInstance;
}
