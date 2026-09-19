#pragma once

#include <vector>
#include <age/math/Vector.h>
#include <age/math/color.h>

namespace Ag
{
	// The editor viewport's ground grid.
	//
	// Drawn in three tiers rather than one. A single spacing cannot serve both
	// ends of the range: at one line per metre a grid that reaches far enough to
	// frame a 180 m building needs thousands of lines, and LineDrawer accepts
	// 1000 (kMaxVerts / 2). Tiering spends the budget where it reads -- fine
	// lines only near the origin, progressively coarser further out -- so the
	// grid covers a kilometre in roughly 240 lines.
	class ViewportGrid
	{
	public:
		ViewportGrid();
		// aCameraForward: current view direction, used to fade out whichever
		// axis line the camera is looking nearly along -- see DrawViewportGrid's
		// definition for why.
		void DrawViewportGrid(const Vector3f& aCameraForward);

		// Overall reach, in metres. The tiers are derived from it and from the
		// cell increment, so a small extent (an asset preview) simply produces
		// the fine tier alone.
		void SetGridLineExtreme(const float);
		void SetGridCellIncrement(const int);

	private:
		void MakeGrid();
		// Appends one tier of axis-aligned lines at aSpacing out to aExtent,
		// skipping offsets a coarser tier will draw (aSkipMultiple) so the two
		// never stack on the same line and double its brightness.
		void AppendTier(float aSpacing, float aExtent, int aSkipMultiple, const Color& aColor);

	private:
		// Metres. The fine tier stops well short of this; see MakeGrid.
		float myLineExtreme = 1000.f;
		// Finest cell size, in metres. This was 100 (= 1 m in centimetres); left
		// at 100 after the unit change it drew 100 m cells, which is what made
		// an imported 1 m cube look like a centimetre.
		int myIncrement = 1;
		uint16_t myNumLines = 0;

		std::vector<Ag::Color> myColors;
		std::vector<Ag::Vector3f> myFrom;
		std::vector<Ag::Vector3f> myTo;
	};
}
