#include <tge/animation/Pose.h>

#include <tge/animation/Skeleton.h>
#include <tge/script/ScriptCommon.h>
#include <tge/script/ScriptNodeBase.h>

#include "tge/animation/PoseGenerator.h"

namespace Tga
{
	struct BlendPoseGenerator : public PoseGenerator
	{
		uint32_t lastUpdatedFrame = (uint32_t)-1;
		PoseGenerator* generatorA;
		PoseGenerator* generatorB;
		float blendFactor;

		void GeneratePose(PoseGenerationContext& context, LocalSpacePose& outputPose) override;
		void GenerateRootMotionDelta(PoseGenerationContext& context, Vector3f& outRootMotionPositionDelta, Quatf& outRootMotionRotationDelta) override;
	};

	struct BlendPoseRuntimeInstance
	{
		BlendPoseGenerator generator;
	};

	class BlendPoseNode : public ScriptNodeWithRuntimeData<BlendPoseRuntimeInstance>
	{
		ScriptPinId myPoseAInPin;
		ScriptPinId myPoseBInPin;
		ScriptPinId myBlendAmountPin;

		ScriptPinId myPoseOutPin;

	public:
		void Init(const ScriptCreationContext& context) override;
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override;
	};
}
