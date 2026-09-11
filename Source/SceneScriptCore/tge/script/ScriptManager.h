#pragma once

#include <unordered_map>
#include <memory>
#include <string_view>

namespace Tga
{
	class Script;
	namespace ScriptManager
	{
		extern std::shared_ptr<const Script> GetScript(std::string_view name);
		extern Script* GetEditableScript(std::string_view name);
		extern void AddEditableScript(std::string_view name, std::unique_ptr<Script>&& script);
	}
}