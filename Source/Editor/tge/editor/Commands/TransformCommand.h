#pragma once

#include <tge/editor/Commands/SceneCommandBase.h>
#include <tge/scene/Scene.h>
#include <span>

namespace Tga
{
	class TransformCommand : public SceneCommandBase
	{
	public:
		TransformCommand() = default;

		void Begin(const std::span<const uint32_t>& someObjectIds);
		void End();

		void Execute() override;
		void Undo() override;

		bool IsEmpty() const { return myFromTRS.empty(); }

		void GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const override;

	private:
		std::vector < std::pair<uint32_t, TRS>> myFromTRS;
		std::vector < std::pair<uint32_t, TRS>> myToTRS;

		//Matrix4x4f myFromTransform;
		//Matrix4x4f myToTransform;
	};
}
