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
		float myLineExtreme = 1000.f;
		int myIncrement = 100;
		uint16_t myNumLines;

		std::vector<Ag::Color> myColors;
		std::vector<Ag::Vector3f> myFrom;
		std::vector<Ag::Vector3f> myTo;
	};
}