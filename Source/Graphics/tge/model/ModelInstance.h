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

	// Per-instance material (RayTracingMaterialTable index) for one sub-mesh.
	// 0 uses the mesh's own material. Used by both raster and DXR.
	void SetMaterial(int meshIndex, uint32_t aMaterialIndex) { myMaterials[meshIndex] = aMaterialIndex; }
	void SetMaterialAll(uint32_t aMaterialIndex) { for (uint32_t& m : myMaterials) m = aMaterialIndex; }
	uint32_t GetMaterialOverride(size_t meshIndex) const { return myMaterials[meshIndex]; }
	bool IsValid() { return myModel ? true : false; }
	void Render(const ModelShader& shader) const;
	void Render(const ModelShader& shader, int aMeshIndex) const;
	// Draw only the listed sub-meshes, hoisting per-instance state once.
	void Render(const ModelShader& shader, const std::vector<int>& someMeshIndices) const;
	// Draw only sub-meshes whose world-space bounds pass the frustum (shadow tiles).
	void Render(const ModelShader& shader, const Frustum& frustum) const;
	// Sub-mesh list AND frustum. A model built from one gathered FBX is a single
	// instance covering the whole scene, so model-level culling can never reject
	// it; only per-sub-mesh bounds can.
	void Render(const ModelShader& shader, const std::vector<int>& someMeshIndices, const Frustum& frustum) const;
private:

	std::shared_ptr<Model> myModel{};
	const TextureResource* myTextures[MAX_MESHES_PER_MODEL][4] = {};
	uint32_t myMaterials[MAX_MESHES_PER_MODEL] = {};
	Matrix4x4f myTransform{};
};

} // namespace Tga