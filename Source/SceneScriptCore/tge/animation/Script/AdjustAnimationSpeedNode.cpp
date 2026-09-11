#include <stdafx.h>
#include "AdjustAnimationSpeedNode.h"

#include <tge/animation/AnimationClip.h>
#include <tge/animation/PoseGenerator.h>
#include <tge/scene/ScenePropertyTypes.h>
#include <tge/script/BaseProperties.h>

using namespace Tga;

void AdjustAnimationSpeedGenerator::GeneratePose(PoseGenerationContext& context, LocalSpacePose& outputPose)
{
	if (generator == nullptr)
	{
		PoseGenerator::GeneratePose(context, outputPose);
		return;
	}

	PoseGenerationContext contextCopy = context;
	contextCopy.deltaTime *= speedScale;

	generator->GeneratePose(contextCopy, outputPose);

}

void AdjustAnimationSpeedGenerator::GenerateRootMotionDelta(PoseGenerationContext& context, Vector3f& outRootMotionPositionDelta, Quatf& outRootMotionRotationDelta)
{
	if (generator == nullptr)
	{
		PoseGenerator::GenerateRootMotionDelta(context, outRootMotionPositionDelta, outRootMotionRotationDelta);
		return;
	}

	PoseGenerationContext contextCopy = context;
	contextCopy.deltaTime /= speedScale;

	generator->GenerateRootMotionDelta(contextCopy, outRootMotionPositionDelta, outRootMotionRotationDelta);
}

void AdjustAnimationSpeedNode::Init(const ScriptCreationContext& context)
{
	{
		ScriptPin valuePin = {};
		valuePin.type = ScriptLinkType::Property;
		valuePin.dataType = GetPropertyType<PoseAndMotion>();
		valuePin.defaultValue = Property::Create<PoseAndMotion>();
		valuePin.name = "Pose"_tgaid;
		valuePin.node = context.GetNodeId();
		valuePin.role = ScriptPinRole::Input;

		myPoseInPin = context.FindOrCreatePin(valuePin);
	}

	{
		ScriptPin valuePin = {};
		valuePin.type = ScriptLinkType::Property;
		valuePin.dataType = GetPropertyType<float>();
		valuePin.defaultValue = Property::Create<float>(1.f);
		valuePin.name = "Rate Scale"_tgaid;
		valuePin.node = context.GetNodeId();
		valuePin.role = ScriptPinRole::Input;

		myRateScalePin = context.FindOrCreatePin(valuePin);
	}

	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<PoseAndMotion>();
		outputPin.name = "Pose"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;

		myPoseOutPin = context.FindOrCreatePin(outputPin);
	}
}



Property AdjustAnimationSpeedNode::ReadPin(ScriptExecutionContext& context, ScriptPinId) const
{
	AdjustAnimationSpeedGenerator& generator = GetRuntimeData(context).generator;

	PoseAndMotion input = *context.ReadInputPin(myPoseInPin).Get<PoseAndMotion>();

	float rateScale = *context.ReadInputPin(myRateScalePin).Get<float>();

	generator.generator = input.poseGenerator;
	generator.speedScale = rateScale;
	
	PoseAndMotion result = {};

	result.poseGenerator = &generator;

	result.desiredSyncedPlaybackRate = input.desiredSyncedPlaybackRate * rateScale;
	result.desiredSyncedPlaybackRateWeight = input.desiredSyncedPlaybackRateWeight;
	
	return Property::Create<PoseAndMotion>(result);
}