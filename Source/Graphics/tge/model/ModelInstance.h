#pragma once
#include <memory>
#include <vector>
#include <tge/Math/Matrix4x4.h>
#include <tge/EngineDefines.h>
#include <tge/texture/texture.h>

namespace Tga
{

class Model;
class ModelShader;
struct Frustum;
class ModelInstance
{
public:
	void Init(std::shared_ptr<Model> aModel);

	std::shared_ptr<Model> GetModel() const;

	const Matrix4x4f& GetTransform() const { return myTransform; }
	Matrix4x4f& GetTransform() { return myTransform; }
	void SetTransform(const Matrix4x4f& someTransform);

	void SetTexture(int meshIndex, int textureIndex, const TextureResource* texture) { myTextures[meshIndex][textureIndex] = texture; }

	const TextureResource* const* GetTextures(size_t meshIndex) const { return myTextures[meshIndex]; }
	bool IsValid() { return myModel ? true : false; }
	void Render(const ModelShader& shader) const;
	void Render(const ModelShader& shader, int aMeshIndex) const;
	// Draw only the listed sub-meshes, hoisting per-instance state once.
	void Render(const ModelShader& shader, const std::vector<int>& someMeshIndices) const;
	// Draw only sub-meshes whose world-space bounds pass the frustum (shadow tiles).
	void Render(const ModelShader& shader, const Frustum& frustum) const;
private:

	std::shared_ptr<Model> myModel{};
	const TextureResource* myTextures[MAX_MESHES_PER_MODEL][4] = {};
	Matrix4x4f myTransform{};
};

} // namespace Tga