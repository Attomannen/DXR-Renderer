#include <stdafx.h>
#include "DataTableNodes.h"

#include "NodeHelpers.h"
#include <age/script/ScriptArrays.h>
#include <age/data/DataTable.h>

#include <algorithm>
#include <cstdlib>
#include <string>

using namespace Ag;
using namespace Ag::NodeHelpers;

namespace
{
	// The cell's numbers in order, whatever separates them: "1 2 3", "(1, 2, 3)".
	Vector3f ParseVector(const std::string& text)
	{
		float values[3] = { 0.f, 0.f, 0.f };
		const char* cursor = text.c_str();
		for (int i = 0; i < 3; ++i)
		{
			while (*cursor && !(*cursor == '-' || *cursor == '+' || *cursor == '.' || (*cursor >= '0' && *cursor <= '9')))
				++cursor;
			if (!*cursor)
				break;
			char* end = nullptr;
			values[i] = (float)std::strtod(cursor, &end);
			if (end == cursor)
				break;
			cursor = end;
		}
		return Vector3f(values[0], values[1], values[2]);
	}

	bool ParseBool(const std::string& text)
	{
		std::string lower = text;
		for (char& c : lower) c = (char)std::tolower((unsigned char)c);
		return lower == "true" || lower == "yes" || lower == "1" || lower == "on";
	}

	template <typename T> T Convert(const std::string& text);
	template <> float Convert<float>(const std::string& text)
	{
		// A spreadsheet in some languages writes 55,5: a comma alone is a decimal point.
		if (text.find(',') != std::string::npos && text.find('.') == std::string::npos)
		{
			std::string fixedText = text;
			std::replace(fixedText.begin(), fixedText.end(), ',', '.');
			return (float)std::atof(fixedText.c_str());
		}
		return (float)std::atof(text.c_str());
	}
	template <> int Convert<int>(const std::string& text) { return std::atoi(text.c_str()); }
	template <> bool Convert<bool>(const std::string& text) { return ParseBool(text); }
	template <> StringId Convert<StringId>(const std::string& text) { return MakeString(text); }
	template <> Vector3f Convert<Vector3f>(const std::string& text) { return ParseVector(text); }

