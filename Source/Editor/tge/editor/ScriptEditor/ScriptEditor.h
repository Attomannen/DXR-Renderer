#pragma once

#include <tge/editor/ScriptEditor/ScriptEditorSelection.h>
#include <tge/script/ScriptCommon.h>
#include <map>

struct ImNodesEditorContext;

namespace Tga
{
	class Script;

	const uint8_t* GetScriptLinkColor(const ScriptPin& pin);
	const uint8_t* GetScriptLinkHoverColor(const ScriptPin& pin);
	const uint8_t* GetScriptLinkSelectedColor(const ScriptPin& pin);
	
	class MoveNodesCommand;
	
	class EditorScriptManager
	{
		EditorScriptManager();
		~EditorScriptManager();

		struct EditorScriptData
		{
			Script* script;
			ScriptEditorSelection selection = {};
			ImNodesEditorContext* nodeEditorContext = nullptr;
			ScriptPinId inProgressLinkPin = { ScriptPinId::InvalidId };
			ScriptNodeId hoveredNode = { ScriptNodeId::InvalidId };
			int latestSavedSequenceNumber = 0;
			bool hasBeenRemoved = false;

			std::shared_ptr<MoveNodesCommand> inProgressMove;
		};
	
		std::map<std::string, EditorScriptData, std::less<>> myOpenScripts;
	
	public:
		static EditorScriptManager& GetInstance();
	
		void Init();

		Script& CreateNewScript(const std::string_view& aName);
		void MarkScriptAsRemoved(const std::string_view aName);
		void MarkScriptAsAdded(const std::string_view aName);
		void DisplayEditor(const std::string_view& aActiveScript, ScriptPinId &aPinToTrigger, bool aIsRunning);

		void GetAllScriptsThatStartsWithPath(const std::string_view path, std::vector<std::string_view>& scripts);

		ScriptEditorSelection& GetSelection(const std::string_view& aName);

		void SaveAll();
	};

} // namespace Tga