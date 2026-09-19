#pragma once

#include <age/script/ScriptCommon.h>

namespace Ag
{

class ScriptExecutionContext
{
	// todo: report all nodes and links activated the last few seconds to some kind of debug service, to be able to show execution flow
	// use to color edges and nodes somehow
	// also, could implement breakpoints in a debug service

	static constexpr int MAX_TRIGGERED_OUTPUTS = 16;

	ScriptRuntimeInstance& myScriptRuntimeInstance;
	ScriptUpdateContext& myUpdateContext;
	ScriptNodeId myNodeId;
	char* myNodeRuntimeInstance;

	ScriptPinId myTriggeredOutputQueue[MAX_TRIGGERED_OUTPUTS];
	int myTriggeredOutputCount = 0;

public:
	ScriptExecutionContext(ScriptRuntimeInstance& scriptRuntimeInstance, ScriptUpdateContext& updateContext, ScriptNodeId nodeId, char* nodeRuntimeInstance);
	~ScriptExecutionContext();
	ScriptUpdateContext& GetUpdateContext();
	void* GetNodeRuntimeDataPtr() const ;

	/// <summary>
	/// Triggers and output pin. The execution is deferred until the ScriptExecutionContext is destroyed.
	/// </summary>
	/// <param name="outputPin"></param>
	void TriggerOutputPin(ScriptPinId outputPin);

	/// <summary>
	/// Runs whatever an output pin leads to right now, to completion, before returning. A loop node uses this to run its
	/// body once per iteration; TriggerOutputPin defers the run until the node has finished.
	/// </summary>
	void RunOutputPin(ScriptPinId outputPin);

	/// <summary>
	/// Reads an input pin. This reading functions to be called on the corresponding node immediately. 
	/// </summary>
	/// <param name="inputPin"></param>
	/// <returns></returns>
	Property ReadInputPin(ScriptPinId inputPin);
};

} // namespace Ag