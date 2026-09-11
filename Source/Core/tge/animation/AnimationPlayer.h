#pragma once
#include <vector>
#include <string>
#include <memory>
#include <tge/animation/animation.h>
#include <tge/animation/pose.h>

namespace Tga
{
	struct AnimationClip;

	enum class AnimationState : uint8_t
	{
		NoAnimation,
		NotStarted,
		Playing,
		Stopped,
		Paused,
		Finished,
	};

	/// <summary>
	/// Plays an Animation on a Model. Holds the active state of the animation and the resulting pose.
	/// </summary>
	class AnimationPlayer
	{
	private:
		std::shared_ptr<const Animation> myAnimation = nullptr;
		float myTime = 0;

		bool myIsLooping = false;
		bool myIsInterpolating = true;
		float myFPS = 0.f;

		AnimationState myState = AnimationState::NotStarted;

		LocalSpacePose myLocalSpacePose;

	public:
		void Init(const std::shared_ptr<const Animation>& animation);

		/**
		 * Updates the animation and writes a new pose.
		 */
		void Update(float aDeltaTime);

		/**
		 * Updates the animation and writes a new pose. Uses an Animation Clip to control playback parameters
		 */
		void Update(const AnimationClip& clip, float aDeltaTime);

		/**
		 * Updates the pose and without steppting forward the animation.
		 */
		void UpdatePose();
		
		/**
		 * Instructs the animation to play.
		 */
		void Play()
		{
			myState = AnimationState::Playing;
		}

		/**
		 * Pauses the animation at the current frame.
		 */
		void Pause()
		{
			myState = AnimationState::Paused;
		}

		/**
		 * Stops and rewinds the current animation to the beginning.
		 */
		void Stop()
		{
			myState = AnimationState::Stopped;
			myTime = 0;
		}

		AnimationState GetState()
		{
			return myState;
		}

		bool GetIsLooping() const { return myIsLooping; }
		void SetIsLooping(bool shouldLoop) { myIsLooping = shouldLoop; }

		bool GetIsInterpolating() const { return myIsInterpolating; }
		void SetIsInterpolating(bool shouldInterpolate) { myIsInterpolating = shouldInterpolate; }

		float GetFramesPerSecond() const { return myFPS; }
		void SetFramesPerSecond(float someFPS)
		{
			myFPS = someFPS;
		}

		float GetTime() const { return myTime; }
		void SetTime(float time)
		{
			myTime = time;
		}

		void SetFrame(unsigned int frame)
		{
			myTime = frame / myFPS;
		}

		const LocalSpacePose& GetLocalSpacePose() const { return myLocalSpacePose; }
		std::shared_ptr<const Animation> GetAnimation() const { return myAnimation; }
		AnimationState GetState() const { return myState; }
		
		unsigned int GetFrame() { return static_cast<unsigned int>(myTime * myFPS); }

		bool IsValid() const { return myAnimation ? true : false; }
	};
}

