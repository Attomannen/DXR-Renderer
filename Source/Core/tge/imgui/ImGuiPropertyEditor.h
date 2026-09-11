#pragma once

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

	}
}
