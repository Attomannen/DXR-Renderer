#include <stdafx.h>
#include "PlayClipNode.h"

#include <tge/animation/Animation.h>
#include <tge/animation/AnimationClip.h>
#include <tge/animation/AnimationPlayer.h>
#include <tge/animation/PoseGenerator.h>

#include <tge/scene/ScenePropertyTypes.h>
#include <tge/script/contexts/ScriptUpdateContext.h>

using namespace Tga;

bool PlayClipGenerator::EnsureLoadedAndUpdated(PoseGenerationContext& context)
{
	if (!clip)
		return false;

	if (lastUpdatedFrame == (uint32_t)-1)
	{
		std::shared_ptr<const Animation> animation = GetAnimation(clip->animationSourcePath.GetString(), context.skeleton);
		if (animation)
			animationPlayer.Init(animation);
	}

	if (!animationPlayer.GetAnimation())
		return false;

	if (clip->isSyncronized)
	{
		float factor = context.syncedPlaybackTime + clip->cycleOffsetPercentage;
		factor = factor - floor(factor);

		// closest new value for lastSyncLocation, larger than previous value, but with correct factor
		float count = ceil(lastSyncLocation - factor);
		lastSyncLocation = count + factor;

		if (clip->isLooping && clip->cycleCount > 0.f)
		{
			while (lastSyncLocation > clip->cycleCount)
			{
				lastSyncLocation -= clip->cycleCount;
			}
		}
		else
		{
			lastSyncLocation = std::min(lastSyncLocation, clip->cycleCount);
		}

		animationPlayer.SetTime(clip->startTime + (clip->endTime - clip->startTime) * lastSyncLocation / clip->cycleCount);
	}
	else
	{
		if (lastUpdatedFrame == (uint32_t)-1)
		{
			animationPlayer.SetTime(clip->startTime);
		}
		else
		{
			float newTime = animationPlayer.GetTime();

			float delta = clip->playbackRate * context.deltaTime;
			newTime += delta;

			bool playingForward = delta > 0.f;

			if (clip->isLooping && clip->endTime > clip->startTime)
			{
				if (playingForward)
				{
					while (newTime > clip->endTime)
					{
						newTime -= clip->endTime - clip->startTime;
					}
				}
				else
				{
					while (newTime < clip->startTime)
					{
						newTime += clip->endTime - clip->startTime;
					}
				}
			}
			else
			{
				newTime = std::min(newTime, clip->endTime);
				newTime = std::max(newTime, clip->startTime);
			}

			animationPlayer.SetTime(newTime);
		}
	}

	lastUpdatedFrame = context.frameNumber;

	animationPlayer.UpdatePose();

	return true;
}
void PlayClipGenerator::GeneratePose(PoseGenerationContext& context, LocalSpacePose& outputPose)
{
	if (!EnsureLoadedAndUpdated(context))
		return;

	outputPose = animationPlayer.GetLocalSpacePose();
}

void PlayClipGenerator::GenerateRootMotionDelta(PoseGenerationContext& context, Vector3f& outRootMotionPositionDelta, Quatf& outRootMotionRotationDelta)
{
	if (!EnsureLoadedAndUpdated(context))
		return;

	outRootMotionPositionDelta = Vector3f{};
	outRootMotionRotationDelta = Quatf{};
}

void PlayClipNode::Init(const ScriptCreationContext& context)
{
	{
		ScriptPin namePin = {};
		namePin.type = ScriptLinkType::Property;
		namePin.dataType = GetPropertyType<CopyOnWriteWrapper<AnimationClipReference>>();
		namePin.defaultValue = Property::Create<CopyOnWriteWrapper<AnimationClipReference>>();
		namePin.name = "Clip"_tgaid;
		namePin.node = context.GetNodeId();
		namePin.role = ScriptPinRole::Input;
		myAnimationClipInPin = context.FindOrCreatePin(namePin);
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

Property PlayClipNode::ReadPin(ScriptExecutionContext& context, ScriptPinId) const
{
	PlayClipGenerator& generator = GetRuntimeData(context).generator;

	if (generator.clip == nullptr)
	{
		StringId path = context.ReadInputPin(myAnimationClipInPin).Get<CopyOnWriteWrapper<AnimationClipReference>>()->Get().path;
		generator.clip = GetAnimationClip(path);
	}

	PoseAndMotion result = {};
	
	if (generator.clip && generator.clip->isSyncronized)
	{
		result.desiredSyncedPlaybackRateWeight = 1.f;
		result.desiredSyncedPlaybackRate = generator.clip->playbackRate * generator.clip->cycleCount / (generator.clip->endTime - generator.clip->startTime);
	}

	result.poseGenerator = &generator;

	return Property::Create<PoseAndMotion>(result);
}