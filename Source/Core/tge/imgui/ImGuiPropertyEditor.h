#pragma once

#include <initializer_list>
#include <span>
#include <string>
#include <vector>

#include <tge/stringRegistry/StringRegistry.h>

namespace Tga
{
	namespace PropertyEditor
	{
		extern bool PropertyHeader(const char* aHeaderName);

		extern bool BeginPropertyTable();
		extern void EndPropertyTable();
		extern void PropertyLabel(bool aHideSeparator = false);
		extern void PropertyValue(bool aHideSeparator = false);

		extern void HelpMarker(const char* aDescription, bool aSameLine = true);

		// The editor supplies the assets under the game folder that have one of these extensions,
		// as paths relative to the asset root.
		using AssetListFunction = void (*)(std::span<const char* const> extensions, std::vector<std::string>& outPaths);
		extern void RegisterAssetListFunction(AssetListFunction function);

		// A field for a reference to an asset (a mesh, a material, a texture...). It shows the
		// current asset; clicking opens a searchable list of the assets that fit, dropping one
		// from the Content Browser assigns it, and X clears it. Returns true when value changed.
		// `id` must be unique among the fields drawn in the same place.
		extern bool AssetField(const char* id, StringId& value, std::initializer_list<const char*> extensions, const char* emptyLabel = "None");

	}
}