	// Table, Row and Column in; the cell, converted to T, out. A missing table, row or column gives T's default, and Found is false.
	template <typename T>
	class GetCellNode : public ScriptNodeBase
	{
		ScriptPinId myTable, myRow, myColumn, myFound;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myTable = In<StringId>(context, "Table");
			myRow = In<StringId>(context, "Row");
			myColumn = In<StringId>(context, "Column");
			Out<T>(context, "Value");
			myFound = Out<bool>(context, "Found");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			T value{};
			bool found = false;
			if (const auto table = GetDataTable(Read<StringId>(context, myTable).GetString()))
			{
				const int row = table->FindRow(Read<StringId>(context, myRow).GetStringView());
				const int column = table->FindColumn(Read<StringId>(context, myColumn).GetStringView());
				if (row >= 0 && column >= 0)
				{
					value = Convert<T>(table->Cell(row, column));
					found = true;
				}
			}
			if (pin == myFound) return Make<bool>(found);
			return Make<T>(value);
		}
	};

	class HasRowNode : public ScriptNodeBase
	{
		ScriptPinId myTable, myRow;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myTable = In<StringId>(context, "Table");
			myRow = In<StringId>(context, "Row");
			Out<bool>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const auto table = GetDataTable(Read<StringId>(context, myTable).GetString());
			return Make<bool>(table && table->FindRow(Read<StringId>(context, myRow).GetStringView()) >= 0);
		}
	};

	class CountNode : public ScriptNodeBase
	{
		ScriptPinId myTable, myRows, myColumns;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myTable = In<StringId>(context, "Table");
			myRows = Out<int>(context, "Rows");
			myColumns = Out<int>(context, "Columns");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			const auto table = GetDataTable(Read<StringId>(context, myTable).GetString());
			if (pin == myRows) return Make<int>(table ? (int)table->RowCount() : 0);
			return Make<int>(table ? (int)table->ColumnCount() : 0);
		}
	};

	class RowNameNode : public ScriptNodeBase
	{
		ScriptPinId myTable, myIndex;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myTable = In<StringId>(context, "Table");
			myIndex = In<int>(context, "Index", 0);
			Out<StringId>(context, "Row");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const auto table = GetDataTable(Read<StringId>(context, myTable).GetString());
			return Make<StringId>(table ? MakeString(table->Cell(Read<int>(context, myIndex), 0)) : StringId());
		}
	};

	class RowNamesNode : public ScriptNodeBase
	{
		ScriptPinId myTable;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myTable = In<StringId>(context, "Table");
			Out<ArrayValue<StringId>>(context, "Rows");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			ArrayValue<StringId> names = ArrayValue<StringId>::Create();
			if (const auto table = GetDataTable(Read<StringId>(context, myTable).GetString()))
				for (size_t r = 0; r < table->RowCount(); ++r)
					names.Edit().items.push_back(MakeString(table->Cell((int)r, 0)));
			return Make<ArrayValue<StringId>>(names);
		}
	};

	class ColumnNameNode : public ScriptNodeBase
	{
		ScriptPinId myTable, myIndex;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myTable = In<StringId>(context, "Table");
			myIndex = In<int>(context, "Index", 0);
			Out<StringId>(context, "Column");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const auto table = GetDataTable(Read<StringId>(context, myTable).GetString());
			const int index = Read<int>(context, myIndex);
			return Make<StringId>(table && index >= 0 && (size_t)index < table->ColumnCount() ? MakeString(table->columns[index]) : StringId());
		}
	};

	// The name of the first row whose Column holds Value.
	class FindRowNode : public ScriptNodeBase
	{
		ScriptPinId myTable, myColumn, myValue, myFound;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myTable = In<StringId>(context, "Table");
			myColumn = In<StringId>(context, "Column");
			myValue = In<StringId>(context, "Value");
			Out<StringId>(context, "Row");
			myFound = Out<bool>(context, "Found");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			StringId row;
			bool found = false;
			if (const auto table = GetDataTable(Read<StringId>(context, myTable).GetString()))
			{
				const int column = table->FindColumn(Read<StringId>(context, myColumn).GetStringView());
				const std::string_view wanted = Read<StringId>(context, myValue).GetStringView();
				for (size_t r = 0; column >= 0 && r < table->RowCount(); ++r)
				{
					if (table->Cell((int)r, column) == wanted)
					{
						row = MakeString(table->Cell((int)r, 0));
						found = true;
						break;
					}
				}
			}
			if (pin == myFound) return Make<bool>(found);
			return Make<StringId>(row);
		}
	};
}

void Ag::RegisterDataTableNodes()
{
	using R = ScriptNodeTypeRegistry;
	R::RegisterType<GetCellNode<float>>("Data Table/Get Table Float", "The cell at Row and Column as a number. Table is a .csv asset, e.g. Data/Items.csv");
	R::RegisterType<GetCellNode<int>>("Data Table/Get Table Int", "The cell at Row and Column as a whole number");
	R::RegisterType<GetCellNode<bool>>("Data Table/Get Table Bool", "The cell at Row and Column as true or false (true, yes, 1 and on are true)");
	R::RegisterType<GetCellNode<StringId>>("Data Table/Get Table String", "The cell at Row and Column as text");
	R::RegisterType<GetCellNode<Vector3f>>("Data Table/Get Table Float3", "The cell as a vector: three numbers such as \"1 2 3\" or \"(1, 2, 3)\"");
	R::RegisterType<HasRowNode>("Data Table/Table Has Row", "True when the table has a row with this name");
	R::RegisterType<CountNode>("Data Table/Table Size", "How many rows and columns the table has");
	R::RegisterType<RowNameNode>("Data Table/Table Row Name", "The name of the row at Index, counting from 0");
	R::RegisterType<RowNamesNode>("Data Table/Table Row Names", "The names of all the rows, in order");
	R::RegisterType<ColumnNameNode>("Data Table/Table Column Name", "The name of the column at Index, counting from 0");
	R::RegisterType<FindRowNode>("Data Table/Find Table Row", "The name of the first row whose Column holds Value");
}
