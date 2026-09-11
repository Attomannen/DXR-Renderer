#pragma once
#include <vector>
#include <memory>
#include <string>
#include <set>

#include <tge/math/Matrix4x4.h>
#include <tge/scene/SceneObjectDefinition.h>

#include <tge/util/StringCast.h>
#include <tge/uuid/UUIDManager.h>

namespace Tga
{
	class Camera;
	class Scene;
	class SceneObject;
	class ScriptRuntimeInstance;
	class SceneObjectDefinitionManager;
	struct ScriptUpdateContext;

	struct TRS {
		Vector3f translation = { 0.0f };
		Vector3f rotation = { 0.0f };
		Vector3f scale = { 1.0f };
	};

	class SceneObject
	{
	public:
		SceneObject() = default;
		~SceneObject() = default;

		SceneObject(const SceneObject& other) = default;


		Matrix4x4f GetTransform() const 
		{
			Matrix4x4f transform = Matrix4x4f::CreateIdentityMatrix();
			transform.SetRotation(myTRS.rotation);
			transform.SetPosition(myTRS.translation);
			transform.Scale(myTRS.scale);
			return transform; 
		}

		const TRS& GetTRS() const { return myTRS; }
		TRS& GetTRS() { return myTRS; }

		void SetTRS(const TRS& aTRS) { myTRS = aTRS; }
		void SetTransform(const Matrix4x4f& transform) 
		{


			myTRS.translation = transform.GetPosition();

			// only update s and r if the resulting transform is different
			// this is done to not change how it is represented, since there are multiple ways to represent the same rotation
			Matrix4x4f diff = GetTransform() - transform;

			float* start = diff.GetDataPtr();
			float* end = diff.GetDataPtr()+16;

			float sum = 0.f;
			for (float* v = start; v < end; v++)
			{
				sum += (*v) * (*v);
			}

			sum = sqrt(sum);

			if (sum > 1e-6)
			{
				Vector3f t;
				Vector3f s;
				Vector3f r;

				Quaternionf q;

				// first remove existing scale, to keep the scale change as small as possible
				// if two axis scales are negative, that can also be interpreted as 180 degree rotation
				// to ensure it is kept as scale, we remove the existing scale before decomposing the matrix

				Vector3f normalizationScale = myTRS.scale;
				if (abs(normalizationScale.x) < 1e-6) normalizationScale.x = 1.f;
				if (abs(normalizationScale.y) < 1e-6) normalizationScale.y = 1.f;
				if (abs(normalizationScale.z) < 1e-6) normalizationScale.z = 1.f;
				Matrix4x4f transformWithOutScale = Matrix4x4f::CreateFromScale(Vector3f(1.f / normalizationScale.x, 1.f / normalizationScale.y, 1.f / normalizationScale.z)) * transform;

				transformWithOutScale.DecomposeMatrix(t, q, s);
				r = q.GetYawPitchRoll();

				// reconstructing scale, with both the normalization and the scale from the decomped matrix
				myTRS.scale = normalizationScale * s;

				// There are always two ways of representing an orientation with euler angles. Try both and pick the closest one to the current value.
				// This rotation representation will be equivalent:
				Vector3f rAlt = Vector3f(r.x + 180.f, 180.f - r.y, r.z + 180.f);
				if (rAlt.x >= 180.f) 
					rAlt.x -= 360.f;
				if (rAlt.y >= 180.f) 
					rAlt.y -= 360.f;
				if (rAlt.z >= 180.f) 
					rAlt.z -= 360.f;

				// adjust rotation to normalized range before comparing:
				Vector3f clampedRotation = myTRS.rotation;

				while (clampedRotation.x <= -180.f)
					clampedRotation.x += 360.f;
				while (clampedRotation.y <= -180.f)
					clampedRotation.y += 360.f;
				while (clampedRotation.z <= -180.f)
					clampedRotation.z += 360.f;

				while (clampedRotation.x > 180.f)
					clampedRotation.x -= 360.f;
				while (clampedRotation.y > 180.f)
					clampedRotation.y -= 360.f;
				while (clampedRotation.z > 180.f)
					clampedRotation.z -= 360.f;


				Vector3f rotationDiff = r - clampedRotation;
				Vector3f rotationDiffAlt = rAlt - clampedRotation;

				auto angleAbsDiff = [](float diff)
					{
						diff = abs(diff);
						if (diff < 180.f)
							return diff;
						else
							return 360.f - diff;
					};

				float rotationScore = angleAbsDiff(rotationDiff.x) + angleAbsDiff(rotationDiff.y) + angleAbsDiff(rotationDiff.z);
				float rotationScoreAlt = angleAbsDiff(rotationDiffAlt.x) + angleAbsDiff(rotationDiffAlt.y) + angleAbsDiff(rotationDiffAlt.z);

				myTRS.rotation = rotationScore < rotationScoreAlt ? r : rAlt;
			}
		}

		const Vector3f& GetPosition() const { return myTRS.translation; }
		const Vector3f& GetEuler() const { return myTRS.rotation; }
		const Vector3f& GetScale() const { return myTRS.scale; }

		Vector3f& GetPosition() { return myTRS.translation; }
		Vector3f& GetEuler() { return myTRS.rotation; }
		Vector3f& GetScale() { return myTRS.scale; }

		void SetName(const char* aName) { myName = aName; }
		const char* GetName() const { return myName.c_str(); }
		void SetPath(Scene* aScene, StringId aPath);
		StringId GetPath() const { return myPath; }

		void SetSceneObjectDefinitionName(StringId aSceneObjectDefinitionName) { mySceneObjectDefinitionName = aSceneObjectDefinitionName; }
		StringId GetSceneObjectDefinitionName() const { return mySceneObjectDefinitionName; };

		std::span<const SceneProperty> GetPropertyOverrides() const { return myPropertyOverrides; };
		std::vector<SceneProperty>& EditPropertyOverrides() { return myPropertyOverrides; };

		struct PropertySourceAndOveride
		{
			ScenePropertyDefinition source;
			SceneProperty override;
		};

		void CalculateEditablePropertySet(SceneObjectDefinitionManager& objectDefinitionManager, std::vector<PropertySourceAndOveride>& outResult) const;
		void CalculateCombinedPropertySet(SceneObjectDefinitionManager& objectDefinitionManager, std::vector<ScenePropertyDefinition>& outResult) const;

	private:
		StringId mySceneObjectDefinitionName;
		std::vector<SceneProperty> myPropertyOverrides;

		TRS myTRS = { { 0.0f }, { 0.0f }, { 1.0f } };
		std::string myName = "Scene Object";
		StringId myPath = {};

	};
}