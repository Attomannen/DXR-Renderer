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

	// All of a model's BLASes in one batched build.
	void BuildBlases(Model::MeshData* meshes, size_t meshCount)
	{
		rhi::IDevice* device = DX11::Rhi();
		if (!device || !device->SupportsRaytracingTier11()) return;
		TGA_CPU_SCOPE("BLAS build");

		std::vector<rhi::RaytracingBlasDesc> descs;
		std::vector<Model::MeshData*> owners;
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
		std::vector<rhi::RaytracingBlasHandle> handles(descs.size());
		device->CreateRaytracingBlases(descs.data(), (uint32_t)descs.size(), handles.data());
		for (size_t i = 0; i < owners.size(); ++i)
			owners[i]->rayGeometry.blas = handles[i];
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
