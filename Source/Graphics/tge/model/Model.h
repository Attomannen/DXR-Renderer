#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstddef>
#include <functional>

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

	// How a mesh's GPU vertex buffer is laid out.
	enum class VertexFormat : uint32_t
	{
		Full = 0,      // Vertex, 180 bytes: skinned meshes
		Compact = 1,   // MeshVertex, 40 bytes: everything else
	};

	struct MeshData
	{
		// Immutable source data consumed by the future DXR hit shader. These are
		// deliberately separate from the IA bindings: RayQuery fetches vertices
		// through raw SRVs, using the byte offsets below.
		struct RayGeometryData
		{
			rhi::SrvHandle vertexRawSrv;
			rhi::SrvHandle indexRawSrv;
			rhi::RaytracingBlasHandle blas;
			uint32_t materialIndex = 0; // RayTracingMaterialTable slot; zero = invalid/default
			uint32_t vertexStride = sizeof(Vertex);
			uint32_t positionOffset = offsetof(Vertex, position);
			uint32_t normalOffset = offsetof(Vertex, normal);
			uint32_t uv0Offset = offsetof(Vertex, uvs);
			uint32_t indexStride = sizeof(uint32_t);
			// Authored per-vertex tangent/binormal -- the same ones GBufferPS.hlsl's
			// TBN uses (import-time-computed, correct across mirrored-UV shells).
			// Smoothly interpolated across a triangle exactly like the raster path,
			// unlike deriving a tangent from position/UV deltas (flat per-triangle).
			uint32_t tangentOffset = offsetof(Vertex, tangent);
			uint32_t binormalOffset = offsetof(Vertex, binormal);
			// Compact: normalOffset/tangentOffset point at the packed octahedral
			// pair and binormalOffset at the bitangent sign.
			VertexFormat vertexFormat = VertexFormat::Full;
		};

		StringId name;
		StringId materialName;
		uint32_t numberOfVertices;
		uint32_t numberOfIndices;
		uint32_t stride;
		uint32_t offset;
		rhi::BufferHandle vertexBuffer;
		rhi::BufferHandle indexBuffer;
		RayGeometryData rayGeometry;
		BoxSphereBounds bounds;
		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;
	};
		
	// Uploads someVertices as aMesh.vertexBuffer and sets the stride and ray
	// offsets to match. Static meshes are packed to MeshVertex.
	static bool CreateVertexBuffer(MeshData& aMesh, const Vertex* someVertices, uint32_t aCount,
		VertexFormat aFormat, const char* aDebugName);
	// Compact upload that fills the mapped buffer in place (mesh cache).
	static bool CreateVertexBuffer(MeshData& aMesh, uint32_t aCount,
		const std::function<void(MeshVertex*)>& aFill, const char* aDebugName);
	static void SetVertexFormat(MeshData& aMesh, VertexFormat aFormat);

	void Init(MeshData& aMeshData, const std::string& aPath);
	void Init(std::vector<MeshData>& someMeshData, const std::string& aPath);
	void Init(std::vector<MeshData>&& someMeshData, const std::string& aPath);   // takes ownership, no vertex copy

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
	void FinishInit(const std::string& aPath);   // ray views, BLASes, bounds

	std::shared_ptr<const Skeleton> mySkeleton;
	std::vector<MeshData> myMeshData;
	std::string myPath;
	BoxSphereBounds myBounds{ 0.f, {0,0,0}, {0,0,0} };

	const TextureResource* myDefaultTextures[MAX_MESHES_PER_MODEL][4] = {};
};

} // namespace Tga
