#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <tge/math/ScaleRotationTranslation.h>
#include <tge/animation/Pose.h>

#include <string_view>

namespace Tga
{
	struct Skeleton;


	struct Animation
	{
		std::vector<LocalSpacePose> frames;

		// The animation length in frames.
		unsigned int length;

		// The number of framer per second
		float framesPerSecond;

		// The animation duration in seconds.
		float duration;

		std::string name;
	};

	// Animation loading is not provided by Core, but it's possible to register a function
	// so that animation code can load animations without needing a dependency on mesh and animation loading
	void RegisterGetAnimationFunction(std::shared_ptr<const Animation>(*aGetFunction)(std::string_view, const std::shared_ptr<const Skeleton>&));
	std::shared_ptr<const Animation> GetAnimation(std::string_view someFilePath, const std::shared_ptr<const Skeleton>& aSkeleton);
}

