#include "stdafx.h"
#include <tge/animation/Pose.h>

#include "Skeleton.h"
#include <tge/log/Log.h>

using namespace Tga;

std::shared_ptr<const Skeleton>(*locGetSkeletonFunction)(std::string_view);

void Tga::RegisterGetSkeletonFunction(std::shared_ptr<const Skeleton>(*aGetFunction)(std::string_view))
{
	locGetSkeletonFunction = aGetFunction;
}

std::shared_ptr<const Skeleton> Tga::GetSkeleton(std::string_view someFilePath)
{
	if (locGetSkeletonFunction == nullptr)
	{
		// todo: need error loading in Core somehow!
		ERROR_PRINT("Trying to load a skeleton without registering an skeleton loader. You need to register a loader with RegisterGetSkeletonFunction to use Animations");

		return nullptr;
	}

	return locGetSkeletonFunction(someFilePath);
}

void Skeleton::ConvertPoseToLocalSpace(const ModelSpacePose& in, LocalSpacePose& out) const
{
	for (size_t i = 0; i < joints.size(); i++)
	{
		const Joint& joint = joints[i];
		Matrix4x4f localTransform = joint.parent == -1 ? in.jointTransforms[i] : in.jointTransforms[i] * in.jointTransforms[joint.parent].GetInverse();

		Vector3f position;
		Quaternionf rotation;
		Vector3f scale;

		localTransform.DecomposeMatrix(position, rotation, scale);

		ScaleRotationTranslationf& jointTransform = out.jointTransforms[i];
		jointTransform.SetTranslation(position);
		jointTransform.SetRotation(rotation);
		jointTransform.SetScale(scale);
	}
}

void Skeleton::ConvertPoseToModelSpace(const LocalSpacePose& in, ModelSpacePose& out) const
{
	ConvertPoseToModelSpace(in, out, 0, Matrix4x4f::CreateIdentityMatrix());
	out.count = in.count;
}

void Skeleton::ConvertPoseToModelSpace(const LocalSpacePose& in, ModelSpacePose& out, unsigned aBoneIdx, const Matrix4x4f& aParentTransform) const
{
	const Skeleton::Joint& joint = joints[aBoneIdx];

	out.jointTransforms[aBoneIdx] = in.jointTransforms[aBoneIdx].GetMatrix()*aParentTransform;

	for (size_t c = 0; c < joint.children.size(); c++)
	{
		ConvertPoseToModelSpace(in, out, joint.children[c], out.jointTransforms[aBoneIdx]);
	}
}

void Skeleton::ApplyBindPoseInverse(const ModelSpacePose& in, Matrix4x4f* out) const
{
	for (size_t i = 0; i < joints.size(); i++)
	{
		const Skeleton::Joint& joint = joints[i];
		out[i] = joint.bindPoseInverse * in.jointTransforms[i];
	}
}