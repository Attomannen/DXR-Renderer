#include <stdafx.h>

#include "Animation.h"

#include <age/log/Log.h>

using namespace Ag;

std::shared_ptr<const Animation>(*locGetAnimationFunction)(std::string_view, const std::shared_ptr<const Skeleton>&);

void Ag::RegisterGetAnimationFunction(std::shared_ptr<const Animation>(*aGetFunction)(std::string_view, const std::shared_ptr<const Skeleton>&))
{
	locGetAnimationFunction = aGetFunction;
}

std::shared_ptr<const Animation> Ag::GetAnimation(std::string_view someFilePath, const std::shared_ptr<const Skeleton>& aSkeleton)
{
	if (locGetAnimationFunction == nullptr)
	{
		// todo: need error loading in Core somehow!
		ERROR_PRINT("Trying to load an animation without registering an animation loader. You need to register a loader with RegisterGetAnimationFunction to use Animations");

		return nullptr;
	}

	return locGetAnimationFunction(someFilePath, aSkeleton);
}