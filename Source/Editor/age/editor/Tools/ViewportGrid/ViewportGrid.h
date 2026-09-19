#pragma once

#include <vector>
#include <age/math/Vector.h>
#include <age/math/color.h>

namespace Ag
{
	class ViewportGrid
	{
	public:
		ViewportGrid();
		// aCameraForward: current view direction, used to fade out whichever
		// axis line the camera is looking nearly along -- see DrawViewportGrid's
		// definition for why.
		void DrawViewportGrid(const Vector3f& aCameraForward);

		void SetGridLineExtreme(const float);
		void SetGridCellIncrement(const int);

	private:
		void MakeGrid();

	private:
		// Metres. Was 1000 when world units were centimetres, i.e. a 10 m grid.
		float myLineExtreme = 10.f;
		// One cell per metre. This was 100 (= 1 m in centimetres); left at 100
		// after the unit change it drew 100 m cells, which is what made an
		// imported 1 m cube look like a centimetre.
		int myIncrement = 1;
		uint16_t myNumLines;

		std::vector<Ag::Color> myColors;
		std::vector<Ag::Vector3f> myFrom;
		std::vector<Ag::Vector3f> myTo;
	};
}