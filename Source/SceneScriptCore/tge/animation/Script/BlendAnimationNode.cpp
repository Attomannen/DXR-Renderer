#include <stdafx.h>
#include "BlendAnimationNode.h"

#include <tge/animation/AnimationClip.h>
#include <tge/animation/PoseGenerator.h>
#include <tge/log/Log.h>

#include <tge/scene/ScenePropertyTypes.h>
#include <tge/script/BaseProperties.h>

using namespace Tga;

void BlendPoseGenerator::GeneratePose(PoseGenerationContext& context, LocalSpacePose& outputPose)
{
	if (generatorA == nullptr || generatorB == nullptr)
	{
		// if one generator is set, could return that one instead...
		PoseGenerator::GeneratePose(context, outputPose);

		return;
	}

	LocalSpacePose poseA;
	LocalSpacePose poseB;

	generatorA->GeneratePose(context, poseA);
	generatorB->GeneratePose(context, poseB);

	if (poseA.count != poseB.count)
	{
		ERROR_PRINT("Pose mismatch");

		PoseGenerator::GeneratePose(context, outputPose);

		return;
	}

	for (int i = 0; i < poseA.count; i++)
	{
		const ScaleRotationTranslationf& srtA = poseA.jointTransforms[i];
		const ScaleRotationTranslationf& srtB = poseB.jointTransforms[i];
		ScaleRotationTranslationf& srtOut = outputPose.jointTransforms[i];

		srtOut.SetScale(Vector3f::Lerp(srtA.GetScale(), srtB.GetScale(), blendFactor));
		srtOut.SetRotation(Quatf::Slerp(srtA.GetRotation(), srtB.GetRotation(), blendFactor));
		srtOut.SetTranslation(Vector3f::Lerp(srtA.GetTranslation(), srtB.GetTranslation(), blendFactor));
	}
}

void BlendPoseGenerator::GenerateRootMotionDelta(PoseGenerationContext& context, Vector3f& outRootMotionPositionDelta, Quatf& outRootMotionRotationDelta)
{
	if (generatorA == nullptr || generatorB == nullptr)
	{
		// if one generator is set, could return that one instead...

		PoseGenerator::GenerateRootMotionDelta(context, outRootMotionPositionDelta, outRootMotionRotationDelta);

		return;
	}

	Vector3f translationA;
	Vector3f translationB;
	Quatf rotationA;
	Quatf rotationB;

	generatorA->GenerateRootMotionDelta(context, translationA, rotationA);
	generatorB->GenerateRootMotionDelta(context, translationB, rotationB);

	outRootMotionRotationDelta = Quatf::Slerp(rotationA, rotationB, blendFactor);
	outRootMotionPositionDelta = Vector3f::Lerp(translationA, translationB, blendFactor);
}


void BlendPoseNode::Init(const ScriptCreationContext& context)
{
	{
		ScriptPin valuePin = {};
		valuePin.type = ScriptLinkType::Property;
		valuePin.dataType = GetPropertyType<PoseAndMotion>();
		valuePin.defaultValue = Property::Create<PoseAndMotion>();
		valuePin.name = "Pose A"_tgaid;
		valuePin.node = context.GetNodeId();
		valuePin.role = ScriptPinRole::Input;

		myPoseAInPin = context.FindOrCreatePin(valuePin);
	}

	{
		ScriptPin valuePin = {};
		valuePin.type = ScriptLinkType::Property;
		valuePin.dataType = GetPropertyType<PoseAndMotion>();
		valuePin.defaultValue = Property::Create<PoseAndMotion>();
		valuePin.name = "Pose B"_tgaid;
		valuePin.node = context.GetNodeId();
		valuePin.role = ScriptPinRole::Input;

		myPoseBInPin = context.FindOrCreatePin(valuePin);
	}

	{
		ScriptPin valuePin = {};
		valuePin.type = ScriptLinkType::Property;
		valuePin.dataType = GetPropertyType<float>();
		valuePin.defaultValue = Property::Create<float>();
		valuePin.name = "Blend Amount"_tgaid;
		valuePin.node = context.GetNodeId();
		valuePin.role = ScriptPinRole::Input;

		myBlendAmountPin = context.FindOrCreatePin(valuePin);
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

Property BlendPoseNode::ReadPin(ScriptExecutionContext& context, ScriptPinId) const
{
	BlendPoseGenerator& generator = GetRuntimeData(context).generator;

	PoseAndMotion inputA = *context.ReadInputPin(myPoseAInPin).Get<PoseAndMotion>();
	PoseAndMotion inputB = *context.ReadInputPin(myPoseBInPin).Get<PoseAndMotion>();

	float blendFactor = *context.ReadInputPin(myBlendAmountPin).Get<float>();


	generator.generatorA = inputA.poseGenerator;
	generator.generatorB = inputB.poseGenerator;
	generator.blendFactor = blendFactor;
	

	PoseAndMotion result = {};

	result.poseGenerator = &generator;
	
	float wrate =
		(1.f - blendFactor) * inputA.desiredSyncedPlaybackRate * inputA.desiredSyncedPlaybackRateWeight
		+ (blendFactor)*inputB.desiredSyncedPlaybackRate * inputB.desiredSyncedPlaybackRateWeight;

	float w =
		(1.f - blendFactor) * inputA.desiredSyncedPlaybackRateWeight
		+ (blendFactor) * inputB.desiredSyncedPlaybackRateWeight;

	if (w > 0.f)
	{
		result.desiredSyncedPlaybackRate = wrate / w;
		result.desiredSyncedPlaybackRateWeight = w;
	}

	return Property::Create<PoseAndMotion>(result);
}