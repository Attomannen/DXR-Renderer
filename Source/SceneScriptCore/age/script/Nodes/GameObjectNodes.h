#pragma once

namespace Ag
{
	// Nodes that act on the object a script runs on. They need a GameScriptContext,
	// so they do nothing when run somewhere that does not provide one (editor preview).
	void RegisterGameObjectNodes();
} // namespace Ag
