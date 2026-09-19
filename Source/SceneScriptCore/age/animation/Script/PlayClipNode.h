#include <age/animation/PoseGenerator.h>
#include <age/script/ScriptCommon.h>
#include <age/script/ScriptNodeBase.h>

#include "age/animation/AnimationPlayer.h"

namespace Ag
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
