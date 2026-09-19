#pragma once

#include "Script.h"
#include "ScriptNodeBase.h"

#include <vector>

namespace Ag
{
struct ScriptUpdateContext;
enum class ScriptEventKind : int;

class ScriptRuntimeInstance
{
	std::shared_ptr<const Script> myScript;

	std::vector<int> myRuntimeDataOffsets;
	std::vector<char> myRuntimeDataBuffer;

	std::vector<ScriptNodeId> myActiveNodes;

public:
	ScriptRuntimeInstance(const std::shared_ptr<const Script>& script);
	~ScriptRuntimeInstance();
	void Init();
	void Reset();
	void Update(ScriptUpdateContext& context);
	void TriggerPin(ScriptPinId pin, ScriptUpdateContext& context);
	// Starts the chain of every event node of this kind.
	void TriggerEvent(ScriptEventKind kind, ScriptUpdateContext& context);

	const Script& GetScript() const;
	char* GetRuntimeInstance(ScriptNodeId nodeId);
	void ActivateNode(ScriptNodeId nodeId);
	void DeactivateNode(ScriptNodeId nodeId);
};


} // namespace Ag