#pragma once
#include <vector>
#include <string>

#include <age/math/color.h>

#define DEBUG_PERFGRAPH_SAMPLES 500
namespace Ag
{
	class Text;
	class CustomShape2D;
	class DebugDrawer;
	class PerformanceGraph
	{
	public:
		PerformanceGraph(DebugDrawer* aDrawer);
		~PerformanceGraph(void);
		void Init(Ag::Color& aBackgroundColor, Ag::Color& aLineColor, const std::string& aText);
		void Render();

		void FeedValue(int aValue);
	private:
		std::unique_ptr<CustomShape2D> myBackground;
		std::vector<int> myBuffer;
		DebugDrawer* myDrawer;
		Ag::Color myLineColor;
		std::unique_ptr<Text> myText;

	};
}
