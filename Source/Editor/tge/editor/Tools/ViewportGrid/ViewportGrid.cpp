#include <tge/editor/Tools/ViewportGrid/ViewportGrid.h>

#include <tge/Application.h>
#include <tge/editor/Editor.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Tga;

ViewportGrid::ViewportGrid()
{
	MakeGrid();
}

void ViewportGrid::MakeGrid()
{
	myNumLines = 3 + (uint16_t)((4 * myLineExtreme) / myIncrement);
	myColors.resize(myNumLines);
	myFrom.resize(myNumLines);
	myTo.resize(myNumLines);

	auto& colors = myColors;
	auto& from = myFrom;
	auto& to = myTo;
	auto& end = myLineExtreme;

	colors[0] = {1.f, 0.f, 0.f, 1.f};
	colors[1] = {0.f, 1.f, 0.f, 1.f};
	colors[2] = {0.f, 0.f, 1.f, 1.f};
	from[0] = {-end, 0.f, 0.f};
	from[1] = {0.f, -end, 0.f};
	from[2] = {0.f, 0.f, -end};
	to[0] = {end, 0.f, 0.f};
	to[1] = {0.f, end, 0.f};
	to[2] = {0.f, 0.f, end};

	size_t iterations = (myNumLines - 3) / 4;

	for (size_t i = 0; i < iterations; i += 1)
	{
		float offset = (float)i * myIncrement;
		size_t idx = 4*i + 3;

		// along x-axis
		colors[idx] = { .5f, .5f, .5f, 0.1f };
		from[idx] = { offset, 0.f, -end };
		to[idx] = { offset, 0.f, end};

		colors[idx+1] = { .5f, .5f, .5f, 0.1f };
		from[idx+1] = { -offset, 0.f, -end };
		to[idx+1] = { -offset, 0.f, end};

		// along z-axis
		colors[idx+2] = { .5f, .5f, .5f, 0.1f };
		from[idx+2] = { -end, 0.f, offset };
		to[idx+2] = { end, 0.f, offset};

		colors[idx+3] = { .5f, .5f, .5f, 0.1f };
		from[idx+3] = { -end, 0.f, -offset };
		to[idx+3] = { end, 0.f, -offset};
	}

}

void ViewportGrid::SetGridLineExtreme(const float anExtreme)
{
	myLineExtreme = anExtreme;
	MakeGrid();
}

void ViewportGrid::SetGridCellIncrement(const int anIncrement)
{
	myIncrement = anIncrement;
	MakeGrid();
}

void ViewportGrid::DrawViewportGrid(const Vector3f& aCameraForward)
{
	if (myNumLines == 0)
	{
		return;
	}

	// The X/Y/Z reference lines (indices 0-2) run through the origin at a
	// fixed length regardless of view direction. Looking nearly along one
	// of them puts the camera almost parallel to that line, so perspective
	// stretches its projection across most of the screen instead of it
	// reading as a short axis marker -- fade that axis out as the view
	// direction approaches it, back to full strength once it doesn't.
	static const Vector3f kAxisDirs[3] = { {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f} };
	constexpr float kFadeStartDot = 0.85f;   // |dot| beyond which the fade begins
	for (int i = 0; i < 3; ++i)
	{
		float absDot = std::abs(aCameraForward.Dot(kAxisDirs[i]));
		float fade = 1.f - std::clamp((absDot - kFadeStartDot) / (1.f - kFadeStartDot), 0.f, 1.f);
		myColors[i].a = fade;
	}

	Editor::GetEditor()->GetEditorGraphics().DrawLines(myColors.data(), myFrom.data(), myTo.data(), myNumLines);
}
