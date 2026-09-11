#pragma once
#include <string>
#include <vector>
#include <unordered_map>

#include <tge/graphics/Vertex.h>
#include <tge/Animation/Skeleton.h>
#include <tge/Math/Vector.h>
#include <tge/Math/BoxSphereBounds.h>
#include <tge/EngineDefines.h>
#include <tge/rhi/Handles.h>

#include "tge/stringRegistry/StringRegistry.h"

namespace Tga
{

class TextureResource;

class Model
{
public:

	friend class ModelFactory;

	struct MeshData
	{
		StringId name;
		StringId materialName;
		uint32_t numberOfVertices;
		uint32_t numberOfIndices;
		uint32_t stride;
		uint32_t offset;
		rhi::BufferHandle vertexBuffer;
		rhi::BufferHandle indexBuffer;
		BoxSphereBounds bounds;
		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;
	};
		
	void Init(MeshData& aMeshData, const std::string& aPath);
	void Init(std::vector<MeshData>& someMeshData, const std::string& aPath);

	const StringId GetMaterialName(int meshIndex) const { return myMeshData[meshIndex].materialName; }
	const StringId GetMeshName(int meshIndex) const { return myMeshData[meshIndex].name; }

	size_t GetMeshCount() const {return myMeshData.size();}
	MeshData const& GetMeshData(unsigned int anIndex) const { return myMeshData[anIndex]; }
	const std::vector<MeshData>& GetMeshDataList() const { return myMeshData; }

	const std::string& GetPath() { return myPath; }
	const std::shared_ptr<const Skeleton>& GetSkeleton() const { return mySkeleton; }

	// Union of all sub-mesh bounds, in model space. Computed at Init.
	const BoxSphereBounds& GetBounds() const { return myBounds; }

	void SetDefaultTexture(int meshIndex, int textureIndex, TextureResource* texture) { myDefaultTextures[meshIndex][textureIndex] = texture; }
	const TextureResource* const* GetDefaultTextures(size_t meshIndex) const { return myDefaultTextures[meshIndex]; }
private:

	std::shared_ptr<const Skeleton> mySkeleton;
	std::vector<MeshData> myMeshData;
	std::string myPath;
	BoxSphereBounds myBounds{ 0.f, {0,0,0}, {0,0,0} };

	const TextureResource* myDefaultTextures[MAX_MESHES_PER_MODEL][4] = {};
};

} // namespace Tga