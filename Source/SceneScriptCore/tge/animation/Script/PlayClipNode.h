#include <tge/animation/PoseGenerator.h>
#include <tge/script/ScriptCommon.h>
#include <tge/script/ScriptNodeBase.h>

#include "tge/animation/AnimationPlayer.h"

namespace Tga
{
	struct PlayClipGenerator : public PoseGenerator
	{
		uint32_t lastUpdatedFrame = (uint32_t)-1;
		AnimationClip* clip = nullptr;
		AnimationPlayer animationPlayer;

		float lastSyncLocation = 0.f;

		bool EnsureLoadedAndUpdated(PoseGenerationContext& context);
		void GeneratePose(PoseGenerationContext& context, LocalSpacePose& outputPose) override;
		void GenerateRootMotionDelta(PoseGenerationContext& context, Vector3f& outRootMotionPositionDelta, Quatf& outRootMotionRotationDelta) override;
	};

	struct PlayClipRuntimeInstance
	{
		PlayClipGenerator generator;
	};

	class PlayClipNode : public ScriptNodeWithRuntimeData<PlayClipRuntimeInstance>
	{
		ScriptPinId myPoseOutPin;
		ScriptPinId myAnimationClipInPin;

	public:
		void Init(const ScriptCreationContext& context) override;

		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override;
	};
}
