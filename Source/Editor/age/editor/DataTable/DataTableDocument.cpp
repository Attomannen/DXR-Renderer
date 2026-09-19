#include "stdafx.h"
#include <age/editor/DataTable/DataTableDocument.h>

#include <age/editor/Editor.h>
#include <age/editor/CommandManager/CommandManager.h>
#include <age/settings/settings.h>

#include <imgui.h>
#include <IconFontHeaders/IconsLucide.h>

#include <filesystem>

using namespace Ag;

void ChangeDataTableCommand::Execute()
{
	// The undo stack outlives documents, so the document may be gone.
	if (Editor::GetEditor()->IsDocumentOpen(myDocument))
		myDocument->SetTable(myNewValue);
}

void ChangeDataTableCommand::Undo()
{
	if (Editor::GetEditor()->IsDocumentOpen(myDocument))
		myDocument->SetTable(myOldValue);
}

void DataTableDocument::Init(std::string_view aPath)
{
	Document::Init(aPath);

	myName = std::filesystem::path(aPath).filename().string();

	// The Content Browser hands over a path relative to the asset root; a new asset arrives absolute.
	const std::string resolved = Ag::Settings::ResolveAssetPath(aPath);
	myResolvedPath = resolved.empty() ? std::string(aPath) : resolved;
	if (!myTable.Load(myResolvedPath))
		myTable.Normalise();

	char buffer[512];
	sprintf_s(buffer, "%s###Document:%s", myName.c_str(), std::string(aPath).c_str());
	myImGuiName = StringRegistry::RegisterOrGetString(buffer);
}

void DataTableDocument::Save()
{
	if (myTable.Save(myResolvedPath))
		mySaveUndoStackSize = myUndoStackSize;
}

void DataTableDocument::OnAction(CommandManager::Action action)
{
	if (action == CommandManager::Action::Do)
	{
		if (myUndoStackSize < mySaveUndoStackSize)
			mySaveUndoStackSize = -1;
		myUndoStackSize++;
	}
	else if (action == CommandManager::Action::PostRedo)
	{
		myUndoStackSize++;
	}
	else if (action == CommandManager::Action::PostUndo)
	{
		myUndoStackSize--;
	}
	else if (action == CommandManager::Action::Clear)
	{
		myUndoStackSize = 0;
	}
}

void DataTableDocument::Update(float aTimeDelta, InputManager& inputManager)
{
	(void)aTimeDelta;
	(void)inputManager;

	char buffer[512];
	char asterix[2] = { 0, 0 };
	if (mySaveUndoStackSize != myUndoStackSize)
		asterix[0] = '*';
	sprintf_s(buffer, "%s%s###Document:%s", myName.c_str(), asterix, myPath.c_str());

	ImGui::SetNextWindowClass(Editor::GetEditor()->GetDocumentWindowClass());
	ImGui::SetNextWindowDockID(Editor::GetEditor()->GetDocumentDockSpaceId(), ImGuiCond_Once);

	bool open = true;
	ImGui::Begin(buffer, &open);
	if (myState == Document::State::Open && !open)
		myState = Document::State::CloseRequested;

	// Edits are batched into one undo step per edit session.
	if (!myHasPendingEdit)
		myUndoSnapshot = myTable;
	myChangedThisFrame = false;

	DrawToolbar();
	DrawGrid();

	if (myChangedThisFrame)
		myHasPendingEdit = true;
	if (myHasPendingEdit && !ImGui::IsAnyItemActive())
	{
		CommandManager::DoCommand(std::make_shared<ChangeDataTableCommand>(*this, myTable, myUndoSnapshot));
		myHasPendingEdit = false;
	}
	ImGui::End();
}

void DataTableDocument::DrawToolbar()
{
	if (ImGui::Button(ICON_LC_SAVE " Save"))
		Save();
	ImGui::SameLine();
	if (ImGui::Button(ICON_LC_PLUS " Row"))
	{
		myTable.rows.emplace_back(myTable.columns.size());
		myChangedThisFrame = true;
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_LC_PLUS " Column"))
	{
		myTable.columns.push_back("Column " + std::to_string(myTable.columns.size()));
		myTable.Normalise();
		myChangedThisFrame = true;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%zu rows, %zu columns.  The first column names each row; scripts look rows up by that name.",
		myTable.RowCount(), myTable.ColumnCount());
	ImGui::Separator();
}

void DataTableDocument::DrawGrid()
{
	const int columns = (int)myTable.ColumnCount();
	const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY
		| ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
	if (!ImGui::BeginTable("##grid", columns + 1, flags))
		return;

	ImGui::TableSetupScrollFreeze(1, 1);
	ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.f);
	for (int c = 0; c < columns; ++c)
		ImGui::TableSetupColumn(("##col" + std::to_string(c)).c_str(), ImGuiTableColumnFlags_WidthFixed, 140.f);

	// The header row: every column name is editable, and a right-click deletes the column.
	ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
	ImGui::TableSetColumnIndex(0);
	ImGui::TextDisabled("#");
	int deleteColumn = -1;
	for (int c = 0; c < columns; ++c)
	{
		ImGui::TableSetColumnIndex(c + 1);
		ImGui::PushID(c);
		char text[128];
		strncpy_s(text, myTable.columns[c].c_str(), _TRUNCATE);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputText("##name", text, IM_ARRAYSIZE(text)))
		{
			myTable.columns[c] = text;
			myChangedThisFrame = true;
		}
		if (ImGui::BeginPopupContextItem("ColumnContext"))
		{
			if (ImGui::MenuItem(ICON_LC_TRASH_2 "  Delete Column", nullptr, false, columns > 1))
				deleteColumn = c;
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}

	int deleteRow = -1, duplicateRow = -1;
	for (int r = 0; r < (int)myTable.RowCount(); ++r)
	{
		ImGui::TableNextRow();
		ImGui::PushID(1000 + r);
		ImGui::TableSetColumnIndex(0);
		ImGui::Selectable(std::to_string(r).c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
		if (ImGui::BeginPopupContextItem("RowContext"))
		{
			if (ImGui::MenuItem(ICON_LC_COPY "  Duplicate Row"))
				duplicateRow = r;
			if (ImGui::MenuItem(ICON_LC_TRASH_2 "  Delete Row"))
				deleteRow = r;
			ImGui::EndPopup();
		}
		for (int c = 0; c < columns; ++c)
		{
			ImGui::TableSetColumnIndex(c + 1);
			ImGui::PushID(c);
			char text[512];
			strncpy_s(text, myTable.rows[r][c].c_str(), _TRUNCATE);
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputText("##cell", text, IM_ARRAYSIZE(text)))
			{
				myTable.rows[r][c] = text;
				myChangedThisFrame = true;
			}
			ImGui::PopID();
		}
		ImGui::PopID();
	}
	ImGui::EndTable();

	if (deleteColumn >= 0)
	{
		myTable.columns.erase(myTable.columns.begin() + deleteColumn);
		for (std::vector<std::string>& row : myTable.rows)
			row.erase(row.begin() + deleteColumn);
		myChangedThisFrame = true;
	}
	if (duplicateRow >= 0)
	{
		std::vector<std::string> copy = myTable.rows[duplicateRow];
		myTable.rows.insert(myTable.rows.begin() + duplicateRow + 1, std::move(copy));
		myChangedThisFrame = true;
	}
	if (deleteRow >= 0)
	{
		myTable.rows.erase(myTable.rows.begin() + deleteRow);
		myChangedThisFrame = true;
	}
}
