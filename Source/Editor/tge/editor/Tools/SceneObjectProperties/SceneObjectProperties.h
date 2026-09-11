#pragma once

#include <tge/editor/Tools/ToolsInterface.h>
#include <tge/editor/Commands/TransformCommand.h>

#include <memory>
#include <vector>

namespace Tga
{
	class Scene;
	struct Transform;
	class TransformCommand;

	class SceneObjectProperties
	{
	public:
		SceneObjectProperties() = default;
		~SceneObjectProperties() = default;

		void Draw();

	private:
		TransformCommand myTransformCommand;
		std::vector<uint32_t> myPreviousSelection;
	};
}