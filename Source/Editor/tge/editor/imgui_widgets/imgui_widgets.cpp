#include <tge/editor/imgui_widgets/imgui_widgets.h>

#include <filesystem>

#include <tge/math/vector.h>

namespace fs = std::filesystem;

Tga::InspectorSection::InspectorSection(const char* aName, bool aDefaultOpen, const char* aTooltip)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
	if (aDefaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
	myOpen = ImGui::TreeNodeEx(aName, flags);
	if (aTooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("%s", aTooltip);
}

Tga::InspectorSection::~InspectorSection()
{
	if (myOpen) ImGui::TreePop();
}

bool Tga::BeginInspectorPropertyTable(const char* aId)
{
	return ImGui::BeginTable(aId, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV);
}

void Tga::InspectorPropertyLabel(const char* aLabel, const char* aTooltip)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(aLabel);
	if (aTooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", aTooltip);
}

void Tga::InspectorPropertyValue()
{
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-1);
}

void Tga::EndInspectorPropertyTable() { ImGui::EndTable(); }

bool Tga::InspectorResetButton(const char* aId, const char* aTooltip)
{
	const bool pressed = ImGui::SmallButton(aId);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", aTooltip);
	return pressed;
}

void Tga::InspectorWarning(const char* aMessage)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.20f, 1.0f));
	ImGui::TextWrapped("! %s", aMessage);
	ImGui::PopStyleColor();
}

bool Tga::DrawVec3Control(const char* label, float* values, float resetValue, float columnWidth)
{
	(void)columnWidth; // width is derived from CalcItemWidth() below instead
	bool modified = false;
	ImGuiIO& io = ImGui::GetIO();
	auto boldFont = io.Fonts->Fonts[0];

	ImGui::PushID(label);

	// Instead of columns, we can just push widths or use a small table.
	// Since it's often used inside a property table, we will just format it nicely.
	// PushMultiItemsWidths is an imgui_internal.h helper; reproduce its per-item
	// width split (total width minus inner spacing between items, divided evenly)
	// without pulling in the internal header, then push it three times to match
	// the three PopItemWidth calls below (one per X/Y/Z field).
	{
		const float fullWidth = ImGui::CalcItemWidth();
		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		const float itemWidth = (fullWidth - spacing * 2.0f) / 3.0f;
		for (int i = 0; i < 3; ++i) ImGui::PushItemWidth(itemWidth);
	}
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

	float lineHeight = ImGui::GetFont()->FontSize + ImGui::GetStyle().FramePadding.y * 2.0f;
	ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

	// X Coordinate
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
	ImGui::PushFont(boldFont);
	if (ImGui::Button("X", buttonSize)) { values[0] = resetValue; modified = true; }
	ImGui::PopFont();
	ImGui::PopStyleColor(3);

	ImGui::SameLine();
	if (ImGui::DragFloat("##X", &values[0], 0.1f, 0.0f, 0.0f, "%.2f")) modified = true;
	ImGui::PopItemWidth();
	ImGui::SameLine();

	// Y Coordinate
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
	ImGui::PushFont(boldFont);
	if (ImGui::Button("Y", buttonSize)) { values[1] = resetValue; modified = true; }
	ImGui::PopFont();
	ImGui::PopStyleColor(3);

	ImGui::SameLine();
	if (ImGui::DragFloat("##Y", &values[1], 0.1f, 0.0f, 0.0f, "%.2f")) modified = true;
	ImGui::PopItemWidth();
	ImGui::SameLine();

	// Z Coordinate
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
	ImGui::PushFont(boldFont);
	if (ImGui::Button("Z", buttonSize)) { values[2] = resetValue; modified = true; }
	ImGui::PopFont();
	ImGui::PopStyleColor(3);

	ImGui::SameLine();
	if (ImGui::DragFloat("##Z", &values[2], 0.1f, 0.0f, 0.0f, "%.2f")) modified = true;
	ImGui::PopItemWidth();

	ImGui::PopStyleVar();
	ImGui::PopID();

	return modified;
}

static void DrawAssetListItem(const char* label, bool isSelected, std::string_view anIcon, const float /*& aThumbSize*/)
{
	ImGui::Selectable("##", isSelected);
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::Text(anIcon.data());

	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::Text(label);
}

static void DrawAssetListItem(const char* label, bool isSelected, ImTextureID textureID, const float& aThumbSize)
{
	if (textureID != 0)
	{
		ImGui::Selectable("##", isSelected, ImGuiSelectableFlags_None, ImVec2(0, aThumbSize));
		ImGui::SameLine();
		ImGui::Image(textureID, ImVec2(aThumbSize, aThumbSize));
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::Text(label);
	}
}

Tga::AssetListItemStatus Tga::AssetListItem(fs::path anAssetPath, bool isSelected, std::string_view anIcon, const float aThumbSize)
{
	std::string filename = anAssetPath.filename().string();
	Tga::AssetListItemStatus result;

	result.selectedAfter = ImGui::Selectable(("##" + filename).c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, aThumbSize));
	result.hovered = ImGui::IsItemHovered();
	result.contextClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
		std::string path = anAssetPath.string();
		std::string ext = anAssetPath.filename().extension().string();

		ImGui::SetDragDropPayload(ext.c_str(), path.c_str(), path.size() + 1);
		DrawAssetListItem(filename.c_str(), false, anIcon, aThumbSize);

		ImGui::EndDragDropSource();
	}
	result.clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	result.doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::Text(anIcon.data());

	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::Text(filename.c_str());

	return result;
}
Tga::AssetListItemStatus Tga::AssetListItem(fs::path anAssetPath, bool isSelected, std::string_view anIcon, ImTextureID textureID, const float aThumbSize)
{
	std::string filename = anAssetPath.filename().string();

	Tga::AssetListItemStatus result;
	result.selectedAfter = ImGui::Selectable(("##" + filename).c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, aThumbSize));
	result.hovered = ImGui::IsItemHovered();
	result.contextClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
		std::string path = anAssetPath.string();
		std::string ext = anAssetPath.filename().extension().string();

		ImGui::SetDragDropPayload(ext.c_str(), path.c_str(), path.size() + 1);
		DrawAssetListItem(filename.c_str(), false, textureID, aThumbSize);

		ImGui::EndDragDropSource();
	}
	result.clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	result.doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

	ImGui::SameLine();
	ImGui::Image(textureID, ImVec2(aThumbSize, aThumbSize));
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::Text(anIcon.data());

	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::Text(filename.c_str());

	return result;
}
