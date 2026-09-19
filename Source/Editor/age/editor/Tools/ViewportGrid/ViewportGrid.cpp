#include <age/editor/Tools/ViewportGrid/ViewportGrid.h>

#include <age/Application.h>
#include <age/editor/Editor.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Ag;

ViewportGrid::ViewportGrid()
{
	MakeGrid();
}

void ViewportGrid::AppendTier(float aSpacing, float aExtent, int aSkipMultiple, const Color& aColor)
{
	if (aSpacing <= 0.f || aExtent <= 0.f) return;
	const int count = (int)(aExtent / aSpacing);
	for (int i = 1; i <= count; ++i)
	{
		// Offsets a coarser tier also draws are left to that tier: two lines on
		// the same spot blend to twice the alpha and break the hierarchy the
		// tiers exist to create.
		if (aSkipMultiple > 1 && (i % aSkipMultiple) == 0) continue;

		const float o = (float)i * aSpacing;
		for (int sign = 0; sign < 2; ++sign)
		{
			const float so = sign ? -o : o;
			myColors.push_back(aColor); myFrom.push_back({ so, 0.f, -aExtent }); myTo.push_back({ so, 0.f, aExtent });
			myColors.push_back(aColor); myFrom.push_back({ -aExtent, 0.f, so }); myTo.push_back({ aExtent, 0.f, so });
		}
	}
}

void ViewportGrid::MakeGrid()
{
	myColors.clear(); myFrom.clear(); myTo.clear();

	const float fine = (float)(myIncrement > 0 ? myIncrement : 1);
	const float end  = myLineExtreme;

	// The three axis markers first: DrawViewportGrid fades them by index 0-2.
	const Color axisColors[3] = { {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}, {0.f, 0.f, 1.f, 1.f} };
	const Vector3f axisEnds[3] = { { end, 0.f, 0.f }, { 0.f, end, 0.f }, { 0.f, 0.f, end } };
	for (int i = 0; i < 3; ++i)
	{
		myColors.push_back(axisColors[i]);
		myFrom.push_back({ -axisEnds[i].x, -axisEnds[i].y, -axisEnds[i].z });
		myTo.push_back(axisEnds[i]);
	}

	// Tier reach is capped so the line count stays roughly constant no matter
	// how far the extent is pushed: the fine tier covers the space you model
	// in, the coarse tiers only give the eye somewhere to put the distance.
	const float fineEnd   = std::min(end, fine * 25.f);
	const float mediumEnd = std::min(end, fine * 250.f);

	AppendTier(fine,          fineEnd,   10, { .45f, .45f, .45f, 0.10f });
	AppendTier(fine * 10.f,   mediumEnd, 10, { .55f, .55f, .55f, 0.16f });
	AppendTier(fine * 100.f,  end,        0, { .60f, .60f, .60f, 0.22f });

	// LineDrawer takes kMaxVerts/2 lines per call and silently drops the rest,
	// so the tier caps above have to keep this well under it.
	myNumLines = (uint16_t)std::min<size_t>(myColors.size(), 1000);
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
