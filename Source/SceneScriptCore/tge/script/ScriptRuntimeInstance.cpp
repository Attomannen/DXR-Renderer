#include <stdafx.h>

#include "ScriptRuntimeInstance.h"

using namespace Tga;


ScriptRuntimeInstance::ScriptRuntimeInstance(const std::shared_ptr<const Script>& script)
	: myScript(script)
{
	assert(script);
}

ScriptRuntimeInstance::~ScriptRuntimeInstance()
{
	if (!myRuntimeDataOffsets.empty())
	{
		for (ScriptNodeId currentNodeId = myScript->GetFirstNodeId(); currentNodeId.id != ScriptNodeId::InvalidId; currentNodeId = myScript->GetNextNodeId(currentNodeId))
		{
			const ScriptNodeBase& node = myScript->GetNode(currentNodeId);

			node.DestroyRuntimeData(myRuntimeDataBuffer.data() + myRuntimeDataOffsets[currentNodeId.id]);
		}
	}
	myRuntimeDataOffsets.clear();
}

void ScriptRuntimeInstance::Init()
{
	myActiveNodes.clear();

	int dataSize = 0;

	ScriptNodeId lastNodeId = myScript->GetLastNodeId();
	myRuntimeDataOffsets.resize(lastNodeId.id + 1);

	for (ScriptNodeId currentNodeId = myScript->GetFirstNodeId(); currentNodeId.id != ScriptNodeId::InvalidId; currentNodeId = myScript->GetNextNodeId(currentNodeId))
	{
		const ScriptNodeBase& node = myScript->GetNode(currentNodeId);
		myRuntimeDataOffsets[currentNodeId.id] = dataSize;
		dataSize += node.GetRuntimeDataSize();
	}

	myRuntimeDataBuffer.resize(dataSize);

	for (ScriptNodeId currentNodeId = myScript->GetFirstNodeId(); currentNodeId.id != ScriptNodeId::InvalidId; currentNodeId = myScript->GetNextNodeId(currentNodeId))
	{
		const ScriptNodeBase& node = myScript->GetNode(currentNodeId);

		node.CreateRuntimeData(myRuntimeDataBuffer.data() + myRuntimeDataOffsets[currentNodeId.id]);

		if (node.ShouldExecuteAtStart())
		{
			myActiveNodes.push_back(currentNodeId);
		}
	}
}

void ScriptRuntimeInstance::Reset()
{

	myActiveNodes.clear();

	if (myRuntimeDataOffsets.empty())
	{
		Init();
	}
	else
	{
		for (ScriptNodeId currentNodeId = myScript->GetFirstNodeId(); currentNodeId.id != ScriptNodeId::InvalidId; currentNodeId = myScript->GetNextNodeId(currentNodeId))
		{
			const ScriptNodeBase& node = myScript->GetNode(currentNodeId);

			node.DestroyRuntimeData(myRuntimeDataBuffer.data() + myRuntimeDataOffsets[currentNodeId.id]);
			node.CreateRuntimeData(myRuntimeDataBuffer.data() + myRuntimeDataOffsets[currentNodeId.id]);

			if (node.ShouldExecuteAtStart())
			{
				myActiveNodes.push_back(currentNodeId);
			}
		}
	}
}

void ScriptRuntimeInstance::Update(ScriptUpdateContext& updateContext)
{
	for (int i = 0; i < myActiveNodes.size(); i++)
	{
		ScriptNodeId nodeId = myActiveNodes[i];
		ScriptExecutionContext executionContext(*this, updateContext, nodeId, myRuntimeDataBuffer.data() + myRuntimeDataOffsets[nodeId.id]);
		const ScriptNodeBase& node = myScript->GetNode(nodeId);

		ScriptNodeResult result = node.Execute(executionContext, { ScriptPinId::InvalidId });
		if (result == ScriptNodeResult::Finished)
		{
			myActiveNodes.erase(begin(myActiveNodes) + i);
			i--;
		}
	}
}

void ScriptRuntimeInstance::TriggerPin(ScriptPinId pinId, ScriptUpdateContext& updateContext)
{
	ScriptPin pin = myScript->GetPin(pinId);
	ScriptNodeId nodeId = pin.node;
	ScriptExecutionContext executionContext(*this, updateContext, nodeId, myRuntimeDataBuffer.data() + myRuntimeDataOffsets[nodeId.id]);
	const ScriptNodeBase& node = myScript->GetNode(nodeId);

	assert(pin.type == ScriptLinkType::Flow);
	if (pin.role == ScriptPinRole::Input)
	{
		ScriptNodeResult result = node.Execute(executionContext, pinId);
		if (result == ScriptNodeResult::KeepRunning)
		{
			ActivateNode(nodeId);
		}
		else
		{
			DeactivateNode(nodeId);
		}
	}
	else
	{
		executionContext.TriggerOutputPin(pinId);
	}
}

const Script& ScriptRuntimeInstance::GetScript() const
{
	return *myScript;
}

char* ScriptRuntimeInstance::GetRuntimeInstance(ScriptNodeId nodeId)
{
	assert("Invalid node" && nodeId.id < myRuntimeDataOffsets.size());
	return myRuntimeDataBuffer.data() + myRuntimeDataOffsets[nodeId.id];
}

void ScriptRuntimeInstance::ActivateNode(ScriptNodeId nodeId)
{
	for (int i = 0; i < myActiveNodes.size(); i++)
	{
		if (myActiveNodes[i] == nodeId)
		{
			return;
		}
	}

	myActiveNodes.push_back(nodeId);
}

void ScriptRuntimeInstance::DeactivateNode(ScriptNodeId nodeId)
{
	for (int i = 0; i < myActiveNodes.size(); i++)
	{
		if (myActiveNodes[i] == nodeId)
		{
			myActiveNodes.erase(begin(myActiveNodes) + i);
			return;
		}
	}
}
