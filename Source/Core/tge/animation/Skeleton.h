#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <string_view>

#include <tge/Animation/Pose.h>
#include <tge/Math/Vector.h>
#include <tge/Math/Matrix4x4.h>

namespace Tga
{


/// <summary>
/// Holds the Skeleton for a model and useful metadata.
/// Can be used to lookup joints from names and hierarchical information for joints.
/// </summary>
struct Skeleton
{
	std::string name;

	struct Joint
	{
		Matrix4x4f bindPoseInverse;
		int parent;
		std::vector<unsigned int> children;
		std::string name;

		bool operator==(const Joint& aJoint) const
		{
			const bool A = bindPoseInverse == aJoint.bindPoseInverse;
			const bool B = parent == aJoint.parent;
			const bool C = name == aJoint.name;
			const bool D = children == aJoint.children;

			return A && B && C && D;
		}

		Joint()
			: bindPoseInverse{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, parent(-1)
		{
		}
	};

	ModelSpacePose modelBindPose;
	LocalSpacePose localBindPose;

	std::vector<Joint> joints;
	std::unordered_map<std::string, size_t> jointNameToIndex;
	std::vector<std::string> jointNames;


	const Joint* GetRoot() const { if (joints.empty()) return nullptr; return &joints[0]; }

	bool operator==(const Skeleton& aSkeleton) const
	{
		return joints == aSkeleton.joints;
	}

	void ConvertPoseToLocalSpace(const ModelSpacePose& in, LocalSpacePose& out) const;
	void ConvertPoseToModelSpace(const LocalSpacePose& in, ModelSpacePose& out) const;
	void ApplyBindPoseInverse(const ModelSpacePose& in, Matrix4x4f* out) const;
private:
	void ConvertPoseToModelSpace(const LocalSpacePose& in, ModelSpacePose& out, unsigned aBoneIdx, const Matrix4x4f& aParentTransform)  const;
};


// Skeleton loading is not provided by Core, but it's possible to register a function
// so that animation code can load skeletons without needing a dependency on mesh and animation loading
void RegisterGetSkeletonFunction(std::shared_ptr<const Skeleton>(*aGetFunction)(std::string_view));
std::shared_ptr<const Skeleton> GetSkeleton(std::string_view someFilePath);

} // namespace Tga