#pragma once

#include <age/editor/ScriptEditor/ScriptEditorSelection.h>
#include <age/script/ScriptCommon.h>
#include <map>
#include <unordered_map>
#include <vector>

namespace ax { namespace NodeEditor { struct EditorContext; } }

namespace Ag
{
	class Script;

	const uint8_t* GetScriptLinkColor(const ScriptPin& pin);
	const uint8_t* GetScriptLinkHoverColor(const ScriptPin& pin);
	const uint8_t* GetScriptLinkSelectedColor(const ScriptPin& pin);
	
	class MoveNodesCommand;
	class SceneObjectDefinition;

	// The node graph editor for one script. Each object-definition document owns one.
	class ScriptGraphEditor
	{
	public:
		ScriptGraphEditor();
		~ScriptGraphEditor();
		ScriptGraphEditor(const ScriptGraphEditor&) = delete;
		ScriptGraphEditor& operator=(const ScriptGraphEditor&) = delete;

		// Draws the graph. `definition` is the object the script belongs to; its variables are
		// what Read/Write Property nodes and "Promote to Variable" work with.
		void Display(Script& script, SceneObjectDefinition* definition, ScriptPinId& pinToTrigger, bool isRunning);

		ScriptEditorSelection& GetSelection() { return myState.selection; }

	private:
		struct State
		{
			ScriptEditorSelection selection = {};
			ax::NodeEditor::EditorContext* nodeEditorContext = nullptr;
			ScriptPinId inProgressLinkPin = { ScriptPinId::InvalidId };
			ScriptNodeId hoveredNode = { ScriptNodeId::InvalidId };
			std::shared_ptr<MoveNodesCommand> inProgressMove;

			ScriptNodeId pendingFocusNode = { ScriptNodeId::InvalidId }; // select and frame this node next frame
			std::vector<ScriptNodeId> pendingSelect;                     // select these next frame (just created)
			bool showIssues = false;                                     // the compile results list is open
			std::unordered_map<unsigned int, float> pinCanvasY;          // where each pin was drawn this frame
			ScriptNodeId editingComment = { ScriptNodeId::InvalidId };
			ScriptPinId contextPin = { ScriptPinId::InvalidId };         // pin the right-click menu is about
			ScriptPinId dragPin = { ScriptPinId::InvalidId };            // pin a wire was dragged off into empty space
			bool searchJustOpened = false;
			// Where the node editor last had each node. A node is only moved into place when the
			// script says something else (undo, paste, first draw): moving it every frame would
			// undo any drag before the library could apply it.
			std::unordered_map<unsigned int, std::pair<float, float>> lastKnownNodePos;
		} myState;
	};

} // namespace Ag