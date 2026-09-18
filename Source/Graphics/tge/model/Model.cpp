#include "stdafx.h"
#include <tge/debugging/CpuProfiler.h>
#include <tge/model/Model.h>
#include <tge/log/Log.h>
#include <tge/graphics/DX11.h>
#include <tge/rhi/Device.h>
#include <tge/render/RayTracingMaterialTable.h>

#include <algorithm>
#include <cmath>

using namespace Tga;

namespace
{
	void CreateRayGeometryViews(Model::MeshData& mesh)
	{
		rhi::IDevice* device = DX11::Rhi();
		if (!device || !mesh.vertexBuffer.IsValid() || !mesh.indexBuffer.IsValid()) return;

		rhi::SrvDesc raw = {};
		raw.bufferType = rhi::BufferSrvType::Raw;
		mesh.rayGeometry.vertexRawSrv = device->CreateSrv(mesh.vertexBuffer, raw);
		mesh.rayGeometry.indexRawSrv = device->CreateSrv(mesh.indexBuffer, raw);
		mesh.rayGeometry.materialIndex = RayTracingMaterialTable::GetOrAssignMaterialIndex(mesh.materialName);
	}

	// Open batch for the current thread, if any. See ScopedBlasBatch.
	struct DeferredBlases
	{
		std::vector<rhi::RaytracingBlasDesc> descs;
		std::vector<Model::MeshData*> owners;
		int depth = 0;
	};
	thread_local DeferredBlases tlsDeferred;

	void FlushBlases(std::vector<rhi::RaytracingBlasDesc>& descs, std::vector<Model::MeshData*>& owners)
	{
		if (descs.empty()) return;
		std::vector<rhi::RaytracingBlasHandle> handles(descs.size());
		DX11::Rhi()->CreateRaytracingBlases(descs.data(), (uint32_t)descs.size(), handles.data());
		for (size_t i = 0; i < owners.size(); ++i)
			owners[i]->rayGeometry.blas = handles[i];
		descs.clear();
		owners.clear();
	}

	// All of a model's BLASes in one batched build.
	void BuildBlases(Model::MeshData* meshes, size_t meshCount)
	{
		rhi::IDevice* device = DX11::Rhi();
		if (!device || !device->SupportsRaytracingTier11()) return;
		TGA_CPU_SCOPE("BLAS build");

		const bool deferred = tlsDeferred.depth > 0;
		std::vector<rhi::RaytracingBlasDesc> localDescs;
		std::vector<Model::MeshData*> localOwners;
		std::vector<rhi::RaytracingBlasDesc>& descs = deferred ? tlsDeferred.descs : localDescs;
		std::vector<Model::MeshData*>& owners = deferred ? tlsDeferred.owners : localOwners;
		for (size_t m = 0; m < meshCount; ++m)
		{
			Model::MeshData& mesh = meshes[m];
			if (!mesh.vertexBuffer.IsValid() || !mesh.indexBuffer.IsValid() ||
				mesh.numberOfVertices == 0 || mesh.numberOfIndices < 3) continue;
			rhi::RaytracingBlasDesc blas = {};
			blas.vertexBuffer = mesh.vertexBuffer;
			blas.indexBuffer = mesh.indexBuffer;
			blas.vertexCount = mesh.numberOfVertices;
			blas.vertexStride = mesh.rayGeometry.vertexStride;
			blas.indexCount = mesh.numberOfIndices;
			blas.indexFormat = rhi::Format::R32_UInt;
			blas.debugName = mesh.name.IsEmpty() ? "ModelMesh" : mesh.name.GetString();
			descs.push_back(blas);
			owners.push_back(&mesh);
		}
		// A deferred batch is flushed by the ScopedBlasBatch that opened it; the
		// mesh pointers stay valid because the caller owns them past that scope.
		if (!deferred) FlushBlases(descs, owners);
	}

	void ComputeUnionBounds(const std::vector<Model::MeshData>& meshes, BoxSphereBounds& out)
	{
		Vector3f mn{ 1e30f, 1e30f, 1e30f }, mx{ -1e30f, -1e30f, -1e30f };
		bool any = false;
		for (const Model::MeshData& m : meshes)
		{
			const Vector3f c = m.bounds.center, e = m.bounds.boxExtents;
			mn = { std::min(mn.x, c.x - e.x), std::min(mn.y, c.y - e.y), std::min(mn.z, c.z - e.z) };
			mx = { std::max(mx.x, c.x + e.x), std::max(mx.y, c.y + e.y), std::max(mx.z, c.z + e.z) };
			any = true;
		}
		if (!any) { out = { 0.f, {0,0,0}, {0,0,0} }; return; }
		out.center = (mn + mx) * 0.5f;
		out.boxExtents = (mx - mn) * 0.5f;
		out.radius = std::sqrt(out.boxExtents.x * out.boxExtents.x + out.boxExtents.y * out.boxExtents.y + out.boxExtents.z * out.boxExtents.z);
	}
}

void Model::SetVertexFormat(MeshData& aMesh, VertexFormat aFormat)
{
	MeshData::RayGeometryData& ray = aMesh.rayGeometry;
	ray.vertexFormat = aFormat;
	if (aFormat == VertexFormat::Compact)
	{
		aMesh.stride = sizeof(MeshVertex);
		ray.vertexStride = sizeof(MeshVertex);
		ray.positionOffset = offsetof(MeshVertex, position);
		ray.normalOffset = offsetof(MeshVertex, normalTangent);
		ray.tangentOffset = offsetof(MeshVertex, normalTangent);
		ray.binormalOffset = offsetof(MeshVertex, bitangentSign);
		ray.uv0Offset = offsetof(MeshVertex, uv0);
	}
	else
	{
		aMesh.stride = sizeof(Vertex);
		ray.vertexStride = sizeof(Vertex);
		ray.positionOffset = offsetof(Vertex, position);
		ray.normalOffset = offsetof(Vertex, normal);
		ray.tangentOffset = offsetof(Vertex, tangent);
		ray.binormalOffset = offsetof(Vertex, binormal);
		ray.uv0Offset = offsetof(Vertex, uvs);
	}
	aMesh.offset = 0;
}

