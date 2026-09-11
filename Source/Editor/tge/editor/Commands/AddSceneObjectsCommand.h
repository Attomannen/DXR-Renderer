#pragma once

#include <span>

#include <tge/editor/Commands/SceneCommandBase.h>
#include <tge/scene/Scene.h>

namespace Tga
{
	class AddSceneObjectsCommand : public SceneCommandBase
	{
	public:
		void AddObjects(std::span<std::shared_ptr<SceneObject>> someObjects);

		void Execute() override;
		void Undo() override;

		std::span<const std::pair<uint32_t, std::shared_ptr<SceneObject>>> GetObjects() const;

		void GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const override;

	private:
		std::vector<std::pair<uint32_t, std::shared_ptr<SceneObject>>> myObjects;
	};
}
