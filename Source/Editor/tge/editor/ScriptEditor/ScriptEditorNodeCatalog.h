#pragma once

#include <string>
#include <vector>

#include <tge/script/ScriptCommon.h>

namespace Tga
{
	struct NodeCatalogPin
	{
		bool isInput = false;
		ScriptLinkType linkType = ScriptLinkType::Unknown;
		const PropertyTypeBase* dataType = nullptr;
		std::string name;
	};

	// Every node type with what it looks like inside, built once by creating a scratch node
	// of each type. Lets the add-node search show which nodes fit a pin.
	struct NodeCatalogEntry
	{
		ScriptNodeTypeId type = { ScriptNodeTypeId::InvalidId };
		std::string shortName;
		std::string fullName;
		std::string lowerName; // lower-case full name, for matching
		std::string tooltip;
		std::vector<NodeCatalogPin> pins; // inputs first, then outputs
	};

	// Restricts the search to nodes that have a pin a dragged wire could end on.
	struct NodePinFilter
	{
		bool active = false;
		bool wantInput = false; // the new node needs an input pin (the wire started on an output)
		ScriptLinkType linkType = ScriptLinkType::Unknown;
		const PropertyTypeBase* dataType = nullptr;
	};

	const std::vector<NodeCatalogEntry>& GetNodeCatalog();

	// Index into entry.pins of the first pin that fits the filter, or -1.
	int FindCompatiblePin(const NodeCatalogEntry& entry, const NodePinFilter& filter);

	// Search box and result list for a popup. Returns the chosen node type (or an invalid id).
	// outPinIndex is the matching pin when a filter is active. outShowTree is true while the
	// box is empty and no filter is set: the caller then shows the category menu instead.
	ScriptNodeTypeId DrawNodeSearch(bool justOpened, const NodePinFilter& filter, int& outPinIndex, bool& outShowTree);
}
