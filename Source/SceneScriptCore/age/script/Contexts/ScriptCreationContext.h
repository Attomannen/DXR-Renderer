#pragma once

#include <age/script/ScriptCommon.h>

namespace Ag
{

class Script;

class ScriptCreationContext
{
	Script& myScript;
	ScriptNodeId myNodeId;
public:
	ScriptCreationContext(Script& script, ScriptNodeId nodeId);
	ScriptNodeId GetNodeId() const { return myNodeId; };
	ScriptPinId FindOrCreatePin(ScriptPin pinData) const;
};

} // namespace Ag