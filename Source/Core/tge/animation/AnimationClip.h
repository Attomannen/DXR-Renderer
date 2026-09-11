#pragma once

#include <tge/script/CopyOnWriteWrapper.h>
#include <tge/stringRegistry/StringRegistry.h>

namespace Tga
{
	struct AnimationClip
	{
		StringId animationSourcePath;
		StringId previewModelPath;
		
		float startTime;
		float endTime;

		float playbackRate = 1.f;

		bool isLooping;

		bool isSyncronized;
		float cycleOffsetPercentage;
		float cycleCount = 1.f;
	};

	AnimationClip* GetAnimationClip(StringId path);
	AnimationClip* GetOrCreateAnimationClip(StringId path);
	void SaveAnimationClip(StringId path);
}