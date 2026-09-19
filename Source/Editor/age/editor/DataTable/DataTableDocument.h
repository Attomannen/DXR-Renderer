#pragma once

#include <age/editor/Document/Document.h>
#include <age/editor/CommandManager/AbstractCommand.h>
#include <age/data/DataTable.h>

#include <string>

namespace Ag
{
	// A spreadsheet-style editor for a .csv data table: click a cell to edit it, add and remove rows and columns.
	class DataTableDocument : public Document
	{
	public:
		void Init(std::string_view path) override;
		void Update(float aTimeDelta, InputManager& inputManager) override;
		void Save() override;
		void OnAction(CommandManager::Action action) override;

		// For ChangeDataTableCommand's Execute() and Undo().
		void SetTable(const DataTable& aTable) { myTable = aTable; }

	private:
		void DrawToolbar();
		void DrawGrid();

		DataTable myTable;
		DataTable myUndoSnapshot;
		bool myHasPendingEdit = false;
		bool myChangedThisFrame = false;

		std::string myName;
		std::string myResolvedPath;
	};

	class ChangeDataTableCommand : public AbstractCommand
	{
	public:
		ChangeDataTableCommand(DataTableDocument& aDocument, const DataTable& aNewValue, const DataTable& anOldValue)
			: myDocument(&aDocument), myNewValue(aNewValue), myOldValue(anOldValue) {}

		void Execute() override;
		void Undo() override;
		const char* GetName() const override { return "Edit Data Table"; }

	private:
		DataTableDocument* myDocument;
		DataTable myNewValue;
		DataTable myOldValue;
	};
}
