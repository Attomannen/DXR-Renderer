#pragma once

#include <tge/editor/ScriptEditor/ScriptEditorSelection.h>
#include <tge/script/ScriptCommon.h>
#include <map>
#include <unordered_map>
#include <vector>

namespace ax { namespace NodeEditor { struct EditorContext; } }

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
			ax::NodeEditor::EditorContext* nodeEditorContext = nullptr;
			ScriptPinId inProgressLinkPin = { ScriptPinId::InvalidId };
			ScriptNodeId hoveredNode = { ScriptNodeId::InvalidId };
			int latestSavedSequenceNumber = 0;
			bool hasBeenRemoved = false;

			std::shared_ptr<MoveNodesCommand> inProgressMove;

			// Editor state that is not part of the script.
			ScriptNodeId pendingFocusNode = { ScriptNodeId::InvalidId }; // select and frame this node next frame
			std::vector<ScriptNodeId> pendingSelect;                     // select these next frame (just created)
			bool showIssues = false;                                     // the compile results list is open
			std::unordered_map<unsigned int, float> pinCanvasY;          // where each pin was drawn this frame
			ScriptNodeId editingComment = { ScriptNodeId::InvalidId };
			ScriptPinId contextPin = { ScriptPinId::InvalidId };         // pin the right-click menu is about
			ScriptPinId dragPin = { ScriptPinId::InvalidId };            // pin a wire was dragged off into empty space
			bool searchJustOpened = false;
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