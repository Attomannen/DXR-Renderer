#include "stdafx.h"
#include "ImGuiPropertyEditor.h"
#include <imgui\imgui.h>
#include <IconFontHeaders\IconsLucide.h>

#include <algorithm>
#include <cctype>

static bool locFirstRowColumn0 = false;
static bool locFirstRowColumn1 = false;

void Tga::PropertyEditor::HelpMarker(const char* aDescription, bool aSameLine)
{
	if (aSameLine)
		ImGui::SameLine();

	ImGui::TextDisabled(ICON_LC_CIRCLE_HELP);
	if (ImGui::BeginItemTooltip())
	{
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(aDescription);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

bool Tga::PropertyEditor::PropertyHeader(const char* aHeaderName)
{
	// todo: color has to match colors, in ImGuiInterface, should expose them instead and use here

	// CollapsingHeader and selected tree items unfortunately share color in ImGui, overriding color to make the header less highlighted
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{ 0.16f, 0.16f, 0.16f, 1.0f });
	bool result = ImGui::CollapsingHeader(aHeaderName, ImGuiTreeNodeFlags_DefaultOpen);
	ImGui::PopStyleColor();

	return result;
}

bool Tga::PropertyEditor::BeginPropertyTable()
{
	locFirstRowColumn0 = true;
	locFirstRowColumn1 = true;
	return ImGui::BeginTable("##Properties", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable);
}

void Tga::PropertyEditor::EndPropertyTable()
{
	ImGui::EndTable();
}

void Tga::PropertyEditor::PropertyLabel(bool aHideSeparator)
{

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);

	if (locFirstRowColumn0)
	{
		locFirstRowColumn0 = false;
	}
	else if (!aHideSeparator)
	{
		ImGui::Separator();
	}
	ImGui::AlignTextToFramePadding();
}

void Tga::PropertyEditor::PropertyValue(bool aHideSeparator)
{
	ImGui::TableSetColumnIndex(1);
	if (locFirstRowColumn1)
	{
		locFirstRowColumn1 = false;
	}
	else if (!aHideSeparator)
	{
		ImGui::Separator();
	}
	ImGui::PushItemWidth(-1);
}
namespace
{
	Tga::PropertyEditor::AssetListFunction locAssetListFunction = nullptr;

	std::string SlashesForward(std::string path)
	{
		std::replace(path.begin(), path.end(), '\\', '/');
		return path;
	}

	std::string Lowercase(std::string text)
	{
		for (char& c : text)
			c = (char)std::tolower((unsigned char)c);
		return text;
	}
}

void Tga::PropertyEditor::RegisterAssetListFunction(AssetListFunction function)
{
	locAssetListFunction = function;
}

bool Tga::PropertyEditor::AssetField(const char* id, StringId& value, std::initializer_list<const char*> extensions, const char* emptyLabel)
{
	static std::vector<std::string> assets;
	static char filter[64] = "";

	bool changed = false;
	ImGui::PushID(id);

	const bool hasValue = !value.IsEmpty();
	const float clearWidth = hasValue ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.f;

	// The file name is what you recognise; the whole path is in the tooltip.
	std::string label = emptyLabel;
	if (hasValue)
	{
		const std::string path = SlashesForward(value.GetString());
		const size_t slash = path.find_last_of('/');
		label = slash == std::string::npos ? path : path.substr(slash + 1);
	}
	label = std::string(ICON_LC_SEARCH "  ") + label + "###field";

	ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f));
	if (!hasValue)
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	if (ImGui::Button(label.c_str(), ImVec2(-clearWidth, 0.f)))
	{
		ImGui::OpenPopup("##picker");
		filter[0] = '\0';
		assets.clear();
		if (locAssetListFunction)
			locAssetListFunction(std::span<const char* const>(extensions.begin(), extensions.size()), assets);
		std::sort(assets.begin(), assets.end());
	}
	if (!hasValue)
		ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	if (hasValue && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("%s", value.GetString());

	// Drop from the Content Browser: the payload is named after the file's extension.
	if (ImGui::BeginDragDropTarget())
	{
		for (const char* extension : extensions)
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(extension))
			{
				value = StringRegistry::RegisterOrGetString(static_cast<const char*>(payload->Data));
				changed = true;
				break;
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (hasValue)
	{
		ImGui::SameLine();
		if (ImGui::Button(ICON_LC_X "##clear", ImVec2(ImGui::GetFrameHeight(), 0.f)))
		{
			value = {};
			changed = true;
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Clear");
	}

	if (ImGui::BeginPopup("##picker"))
	{
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(320.f);
		ImGui::InputTextWithHint("##search", ICON_LC_SEARCH " Search", filter, sizeof(filter));
		const std::string needle = Lowercase(filter);

		if (ImGui::BeginChild("##list", ImVec2(320.f, 260.f)))
		{
			if (ImGui::Selectable("None", !hasValue))
			{
				value = {};
				changed = true;
				ImGui::CloseCurrentPopup();
			}

			const std::string current = hasValue ? SlashesForward(value.GetString()) : std::string();
			int shown = 0;
			for (const std::string& asset : assets)
			{
				const std::string path = SlashesForward(asset);
				if (!needle.empty() && Lowercase(path).find(needle) == std::string::npos)
					continue;
				++shown;

				ImGui::PushID(shown);
				const size_t slash = path.find_last_of('/');
				const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
				if (ImGui::Selectable(name.c_str(), path == current))
				{
					value = StringRegistry::RegisterOrGetString(asset);
					changed = true;
					ImGui::CloseCurrentPopup();
				}
				if (slash != std::string::npos)
				{
					ImGui::SameLine();
					ImGui::TextDisabled("%s", path.substr(0, slash).c_str());
				}
				ImGui::PopID();
			}
			if (shown == 0)
				ImGui::TextDisabled(assets.empty() ? "No matching assets in the project" : "No asset matches");
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}

	ImGui::PopID();
	return changed;
}
