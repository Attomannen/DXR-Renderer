#pragma once
#include <map>
#include <memory>
#include <tge/math/matrix4x4.h>
#include <tge/EngineDefines.h>
#include <tge/model/model.h>
#include <tge/render/RenderCommon.h>

#include <tge/EngineMacros.h>

namespace Tga
{
	class AnimationPlayer;
	class AnimatedModel;
	class ModelShader;
	class TextureResource;

	struct LocalSpacePose;
	struct ModelSpacePose;

	class AnimatedModelInstance
	{
	public:
		AnimatedModelInstance();
		~AnimatedModelInstance();
		void Init(std::shared_ptr<Model> aModel);

		void Render(const ModelShader& shader) const;
		void Render(const ModelShader& shader, int aMeshIndex) const;

		const Matrix4x4f& GetTransform() const { return myTransform; }
		Matrix4x4f& GetTransform() { return myTransform; }
		void SetTransform(const Matrix4x4f& someTransform);
		bool IsValid() const { return myModel ? true : false; }
		void SetTexture(int meshIndex, int textureIndex, const TextureResource* texture) { myTextures[meshIndex][textureIndex] = texture; }
		const TextureResource* const* GetTextures(size_t meshIndex) const { return myTextures[meshIndex]; }

		void SetPose(const LocalSpacePose& pose);
		void SetPose(const ModelSpacePose& pose);
		void SetPose(const AnimationPlayer& animationInstance);
		void ResetPose();

		std::shared_ptr<Model> GetModel() { return myModel; }
		const std::shared_ptr<const Model> GetModel() const { return myModel; }
	private:
		Matrix4x4f myTransform;
		std::shared_ptr<Model> myModel = nullptr;

		const TextureResource* myTextures[MAX_MESHES_PER_MODEL][4] = {};
		Matrix4x4f myBoneTransforms[MAX_ANIMATION_BONES];
	};

}
