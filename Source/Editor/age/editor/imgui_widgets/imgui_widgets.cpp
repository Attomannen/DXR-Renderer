#include <age/editor/imgui_widgets/imgui_widgets.h>

#include <filesystem>

#include <age/math/vector.h>

namespace fs = std::filesystem;

Ag::InspectorSection::InspectorSection(const char* aName, bool aDefaultOpen, const char* aTooltip)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
	if (aDefaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
	myOpen = ImGui::TreeNodeEx(aName, flags);
	if (aTooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("%s", aTooltip);
}

Ag::InspectorSection::~InspectorSection()
{
	if (myOpen) ImGui::TreePop();
}

bool Ag::BeginInspectorPropertyTable(const char* aId)
{
	return ImGui::BeginTable(aId, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV);
}

void Ag::InspectorPropertyLabel(const char* aLabel, const char* aTooltip)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(aLabel);
	if (aTooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", aTooltip);
}

void Ag::InspectorPropertyValue()
{
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-1);
}

void Ag::EndInspectorPropertyTable() { ImGui::EndTable(); }

bool Ag::InspectorResetButton(const char* aId, const char* aTooltip)
{
	const bool pressed = ImGui::SmallButton(aId);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", aTooltip);
	return pressed;
}

void Ag::InspectorWarning(const char* aMessage)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.20f, 1.0f));
	ImGui::TextWrapped("! %s", aMessage);
	ImGui::PopStyleColor();
}

bool Ag::DrawVec3Control(const char* label, float* values, float resetValue, float columnWidth)
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

Ag::AssetListItemStatus Ag::AssetListItem(fs::path anAssetPath, bool isSelected, std::string_view anIcon, const float aThumbSize)
{
	std::string filename = anAssetPath.filename().string();
	Ag::AssetListItemStatus result;

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
Ag::AssetListItemStatus Ag::AssetListItem(fs::path anAssetPath, bool isSelected, std::string_view anIcon, ImTextureID textureID, const float aThumbSize)
{
	std::string filename = anAssetPath.filename().string();

	Ag::AssetListItemStatus result;
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

Ag::AssetListItemStatus Ag::AssetGridItem(fs::path anAssetPath, bool isSelected, std::string_view anIcon, ImTextureID textureID, float aTileSize)
{
	const std::string filename = anAssetPath.filename().string();
	Ag::AssetListItemStatus result;

	ImGui::BeginGroup();
	ImGui::PushID(filename.c_str());

	// A fixed-size Selectable (not SpanAllColumns/width-0 like AssetListItem)
	// is what makes this tileable with SameLine() -- the row-oriented variant
	// always claims the rest of the row regardless of its own thumb size.
	result.selectedAfter = ImGui::Selectable("##tile", isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(aTileSize, aTileSize + ImGui::GetTextLineHeight() * 2.f));
	result.hovered = ImGui::IsItemHovered();
	result.contextClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
	result.clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	result.doubleClicked = result.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		const std::string path = anAssetPath.string();
		const std::string ext = anAssetPath.filename().extension().string();
		ImGui::SetDragDropPayload(ext.c_str(), path.c_str(), path.size() + 1);
		ImGui::Text("%s", filename.c_str());
		ImGui::EndDragDropSource();
	}

	// Everything is drawn straight onto the draw list on top of the (otherwise blank) selectable:
	// a square preview area with the thumbnail or a big icon, and the name centred underneath.
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 tileMin = ImGui::GetItemRectMin();
	const ImVec2 tileMax = ImGui::GetItemRectMax();
	const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);

	const float previewPadding = 6.f;
	const ImVec2 previewMin(tileMin.x + previewPadding, tileMin.y + previewPadding);
	const ImVec2 previewMax(tileMax.x - previewPadding, tileMin.y + aTileSize - previewPadding);
	drawList->AddRectFilled(previewMin, previewMax, IM_COL32(255, 255, 255, 12), 6.f);

	if (textureID != 0)
	{
		const float imgSize = aTileSize - previewPadding * 2.f - 8.f;
		const ImVec2 imgMin(tileMin.x + (aTileSize - imgSize) * 0.5f, tileMin.y + (aTileSize - imgSize) * 0.5f);
		drawList->AddImage(textureID, imgMin, ImVec2(imgMin.x + imgSize, imgMin.y + imgSize));
	}
	else
	{
		// The icon font glyph scaled to the tile, so a bigger tile means a bigger icon.
		ImFont* font = ImGui::GetFont();
		const float iconSize = aTileSize * 0.42f;
		const ImVec2 iconExtent = font->CalcTextSizeA(iconSize, FLT_MAX, 0.f, anIcon.data());
		drawList->AddText(font, iconSize,
			ImVec2(tileMin.x + (aTileSize - iconExtent.x) * 0.5f, tileMin.y + (aTileSize - iconExtent.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text, 0.8f), anIcon.data());
	}

	const float labelWrap = aTileSize - 8.f;
	const ImVec2 labelExtent = ImGui::CalcTextSize(filename.c_str(), nullptr, false, labelWrap);
	drawList->PushClipRect(ImVec2(tileMin.x, tileMin.y + aTileSize), tileMax, true);
	drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
		ImVec2(tileMin.x + (aTileSize - labelExtent.x) * 0.5f, tileMin.y + aTileSize + 1.f),
		textColor, filename.c_str(), nullptr, labelWrap);
	drawList->PopClipRect();

	ImGui::PopID();
	ImGui::EndGroup();
	return result;
}

void Ag::NodeEditorPinIcon(ImU32 aColor, bool aConnected, float aRadius)
{
	const float size = aRadius * 2.f;
	const ImVec2 topLeft = ImGui::GetCursorScreenPos();
	// Dummy reserves the layout space (so callers can SameLine() before/after
	// this like any inline widget) without needing an interactive item.
	ImGui::Dummy(ImVec2(size, size));
	const ImVec2 center(topLeft.x + aRadius, topLeft.y + aRadius);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	if (aConnected)
		drawList->AddCircleFilled(center, aRadius, aColor);
	else
		drawList->AddCircle(center, aRadius, aColor, 0, 1.5f);
}