bool Model::CreateVertexBuffer(MeshData& aMesh, const Vertex* someVertices, uint32_t aCount,
	VertexFormat aFormat, const char* aDebugName)
{
	if (aFormat == VertexFormat::Compact)
	{
		return CreateVertexBuffer(aMesh, aCount, [&](MeshVertex* out)
		{
			for (uint32_t i = 0; i < aCount; ++i)
				out[i] = PackMeshVertex(someVertices[i]);
		}, aDebugName);
	}

	rhi::IDevice* device = DX11::Rhi();
	const uint64_t bytes = (uint64_t)aCount * sizeof(Vertex);
	if (!device || aCount == 0 || bytes > 0xFFFFFFFFull) return false;
	rhi::BufferDesc desc{};
	desc.byteSize = (uint32_t)bytes;
	desc.stride = sizeof(Vertex);
	desc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::ByteAddress;
	desc.memory = rhi::MemoryType::Default;
	desc.debugName = aDebugName;
	aMesh.vertexBuffer = device->CreateBuffer(desc, someVertices);
	SetVertexFormat(aMesh, VertexFormat::Full);
	return aMesh.vertexBuffer.IsValid();
}

bool Model::CreateVertexBuffer(MeshData& aMesh, uint32_t aCount,
	const std::function<void(MeshVertex*)>& aFill, const char* aDebugName)
{
	rhi::IDevice* device = DX11::Rhi();
	const uint64_t bytes = (uint64_t)aCount * sizeof(MeshVertex);
	if (!device || aCount == 0 || bytes > 0xFFFFFFFFull) return false;
	rhi::BufferDesc desc{};
	desc.byteSize = (uint32_t)bytes;
	desc.stride = sizeof(MeshVertex);
	desc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::ByteAddress;
	desc.memory = rhi::MemoryType::Default;
	desc.debugName = aDebugName;
	aMesh.vertexBuffer = device->CreateBufferWith(desc, [&](void* mapped) { aFill(static_cast<MeshVertex*>(mapped)); });
	SetVertexFormat(aMesh, VertexFormat::Compact);
	return aMesh.vertexBuffer.IsValid();
}

void Model::Init(MeshData& aMeshData, const std::string& aPath)
{
	myMeshData.push_back(aMeshData);
	CreateRayGeometryViews(myMeshData.back());
	BuildBlases(&myMeshData.back(), 1);
	myPath = aPath;
	ComputeUnionBounds(myMeshData, myBounds);
}

void Model::Init(std::vector<MeshData>& someMeshData, const std::string& aPath)
{
	if (someMeshData.size() > MAX_MESHES_PER_MODEL)
	{
		// Degrade gracefully rather than overrunning the fixed per-mesh arrays.
		ERROR_PRINT("Model '%s' has %zu sub-meshes; clamping to MAX_MESHES_PER_MODEL (%d)",
			aPath.c_str(), someMeshData.size(), MAX_MESHES_PER_MODEL);
		someMeshData.resize(MAX_MESHES_PER_MODEL);
	}

	myMeshData = someMeshData;
	FinishInit(aPath);
}

void Model::Init(std::vector<MeshData>&& someMeshData, const std::string& aPath)
{
	if (someMeshData.size() > MAX_MESHES_PER_MODEL)
	{
		ERROR_PRINT("Model '%s' has %zu sub-meshes; clamping to MAX_MESHES_PER_MODEL (%d)",
			aPath.c_str(), someMeshData.size(), MAX_MESHES_PER_MODEL);
		someMeshData.resize(MAX_MESHES_PER_MODEL);
	}
	myMeshData = std::move(someMeshData);
	FinishInit(aPath);
}

void Model::FinishInit(const std::string& aPath)
{
	for (MeshData& mesh : myMeshData)
		CreateRayGeometryViews(mesh);
	BuildBlases(myMeshData.data(), myMeshData.size());
	myPath = aPath;
	ComputeUnionBounds(myMeshData, myBounds);

	// Rendering, ray tracing and bounds only need the GPU buffers from here
	// on; a large scene's CPU copy is otherwise gigabytes of dead RAM.
	for (MeshData& mesh : myMeshData)
	{
		if (!mesh.vertexBuffer.IsValid() || !mesh.indexBuffer.IsValid()) continue;
		std::vector<Vertex>().swap(mesh.vertices);
		std::vector<unsigned int>().swap(mesh.indices);
	}
}

Tga::ScopedBlasBatch::ScopedBlasBatch()
{
	++tlsDeferred.depth;
}

Tga::ScopedBlasBatch::~ScopedBlasBatch()
{
	if (--tlsDeferred.depth > 0) return;
	rhi::IDevice* device = DX11::Rhi();
	if (!device || !device->SupportsRaytracingTier11())
	{
		tlsDeferred.descs.clear();
		tlsDeferred.owners.clear();
		return;
	}
	TGA_CPU_SCOPE("BLAS build (batched)");
	FlushBlases(tlsDeferred.descs, tlsDeferred.owners);
}
