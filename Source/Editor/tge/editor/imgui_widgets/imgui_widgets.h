#pragma once

#include <imgui.h>
#include <filesystem>

#include <tge/math/vector.h>

namespace fs = std::filesystem;

namespace Tga
{
	// Shared, deliberately small inspector vocabulary.  Editor panels should use
	// these rather than inventing their own spacing and foldout behaviour.
	// ImGui persists TreeNode open state in its .ini file, so foldouts survive
	// restarts without a second persistence system.
	struct InspectorSection
	{
		InspectorSection(const char* aName, bool aDefaultOpen = true, const char* aTooltip = nullptr);
		~InspectorSection();
		bool IsOpen() const { return myOpen; }
	private:
		bool myOpen = false;
	};

	// Starts an aligned Label | Control table.  Use PropertyLabel/PropertyValue
	// for each row and EndPropertyTable when finished.
	bool BeginInspectorPropertyTable(const char* aId = "InspectorProperties");
	void InspectorPropertyLabel(const char* aLabel, const char* aTooltip = nullptr);
	void InspectorPropertyValue();
	void EndInspectorPropertyTable();
	bool InspectorResetButton(const char* aId, const char* aTooltip = "Restore this value to its default");
	void InspectorWarning(const char* aMessage);
	
	// A Unity-style vector3 property drawer with colored X/Y/Z reset buttons.
	bool DrawVec3Control(const char* label, float* values, float resetValue = 0.0f, float columnWidth = 100.0f);

	struct AssetListItemStatus
	{
		bool selectedAfter = false;
		bool clicked = false;
		bool doubleClicked = false;
		bool hovered = false;
		// Captured while the Selectable is still the last item. Callers can
		// render icons/thumbnails afterwards without losing the context target.
		bool contextClicked = false;
	};

	extern AssetListItemStatus AssetListItem(fs::path anAssetPath, bool isSelected, std::string_view anIcon, ImTextureID textureID, const float aThumbSize = 32.f);
	extern AssetListItemStatus AssetListItem(fs::path anAssetPath, bool isSelected, std::string_view anIcon, const float aThumbSize = 20.f);
}
