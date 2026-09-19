#include <age/animation/Pose.h>

#include <age/animation/Skeleton.h>
#include <age/script/ScriptCommon.h>
#include <age/script/ScriptNodeBase.h>

#include "age/animation/PoseGenerator.h"

namespace Ag
{

	struct AdjustAnimationSpeedGenerator : public PoseGenerator
	{
		uint32_t lastUpdatedFrame = (uint32_t)-1;
		PoseGenerator* generator;
		float speedScale;

		void GeneratePose(PoseGenerationContext& context, LocalSpacePose& outputPose) override;

		void GenerateRootMotionDelta(PoseGenerationContext& context, Vector3f& outRootMotionPositionDelta, Quatf& outRootMotionRotationDelta) override;
	};

	struct AdjustAnimationSpeedRuntimeInstance
	{
		AdjustAnimationSpeedGenerator generator;
	};


	class AdjustAnimationSpeedNode : public ScriptNodeWithRuntimeData<AdjustAnimationSpeedRuntimeInstance>
	{
		ScriptPinId myPoseInPin;
		ScriptPinId myRateScalePin;

		ScriptPinId myPoseOutPin;

	public:
		void Init(const ScriptCreationContext& context) override;
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override;
	};
}
