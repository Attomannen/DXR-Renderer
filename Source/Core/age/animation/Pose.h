#pragma once

#include <age/EngineDefines.h>
#include <age/Math/Matrix.h>
#include <age/Math/ScaleRotationTranslation.h>

namespace Ag
{
	/// <summary>
	/// Represents a Pose of a model. Each transform is relative to its parent bone. The parent hierarchy is stored in a Skeleton class.
	/// A pose is only valid for models with the same skeleton as the animation it was created with.
	/// </summary>
	struct LocalSpacePose
	{
		ScaleRotationTranslationf jointTransforms[MAX_ANIMATION_BONES];
		size_t count;
	};

	/// <summary>
	/// Represents a Pose of a model. Each transform is relative to the model it is animating.
	/// A pose is only valid for models with the same skeleton as the animation it was created with.
	/// </summary>
	struct ModelSpacePose
	{
		Matrix4x4f jointTransforms[MAX_ANIMATION_BONES];
		size_t count;
	};
} // namespace Ag