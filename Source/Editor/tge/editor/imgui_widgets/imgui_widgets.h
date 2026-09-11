#pragma once

#include <imgui.h>
#include <filesystem>

#include <tge/math/vector.h>

namespace fs = std::filesystem;

namespace Tga
{
	struct AssetListItemStatus
	{
		bool selectedAfter = false;
		bool clicked = false;
		bool doubleClicked = false;
		bool hovered = false;
	};

	extern AssetListItemStatus AssetListItem(fs::path anAssetPath, bool isSelected, std::string_view anIcon, ImTextureID textureID, const float aThumbSize = 32.f);
	extern AssetListItemStatus AssetListItem(fs::path anAssetPath, bool isSelected, std::string_view anIcon, const float aThumbSize = 20.f);
}