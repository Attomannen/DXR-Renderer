#pragma once

#include <age/editor/Tools/ToolsInterface.h>
#include <age/editor/Commands/TransformCommand.h>

#include <memory>
#include <vector>

namespace Ag
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