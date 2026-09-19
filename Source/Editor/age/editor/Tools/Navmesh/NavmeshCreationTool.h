#pragma once

#include <Recast/Include/Recast.h>

namespace Ag {
	class Scene;
	class Color;

	class NavmeshCreationTool {
	public:
		NavmeshCreationTool();
		~NavmeshCreationTool();
		NavmeshCreationTool(const NavmeshCreationTool&) = delete;

		void Init();
		void DrawUI();
		void DrawNavmesh() const;

	private:
		void BuildNavmesh();
	};
}