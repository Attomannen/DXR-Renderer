#include "stdafx.h"

#include <age/editor/ScriptEditor/ScriptEditorNodeCatalog.h>

#include <algorithm>
#include <cctype>

#include <imgui.h>

#include <age/script/Script.h>
#include <age/script/ScriptNodeBase.h>
#include <age/script/ScriptNodeTypeRegistry.h>

using namespace Ag;

namespace
{
	std::string Lower(std::string_view text)
	{
		std::string result(text);
		for (char& c : result)
			c = (char)std::tolower((unsigned char)c);
		return result;
	}

	void CollectTypes(const ScriptNodeTypeRegistry::CategoryInfo& category, std::vector<ScriptNodeTypeId>& out)
	{
		for (const ScriptNodeTypeRegistry::CategoryInfo& child : category.childCategories)
			CollectTypes(child, out);
		for (ScriptNodeTypeId type : category.nodeTypes)
			out.push_back(type);
	}

	std::vector<NodeCatalogEntry> BuildCatalog()
	{
		std::vector<ScriptNodeTypeId> types;
		CollectTypes(ScriptNodeTypeRegistry::GetRootCategory(), types);

		std::vector<NodeCatalogEntry> catalog;
		for (ScriptNodeTypeId type : types)
		{
			NodeCatalogEntry entry;
			entry.type = type;
			entry.shortName = std::string(ScriptNodeTypeRegistry::GetNodeTypeShortName(type));
			if (entry.shortName == "ERROR") // internal placeholder, never something to add
				continue;
			entry.fullName = std::string(ScriptNodeTypeRegistry::GetNodeTypeFullName(type));
			entry.lowerName = Lower(entry.fullName);
			entry.tooltip = std::string(ScriptNodeTypeRegistry::GetNodeTooltip(type));

			// A scratch node shows what pins the type really creates.
			Script scratch;
			const ScriptNodeId id = scratch.CreateNode(type, ScriptNodeTypeRegistry::CreateNode(type), { 0.f, 0.f });
			ScriptCreationContext context(scratch, id);
			scratch.EditNode(id).Init(context);

			for (int pass = 0; pass < 2; ++pass)
			{
				size_t count = 0;
				const ScriptPinId* pins = pass == 0 ? scratch.GetInputPins(id, count) : scratch.GetOutputPins(id, count);
				for (size_t i = 0; i < count; ++i)
				{
					const ScriptPin& pin = scratch.GetPin(pins[i]);
					NodeCatalogPin catalogPin;
					catalogPin.isInput = pass == 0;
					catalogPin.linkType = pin.type;
					catalogPin.dataType = pin.dataType;
					catalogPin.name = pin.name.GetString();
					entry.pins.push_back(std::move(catalogPin));
				}
			}
			catalog.push_back(std::move(entry));
		}
		return catalog;
	}
}

const std::vector<NodeCatalogEntry>& Ag::GetNodeCatalog()
{
	static const std::vector<NodeCatalogEntry> catalog = BuildCatalog();
	return catalog;
}

int Ag::FindCompatiblePin(const NodeCatalogEntry& entry, const NodePinFilter& filter)
{
	if (!filter.active)
		return -1;
	for (size_t i = 0; i < entry.pins.size(); ++i)
	{
		const NodeCatalogPin& pin = entry.pins[i];
		if (pin.isInput != filter.wantInput || pin.linkType != filter.linkType || pin.dataType != filter.dataType)
			continue;
		return (int)i;
	}
	return -1;
}

ScriptNodeTypeId Ag::DrawNodeSearch(bool justOpened, const NodePinFilter& filter, int& outPinIndex, bool& outShowTree)
{
	static char query[128] = "";
	static int selected = 0;

	outPinIndex = -1;
	outShowTree = false;
	ScriptNodeTypeId result = { ScriptNodeTypeId::InvalidId };

	if (justOpened)
	{
		query[0] = '\0';
		selected = 0;
		ImGui::SetKeyboardFocusHere();
	}

	ImGui::SetNextItemWidth(320.f);
	if (ImGui::InputTextWithHint("##nodesearch", filter.active ? "Search nodes that fit this pin..." : "Search nodes...", query, sizeof(query)))
		selected = 0;

	const bool hasQuery = query[0] != '\0';
	if (!hasQuery && !filter.active)
	{
		outShowTree = true;
		return result;
	}

	// Words must all appear somewhere in the full name (category included). Names that start
	// with the query, then contain it, rank first.
	std::vector<std::string> words;
	{
		std::string lowered = Lower(query);
		size_t start = 0;
		while (start < lowered.size())
		{
			const size_t end = lowered.find(' ', start);
			const size_t stop = end == std::string::npos ? lowered.size() : end;
			if (stop > start)
				words.push_back(lowered.substr(start, stop - start));
			start = stop + 1;
		}
	}

	struct Match { const NodeCatalogEntry* entry; int pin; int score; };
	std::vector<Match> matches;
	const std::string lowerQuery = Lower(query);
	for (const NodeCatalogEntry& entry : GetNodeCatalog())
	{
		int pin = -1;
		if (filter.active)
		{
			pin = FindCompatiblePin(entry, filter);
			if (pin < 0)
				continue;
		}
		bool all = true;
		for (const std::string& word : words)
			all = all && entry.lowerName.find(word) != std::string::npos;
		if (!all)
			continue;

		const std::string lowerShort = Lower(entry.shortName);
		int score = 2;
		if (!words.empty())
		{
			if (lowerShort.rfind(lowerQuery, 0) == 0) score = 0;
			else if (lowerShort.find(lowerQuery) != std::string::npos) score = 1;
		}
		matches.push_back({ &entry, pin, score });
	}
	std::stable_sort(matches.begin(), matches.end(), [](const Match& a, const Match& b)
	{
		return a.score != b.score ? a.score < b.score : a.entry->shortName < b.entry->shortName;
	});

	const int count = (int)matches.size();
	bool moved = false;
	if (count > 0)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { selected = std::min(selected + 1, count - 1); moved = true; }
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { selected = std::max(selected - 1, 0); moved = true; }
		selected = std::clamp(selected, 0, count - 1);
	}

	if (ImGui::BeginChild("##nodesearchresults", ImVec2(320.f, 260.f)))
	{
		if (count == 0)
			ImGui::TextDisabled("No node matches");

		for (int i = 0; i < count; ++i)
		{
			const NodeCatalogEntry& entry = *matches[i].entry;
			ImGui::PushID(i);
			if (ImGui::Selectable(entry.shortName.c_str(), i == selected))
			{
				result = entry.type;
				outPinIndex = matches[i].pin;
			}
			if (i == selected && moved)
				ImGui::SetScrollHereY();
			if (ImGui::IsItemHovered() && !entry.tooltip.empty())
				ImGui::SetTooltip("%s\n%s", entry.fullName.c_str(), entry.tooltip.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", entry.fullName.c_str());
			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	if (count > 0 && result.id == ScriptNodeTypeId::InvalidId && ImGui::IsKeyPressed(ImGuiKey_Enter))
	{
		result = matches[selected].entry->type;
		outPinIndex = matches[selected].pin;
	}
	return result;
}
