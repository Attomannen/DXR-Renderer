#pragma once

#include <span>
#include <cstdint>

#include <age/editor/Commands/SceneCommandBase.h>
#include <age/scene/Scene.h>

namespace Ag
{
	class RemoveSceneObjectsCommand : public SceneCommandBase
	{
	public:
		void AddObjects(std::span<const uint32_t>);

		void Execute() override;
		void Undo() override;

		std::span<const std::pair<uint32_t, std::shared_ptr<SceneObject>>> GetObjects() const;

		void GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const override;

	private:
		std::vector<std::pair<uint32_t, std::shared_ptr<SceneObject>>> myObjects;
	};
}
