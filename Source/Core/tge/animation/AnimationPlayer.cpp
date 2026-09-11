#include "stdafx.h"
#include "AnimationPlayer.h"
#include <tge/animation/AnimationClip.h>
#include <algorithm>

using namespace Tga;

void AnimationPlayer::Init(const std::shared_ptr<const Animation>& animation)
{
	myAnimation = animation;
	myFPS = animation->framesPerSecond;
}

void AnimationPlayer::Update(float aDeltaTime)
{
	if (myState == AnimationState::Playing)
	{
		myTime += aDeltaTime;

		if (myTime >= GetAnimation()->duration)
		{
			if (myIsLooping)
			{
				while (myTime >= GetAnimation()->duration)
					myTime -= GetAnimation()->duration;
			}
			else
			{
				myTime = GetAnimation()->duration;
				myState = AnimationState::Finished;
			}
		}
	}
	UpdatePose();

}

void AnimationPlayer::Update(const AnimationClip& aClip, float aDeltaTime)
{
	float delta = aDeltaTime * aClip.playbackRate;

	if (aClip.endTime <= aClip.startTime)
	{
		// Start and end time are set up wrong, can't play properly, just show start time
		myTime = aClip.startTime;
	}
	else
	{
		// first adjust time to correct range, to handle time being set to a value outside the valid range
		if (myTime < aClip.startTime)
		{
			myTime = aClip.startTime;
		}

		if (myTime > aClip.endTime)
		{
			myTime = aClip.endTime;
		}

		if (myState == AnimationState::Playing)
		{
			myTime += delta;

			if (aClip.isLooping)
			{
				if (delta < 0.f)
				{
					while (myTime < aClip.startTime)
					{
						myTime += aClip.endTime - aClip.startTime;
					}
				}
				else
				{
					while (myTime > aClip.endTime)
					{
						myTime -= aClip.endTime - aClip.startTime;
					}
				}
			}
			else
			{
				if (myTime < aClip.startTime)
				{
					myTime = aClip.startTime;
					myState = AnimationState::Finished;
				}

				if (myTime > aClip.endTime)
				{
					myTime = aClip.endTime;
					myState = AnimationState::Finished;
				}
			}
		}
	}

	UpdatePose();
}


void Tga::AnimationPlayer::UpdatePose()
{
	const float frameRate = 1.0f / myFPS;
	const float result = myTime / frameRate;
	const unsigned int length = GetAnimation()->length;

	unsigned int frame = static_cast<size_t>(std::floor(result));// Which frame we're on
	const float delta = result - static_cast<float>(frame); // How far we have progressed to the next frame.

	unsigned int nextFrame = frame + 1;

	// TODO: This is not handled when working with Animation Clips or manually setting time
	// Do we need to somehow manually set a loop point? 
	if (nextFrame > GetAnimation()->length && myIsLooping)
		nextFrame = 0;

	frame = std::max(0u, std::min(length-1, frame));
	nextFrame = std::max(0u, std::min(length-1, nextFrame));

	int jointCount = (int)myAnimation->frames[frame].count;

	// Update all animations
	for (size_t i = 0; i < jointCount; i++)
	{
		const auto& currentFrameJointXform = myAnimation->frames[frame].jointTransforms[i];
		ScaleRotationTranslationf jointXform = currentFrameJointXform;
		if (myIsInterpolating)
		{
			const ScaleRotationTranslationf& nextFrameJointXform = myAnimation->frames[nextFrame].jointTransforms[i];

			jointXform = ScaleRotationTranslationf::Lerp(currentFrameJointXform, nextFrameJointXform, delta);
		}

		myLocalSpacePose.jointTransforms[i] = jointXform;
	}
	myLocalSpacePose.count = jointCount;
}
