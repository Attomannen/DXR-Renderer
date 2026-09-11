#include <tge/editor/imgui_widgets/imgui_widgets.h>

#include <filesystem>

#include <tge/math/vector.h>

namespace fs = std::filesystem;

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