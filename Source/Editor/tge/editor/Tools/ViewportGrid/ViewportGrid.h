#pragma once

#include <vector>
#include <tge/math/Vector.h>
#include <tge/math/color.h>

namespace Tga
{
	class ViewportGrid
	{
	public:
		ViewportGrid();
		void DrawViewportGrid();

		void SetGridLineExtreme(const float);
		void SetGridCellIncrement(const int);

	private:
		void MakeGrid();

	private:
		float myLineExtreme = 1000.f;
		int myIncrement = 100;
		uint16_t myNumLines;

		std::vector<Tga::Color> myColors;
		std::vector<Tga::Vector3f> myFrom;
		std::vector<Tga::Vector3f> myTo;
	};
}