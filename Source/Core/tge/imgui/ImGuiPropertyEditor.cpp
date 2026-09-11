#include "stdafx.h"
#include "ImGuiPropertyEditor.h"
#include <imgui\imgui.h>
#include <IconFontHeaders\IconsLucide.h>

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