#include <stdafx.h>
#include "DataTable.h"

#include <age/settings/settings.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace Ag
{
	namespace
	{
		// The separator a file uses: whichever of , ; tab is most common in its first line, outside quotes.
		char DetectSeparator(std::string_view aText)
		{
			int commas = 0, semicolons = 0, tabs = 0;
			bool quoted = false;
			for (const char c : aText)
			{
				if (c == '"') quoted = !quoted;
				else if (c == '\n' && !quoted) break;
				else if (!quoted)
				{
					if (c == ',') ++commas;
					else if (c == ';') ++semicolons;
					else if (c == '\t') ++tabs;
				}
			}
			if (semicolons > commas && semicolons >= tabs) return ';';
			if (tabs > commas && tabs > semicolons) return '\t';
			return ',';
		}

		std::string Escape(const std::string& aCell)
		{
			if (aCell.find_first_of(",\"\n\r") == std::string::npos && (aCell.empty() || (aCell.front() != ' ' && aCell.back() != ' ')))
				return aCell;
			std::string result = "\"";
			for (const char c : aCell)
			{
				if (c == '"') result += '"';
				result += c;
			}
			result += '"';
			return result;
		}
	}

	DataTable DataTable::FromCsv(std::string_view aText)
	{
		// A byte order mark, as Excel writes at the start of a UTF-8 file.
		if (aText.size() >= 3 && aText.substr(0, 3) == "\xEF\xBB\xBF")
			aText.remove_prefix(3);

		const char separator = DetectSeparator(aText);
		std::vector<std::vector<std::string>> records;
		std::vector<std::string> record;
		std::string field;
		bool quoted = false;
		bool anyContent = false;   // something (even an empty quoted field) has been seen on this line

		const auto endField = [&]()
		{
			record.push_back(std::move(field));
			field.clear();
		};
		const auto endRecord = [&]()
		{
			if (anyContent || !record.empty())
			{
				endField();
				records.push_back(std::move(record));
				record.clear();
			}
			anyContent = false;
		};

		for (size_t i = 0; i < aText.size(); ++i)
		{
			const char c = aText[i];
			if (quoted)
			{
				if (c == '"')
				{
					if (i + 1 < aText.size() && aText[i + 1] == '"') { field += '"'; ++i; }
					else quoted = false;
				}
				else field += c;
			}
			else if (c == '"') { quoted = true; anyContent = true; }
			else if (c == separator) { endField(); anyContent = true; }
			else if (c == '\r') { /* the \n that follows ends the line */ }
			else if (c == '\n') endRecord();
			else { field += c; anyContent = true; }
		}
		endRecord();

		DataTable table;
		if (!records.empty())
		{
			table.columns = std::move(records.front());
			table.rows.assign(std::make_move_iterator(records.begin() + 1), std::make_move_iterator(records.end()));
		}
		table.Normalise();
		return table;
	}

	std::string DataTable::ToCsv() const
	{
		std::string text;
		const auto writeRow = [&](const std::vector<std::string>& row)
		{
			for (size_t c = 0; c < row.size(); ++c)
			{
				if (c > 0) text += ',';
				text += Escape(row[c]);
			}
			text += "\n";
		};
		writeRow(columns);
		for (const std::vector<std::string>& row : rows)
			writeRow(row);
		return text;
	}

	bool DataTable::Load(const std::string& aFilePath)
	{
		std::ifstream in(aFilePath, std::ios::binary);
		if (!in.is_open())
			return false;
		std::stringstream buffer;
		buffer << in.rdbuf();
		*this = FromCsv(buffer.str());
		return true;
	}

	bool DataTable::Save(const std::string& aFilePath) const
	{
		std::ofstream out(aFilePath, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
			return false;
		out << ToCsv();
		return true;
	}

	int DataTable::FindColumn(std::string_view aName) const
	{
		for (size_t c = 0; c < columns.size(); ++c)
			if (columns[c] == aName)
				return (int)c;
		return -1;
	}

	int DataTable::FindRow(std::string_view aName) const
	{
		for (size_t r = 0; r < rows.size(); ++r)
			if (!rows[r].empty() && rows[r][0] == aName)
				return (int)r;
		return -1;
	}

	const std::string& DataTable::Cell(int aRow, int aColumn) const
	{
		static const std::string kEmpty;
		if (aRow < 0 || aColumn < 0 || (size_t)aRow >= rows.size() || (size_t)aColumn >= rows[aRow].size())
			return kEmpty;
		return rows[aRow][aColumn];
	}

	void DataTable::Normalise()
	{
		if (columns.empty())
			columns.push_back("Name");
		for (std::vector<std::string>& row : rows)
			row.resize(columns.size());
	}

	std::shared_ptr<const DataTable> GetDataTable(const std::string& anAssetPath)
	{
		struct Entry
		{
			std::shared_ptr<const DataTable> table;
			std::filesystem::file_time_type writeTime;
			std::chrono::steady_clock::time_point lastCheck;
		};
		static std::unordered_map<std::string, Entry> cache;
		static std::mutex mutex;
		std::lock_guard<std::mutex> lock(mutex);

		const auto now = std::chrono::steady_clock::now();
		auto it = cache.find(anAssetPath);
		// A script may read the same table many times a frame; look at the disk once a second at most.
		if (it != cache.end() && now - it->second.lastCheck < std::chrono::seconds(1))
			return it->second.table;

		const std::string resolved = Settings::ResolveAssetPath(anAssetPath);
		std::error_code error;
		if (resolved.empty() || !std::filesystem::exists(resolved, error))
		{
			cache.erase(anAssetPath);
			return nullptr;
		}

		const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(resolved, error);
		if (it != cache.end() && it->second.writeTime == writeTime)
		{
			it->second.lastCheck = now;
			return it->second.table;
		}

		auto table = std::make_shared<DataTable>();
		if (!table->Load(resolved))
			return nullptr;
		Entry& entry = cache[anAssetPath];
		entry.table = table;
		entry.writeTime = writeTime;
		entry.lastCheck = now;
		return entry.table;
	}
}
