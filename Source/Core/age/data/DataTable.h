#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Ag
{
	// A data table, like Unreal's Data Table: rows of named values, kept in a plain CSV file so it can be edited in
	// any spreadsheet as well as in the editor. The first row holds the column names. The first column holds each
	// row's name, which is how scripts look a row up.
	//
	//   Name,Damage,Cost
	//   Sword,12,40
	//   Bow,7,55
	//
	// Every cell is text; the script nodes read it as a float, int, bool, string or vector on demand. When reading, a
	// semicolon or tab is accepted as the separator too (spreadsheets in some languages save with semicolons).
	class DataTable
	{
	public:
		std::vector<std::string> columns;
		std::vector<std::vector<std::string>> rows;   // each row has exactly columns.size() cells

		static DataTable FromCsv(std::string_view aText);
		std::string ToCsv() const;

		bool Load(const std::string& aFilePath);
		bool Save(const std::string& aFilePath) const;

		size_t RowCount() const { return rows.size(); }
		size_t ColumnCount() const { return columns.size(); }
		int FindColumn(std::string_view aName) const;   // -1 when there is no such column
		int FindRow(std::string_view aName) const;      // matches the first column; -1 when there is no such row
		const std::string& Cell(int aRow, int aColumn) const;   // empty when out of range

		// Makes every row as wide as the header, and keeps at least a name column.
		void Normalise();
	};

	// A table by asset path (e.g. "Data/Items.csv"), read once and read again when the file changes on disk.
	// Null when the file does not exist.
	std::shared_ptr<const DataTable> GetDataTable(const std::string& anAssetPath);
}
