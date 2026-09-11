#include "stdafx.h"
#include "ModelFactory.h"

#include <fstream>
#include <filesystem>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <tge/application.h>
#include <tge/settings/settings.h>
#include <tge/log/Log.h>
#include <tge/animation/animationPlayer.h>
#include <tge/graphics/DX11.h>
#include <tge/rhi/Device.h>
#include <tge/graphics/GraphicsEngine.h>
#include <tge/model/Model.h>
#include <tge/model/ModelInstance.h>
#include <tge/graphics/Vertex.h>
#include <tge/math/matrix4x4.h>
#include <tge/texture/texture.h>
#include <tge/texture/TextureManager.h>
#include <tge/filewatcher/FileWatcher.h>
#include <tge/util/FixedStream.h>

//#define TGA_USE_UFBX
#ifdef TGA_USE_UFBX
#include <ufbx/ufbx.h>
#else
#include <TGAFBXImporter/source/Importer.h>
#endif

#include <DDSTextureLoader/DDSTextureLoader11.h>

using namespace Tga;
ModelFactory* ModelFactory::ourInstance = nullptr;

#define TEXTURE_SET_0 0
#define TEXTURE_SET_1 1
#define TEXTURE_SET_2 2
#define TEXTURE_SET_3 3

#define VERTEX_COLOR_SET_0 0
#define VERTEX_COLOR_SET_1 1
#define VERTEX_COLOR_SET_2 2
#define VERTEX_COLOR_SET_3 3

#define NUM_BONES_PER_VERTEX 4

using namespace Tga;

void AssignDefaultMaterials(std::string_view someFilePath, Model* aModel);

struct VertexBoneData
{
	unsigned int IDs[NUM_BONES_PER_VERTEX];
	float Weights[NUM_BONES_PER_VERTEX];

	VertexBoneData()
	{
		Reset();
	};

	void Reset()
	{
		memset(IDs, 0, sizeof(IDs));
		memset(Weights, 0, sizeof(Weights));
	}

	void AddBoneData(unsigned int BoneID, float Weight)
	{
		for (unsigned int i = 0; i < sizeof(IDs) / sizeof(IDs[0]); i++)
		{
			if (Weights[i] == 0.0)
			{
				IDs[i] = BoneID;
				Weights[i] = Weight;
				return;
			}
		}

		// should never get here - more bones than we have space for
		//assert(0);
	}
};

bool ModelFactory::InitUnitCube()
{
	// First we make a cube.

	Model::MeshData meshData = {};
	// Watch the winding! DX defaults to Clockwise.
	// Assume the winding as if you're viewing the face head on.
	// +Y up, +X right, +Z Forward
	meshData.vertices = 
	{
		// Front
		{
			50.0f, -50.0f, 50.0f,
			0, 0, 1,
			1, 0, 0,
			0, -1, 0,
			1, 1, 1, 1,
			0, 1
		},
		{
			50.0f, 50.0f, 50.0f,
			0, 0, 1,
			1, 0, 0,
			0, -1, 0,
			1, 1, 1, 1,
			0, 0
		},
		{
			-50.0f, 50.0f, 50.0f,
			0, 0, 1,
			1, 0, 0,
			0, -1, 0,
			1, 1, 1, 1,
			1, 0
		},
		{
			-50.0f, -50.0f, 50.0f,
			0, 0, 1,
			1, 0, 0,
			0, -1, 0,
			1, 1, 1, 1,
			1, 1
		},

		// Left
		{
			-50.0f, -50.0f, 50.0f,
			-1, 0, 0,
			0, 0, 1,
			0, -1, 0,
			1, 0, 0, 1,
			0, 1
		},
		{
			-50.0f, 50.0f, 50.0f,
			-1, 0, 0,
			0, 0, 1,
			0, -1, 0,
			1, 0, 0, 1,
			0, 0
		},
		{
			-50.0f, 50.0f, -50.0f,
			-1, 0, 0,
			0, 0, 1,
			0, -1, 0,
			1, 0, 0, 1,
			1, 0
		},
		{
			-50.0f, -50.0f, -50.0f,
			-1, 0, 0,
			0, 0, 1,
			0, -1, 0,
			1, 0, 0, 1,
			1, 1
		},

		// Back
		{
			-50.0f, -50.0f, -50.0f,
			0, 0, -1,
			1, 0, 0,
			0, -1, 0,
			0, 1, 0, 1,
			0, 1
		},
		{
			-50.0f, 50.0f, -50.0f,
			0, 0, -1,
			1, 0, 0,
			0, -1, 0,
			0, 1, 0, 1,
			0, 0
		},
		{
			50.0f, 50.0f, -50.0f,
			0, 0, -1,
			1, 0, 0,
			0, -1, 0,
			0, 1, 0, 1,
			1, 0
		},
		{
			50.0f, -50.0f, -50.0f,
			0, 0, -1,
			1, 0, 0,
			0, -1, 0,
			0, 1, 0, 1,
			1, 1
		},

		// Right
		{
			50.0f, -50.0f, -50.0f,
			1, 0, 0,
			0, 0, -1,
			0, -1, 0,
			0, 0, 1, 1,
			0, 1
		},
		{
			50.0f, 50.0f, -50.0f,
			1, 0, 0,
			0, 0, -1,
			0, -1, 0,
			0, 0, 1, 1,
			0, 0
		},
		{
			50.0f, 50.0f, 50.0f,
			1, 0, 0,
			0, 0, -1,
			0, -1, 0,
			0, 0, 1, 1
			, 1, 0
		},
		{
			50.0f, -50.0f, 50.0f,
			1, 0, 0,
			0, 0, -1,
			0, -1, 0,
			0, 0, 1, 1,
			1, 1
		},

		// Top
		{
			50.0f, 50.0f, 50.0f,
			0, 1, 0,
			1, 0, 0,
			0, 0, 1,
			1, 1, 0, 1,
			0, 1
		},
		{
			50.0f, 50.0f, -50.0f,
			0, 1, 0,
			1, 0, 0,
			0, 0, 1,
			1, 1, 0, 1,
			0, 0
		},
		{
			-50.0f, 50.0f, -50.0f,
			0, 1, 0,
			1, 0, 0,
			0, 0, 1,
			1, 1, 0, 1,
			1, 0
		},
		{
			-50.0f, 50.0f, 50.0f,
			0, 1, 0,
			1, 0, 0,
			0, 0, 1,
			1, 1, 0, 1,
			1, 1
		},

		// Bottom
		{
			-50.0f, -50.0f, 50.0f,
			0, -1, 0,
			1, 0, 0,
			0, 0, 1,
			1, 0, 1, 1,
			0, 1
		},
		{
			-50.0f, -50.0f, -50.0f,
			0, -1, 0,
			1, 0, 0,
			0, 0, 1,
			1, 0, 1, 1,
			0, 0
		},
		{
			50.0f, -50.0f, -50.0f,
			0, -1, 0,
			1, 0, 0,
			0, 0, 1,
			1, 0, 1, 1,
			1, 0
		},
		{
			50.0f, -50.0f, 50.0f,
			0, -1, 0,
			1, 0, 0,
			0, 0, 1,
			1, 0, 1, 1
			, 1, 1
		},
	};

	meshData.indices =
	{
		0, 1, 2,        /* |/ */
		0, 2, 3,        /* /| */
		4, 5, 6,        /* |/ */
		4, 6, 7,        /* /| */
		8, 9, 10,       /* |/ */
		8, 10, 11,      /* /| */
		12, 13, 14,     /* |/ */
		12, 14, 15,     /* /| */
		16, 17, 18,     /* |/ */
		16, 18, 19,     /* /| */
		20, 21, 22,     /* |/ */
		20, 22, 23      /* /| */
	};

	//const Vector3f extentsCenter = 0.5f * (minExtents + maxExtents);
	//const Vector3f boxExtents = 0.5f * (maxExtents - minExtents);
	//const float myBoxSphereRadius = FMath::Max(boxExtents.X, FMath::Max(boxExtents.Y, boxExtents.Z));

	rhi::BufferDesc vertexBufferDesc{};
	vertexBufferDesc.byteSize = static_cast<UINT>(meshData.vertices.size()) * static_cast<UINT>(sizeof(Vertex));
	vertexBufferDesc.stride = sizeof(Vertex);
	vertexBufferDesc.usage = rhi::BufferUsage::Vertex;
	vertexBufferDesc.memory = rhi::MemoryType::Default;
	vertexBufferDesc.debugName = "Cube_VB";

	rhi::BufferHandle vertexBuffer = DX11::Rhi()->CreateBuffer(vertexBufferDesc, &meshData.vertices[0]);
	if (!vertexBuffer.IsValid())
	{
		return false;
	}

	rhi::BufferDesc indexBufferDesc{};
	indexBufferDesc.byteSize = static_cast<UINT>(meshData.indices.size()) * static_cast<UINT>(sizeof(unsigned int));
	indexBufferDesc.stride = sizeof(unsigned int);
	indexBufferDesc.usage = rhi::BufferUsage::Index;
	indexBufferDesc.memory = rhi::MemoryType::Default;
	indexBufferDesc.debugName = "Cube_IB";

	rhi::BufferHandle indexBuffer = DX11::Rhi()->CreateBuffer(indexBufferDesc, &meshData.indices[0]);
	if (!indexBuffer.IsValid())
	{
		return false;
	}

	std::shared_ptr<Model> model = std::make_shared<Model>();

	meshData.numberOfVertices = static_cast<UINT>(meshData.vertices.size());
	meshData.numberOfIndices = static_cast<UINT>(meshData.indices.size());
	meshData.stride = sizeof(Vertex);
	meshData.offset = 0;
	meshData.vertexBuffer = vertexBuffer;
	meshData.indexBuffer = indexBuffer;
	meshData.bounds = CalculateBoxSphereBounds(meshData.vertices);
	model->Init(meshData, "Cube");

	AssignDefaultMaterials("", model.get());

	myLoadedModels.insert(std::pair<StringId, std::shared_ptr<Model>>("Cube"_tgaid, model));

	return true;
}

bool ModelFactory::InitUnitPlane()
{
	Model::MeshData meshData = {};

	meshData.vertices.push_back({
		-50.0f, 0.0f, 50.0f,
		0, 1, 0,
		1, 0, 0,
		0, 0, 1,
		1, 1, 1, 1,
		0, 0
		});

	meshData.vertices.push_back({
		50.0f, 0.0f, 50.0f,
		0, 1, 0,
		1, 0, 0,
		0, 0, 1,
		1, 1, 1, 1,
		1, 0
		});

	meshData.vertices.push_back({
		50.0f, 0.0f, -50.0f,
		0, 1, 0,
		1, 0, 0,
		0, 0, 1,
		1, 1, 1, 1,
		1, 1
		});

	meshData.vertices.push_back({
		-50.0f, 0.0f, -50.0f,
		0, 1, 0,
		1, 0, 0,
		0, 0, 1,
		1, 1, 1, 1,
		0, 1
		});


	meshData.indices = { 0, 1, 2, 0, 2, 3 };

	//const Vector3f extentsCenter = 0.5f * (minExtents + maxExtents);
	//const Vector3f boxExtents = 0.5f * (maxExtents - minExtents);
	//const float myBoxSphereRadius = FMath::Max(boxExtents.X, FMath::Max(boxExtents.Y, boxExtents.Z));

	rhi::BufferDesc vertexBufferDesc{};
	vertexBufferDesc.byteSize = static_cast<UINT>(meshData.vertices.size()) * static_cast<UINT>(sizeof(Vertex));
	vertexBufferDesc.stride = sizeof(Vertex);
	vertexBufferDesc.usage = rhi::BufferUsage::Vertex;
	vertexBufferDesc.memory = rhi::MemoryType::Default;
	vertexBufferDesc.debugName = "Plane_VB";

	rhi::BufferHandle vertexBuffer = DX11::Rhi()->CreateBuffer(vertexBufferDesc, &meshData.vertices[0]);
	if (!vertexBuffer.IsValid())
	{
		return false;
	}

	rhi::BufferDesc indexBufferDesc{};
	indexBufferDesc.byteSize = static_cast<UINT>(meshData.indices.size()) * static_cast<UINT>(sizeof(unsigned int));
	indexBufferDesc.stride = sizeof(unsigned int);
	indexBufferDesc.usage = rhi::BufferUsage::Index;
	indexBufferDesc.memory = rhi::MemoryType::Default;
	indexBufferDesc.debugName = "Plane_IB";

	rhi::BufferHandle indexBuffer = DX11::Rhi()->CreateBuffer(indexBufferDesc, &meshData.indices[0]);
	if (!indexBuffer.IsValid())
	{
		return false;
	}

	std::shared_ptr<Model> model = std::make_shared<Model>();

	meshData.numberOfVertices = static_cast<UINT>(meshData.vertices.size());
	meshData.numberOfIndices = static_cast<UINT>(meshData.indices.size());
	meshData.stride = sizeof(Vertex);
	meshData.offset = 0;
	meshData.vertexBuffer = vertexBuffer;
	meshData.indexBuffer = indexBuffer;
	meshData.bounds = CalculateBoxSphereBounds(meshData.vertices);
	model->Init(meshData, "Plane");
	myLoadedModels.insert(std::pair<StringId, std::shared_ptr<Model>>("Plane"_tgaid, model));

	AssignDefaultMaterials("", model.get());

	return true;
}

// Shared tail for the procedural primitives: upload buffers, register the model
// under aId and give it the default (T_Default_*) material set.
static bool FinalizePrimitive(Model::MeshData& meshData, const char* aName, StringId aId,
	std::unordered_map<StringId, std::shared_ptr<Model>>& aRegistry)
{
	rhi::BufferDesc vbDesc{};
	vbDesc.byteSize = static_cast<UINT>(meshData.vertices.size()) * static_cast<UINT>(sizeof(Vertex));
	vbDesc.stride = sizeof(Vertex);
	vbDesc.usage = rhi::BufferUsage::Vertex;
	vbDesc.memory = rhi::MemoryType::Default;
	vbDesc.debugName = "Primitive_VB";
	rhi::BufferHandle vertexBuffer = DX11::Rhi()->CreateBuffer(vbDesc, meshData.vertices.data());
	if (!vertexBuffer.IsValid())
		return false;

	rhi::BufferDesc ibDesc{};
	ibDesc.byteSize = static_cast<UINT>(meshData.indices.size()) * static_cast<UINT>(sizeof(unsigned int));
	ibDesc.stride = sizeof(unsigned int);
	ibDesc.usage = rhi::BufferUsage::Index;
	ibDesc.memory = rhi::MemoryType::Default;
	ibDesc.debugName = "Primitive_IB";
	rhi::BufferHandle indexBuffer = DX11::Rhi()->CreateBuffer(ibDesc, meshData.indices.data());
	if (!indexBuffer.IsValid())
		return false;

	meshData.numberOfVertices = static_cast<UINT>(meshData.vertices.size());
	meshData.numberOfIndices = static_cast<UINT>(meshData.indices.size());
	meshData.stride = sizeof(Vertex);
	meshData.offset = 0;
	meshData.vertexBuffer = vertexBuffer;
	meshData.indexBuffer = indexBuffer;
	// meshData.bounds is filled by the caller (member fn can reach CalculateBoxSphereBounds).

	std::shared_ptr<Model> model = std::make_shared<Model>();
	model->Init(meshData, aName);
	aRegistry.insert({ aId, model });
	AssignDefaultMaterials("", model.get());
	return true;
}

bool ModelFactory::InitUnitCone()
{
	// Apex at +Y (y=+50), base ring radius 50 at y=-50, plus a base cap. Matches
	// the ±50 convention of the other built-ins. +Y up.
	constexpr int kSegments = 32;
	constexpr float kRadius = 50.0f;
	constexpr float kHalf = 50.0f;
	const float kSlant = std::sqrt(kRadius * kRadius + (2.0f * kHalf) * (2.0f * kHalf));

	Model::MeshData meshData = {};
	const Vector3f apex{ 0.0f, kHalf, 0.0f };

	for (int i = 0; i < kSegments; ++i)
	{
		const float a0 = (float)i / kSegments * 6.28318530718f;
		const float a1 = (float)(i + 1) / kSegments * 6.28318530718f;
		const Vector3f p0{ std::cos(a0) * kRadius, -kHalf, std::sin(a0) * kRadius };
		const Vector3f p1{ std::cos(a1) * kRadius, -kHalf, std::sin(a1) * kRadius };

		// Side face normal: outward, tilted up by the cone slope.
		const float am = (a0 + a1) * 0.5f;
		Vector3f n{ std::cos(am) * (2.0f * kHalf), kRadius, std::sin(am) * (2.0f * kHalf) };
		n.Normalize();
		Vector3f t{ -std::sin(am), 0.0f, std::cos(am) };
		Vector3f b = n.Cross(t);

		const float fi0 = (float)i / (float)kSegments;
		const float fi1 = (float)(i + 1) / (float)kSegments;
		const unsigned int base = (unsigned int)meshData.vertices.size();
		meshData.vertices.emplace_back(apex.x, apex.y, apex.z, n.x, n.y, n.z, t.x, t.y, t.z, b.x, b.y, b.z, 1.0f, 1.0f, 1.0f, 1.0f, fi0, 0.0f);
		meshData.vertices.emplace_back(p0.x, p0.y, p0.z, n.x, n.y, n.z, t.x, t.y, t.z, b.x, b.y, b.z, 1.0f, 1.0f, 1.0f, 1.0f, fi0, 1.0f);
		meshData.vertices.emplace_back(p1.x, p1.y, p1.z, n.x, n.y, n.z, t.x, t.y, t.z, b.x, b.y, b.z, 1.0f, 1.0f, 1.0f, 1.0f, fi1, 1.0f);
		meshData.indices.push_back(base + 0);
		meshData.indices.push_back(base + 2);
		meshData.indices.push_back(base + 1);
		(void)kSlant;

		// Base cap (normal -Y), wound to face down.
		const unsigned int cbase = (unsigned int)meshData.vertices.size();
		meshData.vertices.emplace_back(0.0f, -kHalf, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f);
		meshData.vertices.emplace_back(p0.x, -kHalf, p0.z, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, std::cos(a0) * 0.5f + 0.5f, std::sin(a0) * 0.5f + 0.5f);
		meshData.vertices.emplace_back(p1.x, -kHalf, p1.z, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, std::cos(a1) * 0.5f + 0.5f, std::sin(a1) * 0.5f + 0.5f);
		meshData.indices.push_back(cbase + 0);
		meshData.indices.push_back(cbase + 1);
		meshData.indices.push_back(cbase + 2);
	}

	meshData.bounds = CalculateBoxSphereBounds(meshData.vertices);
	return FinalizePrimitive(meshData, "Cone", "Cone"_tgaid, myLoadedModels);
}

bool ModelFactory::InitUnitTorus()
{
	// Major radius chosen so the torus fits the ±50 box; minor radius 15. Lies in
	// the XZ plane, +Y up.
	constexpr int kMajor = 48;
	constexpr int kMinor = 20;
	constexpr float kMajorR = 35.0f;
	constexpr float kMinorR = 15.0f;
	constexpr float kTwoPi = 6.28318530718f;

	Model::MeshData meshData = {};
	for (int i = 0; i < kMajor; ++i)
	{
		const float u = (float)i / kMajor * kTwoPi;
		for (int j = 0; j < kMinor; ++j)
		{
			const float v = (float)j / kMinor * kTwoPi;
			const Vector3f center{ std::cos(u) * kMajorR, 0.0f, std::sin(u) * kMajorR };
			Vector3f n{ std::cos(u) * std::cos(v), std::sin(v), std::sin(u) * std::cos(v) };
			n.Normalize();
			const Vector3f pos = center + n * kMinorR;
			Vector3f t{ -std::sin(u), 0.0f, std::cos(u) };
			Vector3f b = n.Cross(t);
			meshData.vertices.emplace_back(pos.x, pos.y, pos.z, n.x, n.y, n.z, t.x, t.y, t.z, b.x, b.y, b.z,
				1.0f, 1.0f, 1.0f, 1.0f, (float)i / (float)kMajor, (float)j / (float)kMinor);
		}
	}
	auto idx = [&](int i, int j) { return (unsigned int)((i % kMajor) * kMinor + (j % kMinor)); };
	for (int i = 0; i < kMajor; ++i)
		for (int j = 0; j < kMinor; ++j)
		{
			const unsigned int a = idx(i, j), bIdx = idx(i + 1, j), c = idx(i + 1, j + 1), d = idx(i, j + 1);
			meshData.indices.push_back(a); meshData.indices.push_back(d); meshData.indices.push_back(bIdx);
			meshData.indices.push_back(bIdx); meshData.indices.push_back(d); meshData.indices.push_back(c);
		}

	meshData.bounds = CalculateBoxSphereBounds(meshData.vertices);
	return FinalizePrimitive(meshData, "Torus", "Torus"_tgaid, myLoadedModels);
}

bool ModelFactory::InitUnitSphere()
{
	// UV sphere, radius 50, +Y up.
	constexpr int kRings = 32;   // latitude
	constexpr int kSegs = 48;    // longitude
	constexpr float kR = 50.0f;
	constexpr float kPi = 3.14159265358979f;
	constexpr float kTwoPi = 6.28318530718f;

	Model::MeshData meshData = {};
	for (int y = 0; y <= kRings; ++y)
	{
		const float v = (float)y / kRings;
		const float phi = v * kPi;             // 0..pi
		for (int x = 0; x <= kSegs; ++x)
		{
			const float u = (float)x / kSegs;
			const float theta = u * kTwoPi;
			Vector3f n{ std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta) };
			Vector3f t{ -std::sin(theta), 0.0f, std::cos(theta) };
			Vector3f b = n.Cross(t);
			meshData.vertices.emplace_back(n.x * kR, n.y * kR, n.z * kR, n.x, n.y, n.z, t.x, t.y, t.z, b.x, b.y, b.z,
				1.0f, 1.0f, 1.0f, 1.0f, u, v);
		}
	}
	const int stride = kSegs + 1;
	for (int y = 0; y < kRings; ++y)
		for (int x = 0; x < kSegs; ++x)
		{
			const unsigned int a = (unsigned int)(y * stride + x);
			const unsigned int c = (unsigned int)(a + stride);
			meshData.indices.push_back(a); meshData.indices.push_back(a + 1); meshData.indices.push_back(c);
			meshData.indices.push_back(a + 1); meshData.indices.push_back(c + 1); meshData.indices.push_back(c);
		}

	meshData.bounds = CalculateBoxSphereBounds(meshData.vertices);
	return FinalizePrimitive(meshData, "Sphere", "Sphere"_tgaid, myLoadedModels);
}

bool ModelFactory::InitUnitCylinder()
{
	// Radius 50, height 100 (y in [-50,50]), +Y up, with end caps.
	constexpr int kSegs = 48;
	constexpr float kR = 50.0f;
	constexpr float kHalf = 50.0f;
	constexpr float kTwoPi = 6.28318530718f;

	Model::MeshData meshData = {};
	for (int i = 0; i < kSegs; ++i)
	{
		const float a0 = (float)i / kSegs * kTwoPi;
		const float a1 = (float)(i + 1) / kSegs * kTwoPi;
		const Vector3f n0{ std::cos(a0), 0.0f, std::sin(a0) };
		const Vector3f n1{ std::cos(a1), 0.0f, std::sin(a1) };
		const Vector3f t0{ -std::sin(a0), 0.0f, std::cos(a0) };
		const Vector3f up{ 0.0f, 1.0f, 0.0f };

		const unsigned int base = (unsigned int)meshData.vertices.size();
		meshData.vertices.emplace_back(n0.x * kR, -kHalf, n0.z * kR, n0.x, n0.y, n0.z, t0.x, t0.y, t0.z, up.x, up.y, up.z, 1.0f, 1.0f, 1.0f, 1.0f, (float)i / kSegs, 1.0f);
		meshData.vertices.emplace_back(n0.x * kR,  kHalf, n0.z * kR, n0.x, n0.y, n0.z, t0.x, t0.y, t0.z, up.x, up.y, up.z, 1.0f, 1.0f, 1.0f, 1.0f, (float)i / kSegs, 0.0f);
		meshData.vertices.emplace_back(n1.x * kR,  kHalf, n1.z * kR, n1.x, n1.y, n1.z, t0.x, t0.y, t0.z, up.x, up.y, up.z, 1.0f, 1.0f, 1.0f, 1.0f, (float)(i + 1) / kSegs, 0.0f);
		meshData.vertices.emplace_back(n1.x * kR, -kHalf, n1.z * kR, n1.x, n1.y, n1.z, t0.x, t0.y, t0.z, up.x, up.y, up.z, 1.0f, 1.0f, 1.0f, 1.0f, (float)(i + 1) / kSegs, 1.0f);
		meshData.indices.push_back(base + 0); meshData.indices.push_back(base + 1); meshData.indices.push_back(base + 2);
		meshData.indices.push_back(base + 0); meshData.indices.push_back(base + 2); meshData.indices.push_back(base + 3);

		// caps
		const unsigned int top = (unsigned int)meshData.vertices.size();
		meshData.vertices.emplace_back(0.0f, kHalf, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f);
		meshData.vertices.emplace_back(n0.x * kR, kHalf, n0.z * kR, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, n0.x * 0.5f + 0.5f, n0.z * 0.5f + 0.5f);
		meshData.vertices.emplace_back(n1.x * kR, kHalf, n1.z * kR, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, n1.x * 0.5f + 0.5f, n1.z * 0.5f + 0.5f);
		meshData.indices.push_back(top + 0); meshData.indices.push_back(top + 1); meshData.indices.push_back(top + 2);

		const unsigned int bot = (unsigned int)meshData.vertices.size();
		meshData.vertices.emplace_back(0.0f, -kHalf, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f);
		meshData.vertices.emplace_back(n1.x * kR, -kHalf, n1.z * kR, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, n1.x * 0.5f + 0.5f, n1.z * 0.5f + 0.5f);
		meshData.vertices.emplace_back(n0.x * kR, -kHalf, n0.z * kR, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, n0.x * 0.5f + 0.5f, n0.z * 0.5f + 0.5f);
		meshData.indices.push_back(bot + 0); meshData.indices.push_back(bot + 1); meshData.indices.push_back(bot + 2);
	}

	meshData.bounds = CalculateBoxSphereBounds(meshData.vertices);
	return FinalizePrimitive(meshData, "Cylinder", "Cylinder"_tgaid, myLoadedModels);
}

bool ModelFactory::InitPrimitives()
{
	if (!InitUnitCube())
		return false;

	if (!InitUnitPlane())
		return false;

	if (!InitUnitCone())
		return false;

	if (!InitUnitTorus())
		return false;

	if (!InitUnitSphere())
		return false;

	if (!InitUnitCylinder())
		return false;

	return true;
}

static std::shared_ptr<const Animation> GetAnimationWrapper(std::string_view someFilePath, const std::shared_ptr<const Skeleton>& aSkeleton)
{
	return ModelFactory::GetInstance().GetAnimation(StringRegistry::RegisterOrGetString(someFilePath), aSkeleton);
}

static std::shared_ptr<const Skeleton> GetSkeletonWrapper(std::string_view someFilePath)
{
	std::shared_ptr<Model> model = ModelFactory::GetInstance().GetModel(StringRegistry::RegisterOrGetString(someFilePath));
	return model ? model->GetSkeleton() : nullptr;
}

ModelFactory::ModelFactory()
{
	RegisterGetAnimationFunction(GetAnimationWrapper);
	RegisterGetSkeletonFunction(GetSkeletonWrapper);

#ifndef TGA_USE_UFBX
	TGA::FBX::Importer::InitImporter();
#endif
	InitPrimitives();
	ourInstance = this;
}

ModelFactory::~ModelFactory()
{
	ourInstance = nullptr;
#ifndef TGA_USE_UFBX
	TGA::FBX::Importer::UninitImporter();
#endif

}

static TextureResource *AssignAlbedoTexture(std::string_view baseFileName, std::string_view materialFileName) 
{
	FixedStream<512> stream;
	stream << materialFileName << "_C.dds";
	TextureResource	*albedoTexture = GraphicsEngine::GetInstance()->GetTextureManager().TryGetTexture(stream.GetData());

	if (albedoTexture == nullptr)
	{
		FixedStream<512> streamD;
		streamD << materialFileName << "_D.dds";
		albedoTexture = GraphicsEngine::GetInstance()->GetTextureManager().TryGetTexture(streamD.GetData());
	}

	if (albedoTexture == nullptr)
	{
		FixedStream<512> streamBC;
		streamBC << baseFileName << "_C.dds";
		albedoTexture = GraphicsEngine::GetInstance()->GetTextureManager().TryGetTexture(streamBC.GetData());
	}

	if (albedoTexture == nullptr)
	{
		FixedStream<512> streamBD;
		streamBD << baseFileName << "_D.dds";
		albedoTexture = GraphicsEngine::GetInstance()->GetTextureManager().TryGetTexture(streamBD.GetData());
	}

	if (albedoTexture == nullptr)
	{
		albedoTexture = GraphicsEngine::GetInstance()->GetTextureManager().GetTexture("Textures/T_Default_c.dds");
	}
	return albedoTexture;
}

static TextureResource *AssignNormalTexture(std::string_view baseFileName, std::string_view materialFileName) 
{
	FixedStream<512> streamN;
	streamN << materialFileName << "_N.dds";
	TextureResource *normalTexture = GraphicsEngine::GetInstance()->GetTextureManager().TryGetTexture(streamN.GetData(), TextureSrgbMode::ForceNoSrgbFormat);

	if (normalTexture == nullptr)
	{
		FixedStream<512> streamBN;
		streamBN << baseFileName << "_N.dds";
		normalTexture = GraphicsEngine::GetInstance()->GetTextureManager().TryGetTexture(streamBN.GetData(), TextureSrgbMode::ForceNoSrgbFormat);
	}

	if (normalTexture == nullptr)
		normalTexture = GraphicsEngine::GetInstance()->GetTextureManager().GetTexture("Textures/T_Default_n.dds", TextureSrgbMode::ForceNoSrgbFormat);

	return normalTexture;
}

static TextureResource *AssignMaterialTexture(std::string_view baseFileName, std::string_view materialFileName) 
{
	FixedStream<512> streamM;
	streamM << materialFileName << "_M.dds";
	TextureResource *materialTexture = GraphicsEngine::GetInstance()->GetTextureManager().TryGetTexture(streamM.GetData(), TextureSrgbMode::ForceNoSrgbFormat);

	if (materialTexture == nullptr)
	{
		FixedStream<512> streamBM;
		streamBM << baseFileName << "_M.dds";
		materialTexture = GraphicsEngine::GetInstance()->GetTextureManager().TryGetTexture(streamBM.GetData(), TextureSrgbMode::ForceNoSrgbFormat);
	}

	if (materialTexture == nullptr)
		materialTexture = GraphicsEngine::GetInstance()->GetTextureManager().GetTexture("Textures/T_Default_m.dds", TextureSrgbMode::ForceNoSrgbFormat);

	return materialTexture;
}

static TextureResource *AssignFxTexture(std::string_view baseFileName, std::string_view materialFileName) 
{
	FixedStream<512> streamFX;
	streamFX << materialFileName << "_FX.dds";
	TextureResource *fxTexture = GraphicsEngine::GetInstance()->GetTextureManager().TryGetTexture(streamFX.GetData(), TextureSrgbMode::ForceNoSrgbFormat);

	if (fxTexture == nullptr)
	{
		FixedStream<512> streamBFX;
		streamBFX << baseFileName << "_FX.dds";
		fxTexture = GraphicsEngine::GetInstance()->GetTextureManager().TryGetTexture(streamBFX.GetData(), TextureSrgbMode::ForceNoSrgbFormat);
	}

	if (fxTexture == nullptr)
		fxTexture = GraphicsEngine::GetInstance()->GetTextureManager().GetTexture("Textures/T_Default_fx.dds", TextureSrgbMode::ForceNoSrgbFormat);
	return fxTexture;
}

void AssignDefaultMaterials(std::string_view someFilePath, Model* aModel)
{
	size_t dotPos = someFilePath.find_last_of('.');
	std::string_view baseFileName = (dotPos != std::string_view::npos) ? someFilePath.substr(0, dotPos) : someFilePath;
	size_t slashPos = someFilePath.find_last_of("/\\");
	std::string_view path = (slashPos != std::string_view::npos) ? someFilePath.substr(0, slashPos + 1) : "";

	for (int i = 0; i < aModel->GetMeshCount(); i++)
	{
		FixedStream<512> materialFileNameStream;
		materialFileNameStream << path << aModel->GetMaterialName(i);

		if (std::getenv("TGE_LOG_MATERIALS"))
		{
			FixedStream<512> pc; pc << materialFileNameStream.GetStringView() << "_C.dds";
			FixedStream<512> pn; pn << materialFileNameStream.GetStringView() << "_N.dds";
			FixedStream<512> pm; pm << materialFileNameStream.GetStringView() << "_M.dds";
			auto& tm = GraphicsEngine::GetInstance()->GetTextureManager();
			INFO_PRINT("  mat[%2d] %-28s  C:%s N:%s M:%s", i, aModel->GetMaterialName(i).GetString(),
				tm.TryGetTexture(pc.GetData()) ? "ok" : "--",
				tm.TryGetTexture(pn.GetData(), TextureSrgbMode::ForceNoSrgbFormat) ? "ok" : "--",
				tm.TryGetTexture(pm.GetData(), TextureSrgbMode::ForceNoSrgbFormat) ? "ok" : "--");
		}

		TextureResource* albedoTexture = AssignAlbedoTexture(baseFileName, materialFileNameStream.GetStringView());
		aModel->SetDefaultTexture(i, 0, albedoTexture);

		TextureResource* normalTexture = AssignNormalTexture(baseFileName, materialFileNameStream.GetStringView());
		aModel->SetDefaultTexture(i, 1, normalTexture);

		TextureResource* materialTexture = AssignMaterialTexture(baseFileName, materialFileNameStream.GetStringView());
		aModel->SetDefaultTexture(i, 2, materialTexture);

		TextureResource* fxTexture = AssignFxTexture(baseFileName, materialFileNameStream.GetStringView());
		aModel->SetDefaultTexture(i, 3, fxTexture);
	}
}


ModelInstance ModelFactory::GetModelInstance(StringId someFilePath)
{
	// This needs to be moved to separate memory structures at some point.
	ModelInstance meshInstance;

	std::shared_ptr<Model> model = GetModel(someFilePath);
	if (!model)
		return meshInstance;

	meshInstance.Init(model);

	return meshInstance;
		}

AnimatedModelInstance ModelFactory::GetAnimatedModelInstance(StringId someFilePath)
{
	// This needs to be moved to separate memory structures at some point.
	AnimatedModelInstance meshInstance;

	std::shared_ptr<Model> model = GetModel(someFilePath);
	if (!model || !model->GetSkeleton()->GetRoot())
		return meshInstance;

	meshInstance.Init(model);

	return meshInstance;
}

ModelInstance ModelFactory::GetUnitCube()
{
	return GetModelInstance("Cube"_tgaid);
}

ModelInstance ModelFactory::GetUnitPlane()
{
	return GetModelInstance("Plane"_tgaid);
}

#ifdef TGA_USE_UFBX
static Matrix4x4f ConvertMatrix(const ufbx_matrix& m)
{
	Matrix4x4f mat;

	mat(1, 1) = (float)m.m00; mat(1, 2) = (float)m.m10; mat(1, 3) = (float)m.m20; mat(1, 4) = 0.f;
	mat(2, 1) = (float)m.m01; mat(2, 2) = (float)m.m11; mat(2, 3) = (float)m.m21; mat(2, 4) = 0.f;
	mat(3, 1) = (float)m.m02; mat(3, 2) = (float)m.m12; mat(3, 3) = (float)m.m22; mat(3, 4) = 0.f;
	mat(4, 1) = (float)m.m03; mat(4, 2) = (float)m.m13; mat(4, 3) = (float)m.m23; mat(4, 4) = 1.f;

	return mat;
}

static constexpr ufbx_coordinate_axes FBX_AXIS = {
	UFBX_COORDINATE_AXIS_POSITIVE_X, UFBX_COORDINATE_AXIS_POSITIVE_Y, UFBX_COORDINATE_AXIS_POSITIVE_Z,
};

static void GenerateTangents(std::vector<Vertex>& verts, const std::vector<uint32_t>& indices)
{
	// Quick implementation of tangent computation in cases they are missing
	// Verified to match imported tangent orientation on particle_chest

	for (auto& v : verts)
	{
		v.tangent = { 0,0,0 };
		v.binormal = { 0,0,0 };
	}

	// Accumulation across triangles
	// TODO: should probably have some kind of angle based weighting?
	for (size_t i = 0; i < indices.size(); i += 3)
	{
		uint32_t i0 = indices[i + 0];
		uint32_t i1 = indices[i + 1];
		uint32_t i2 = indices[i + 2];

		Vertex& v0 = verts[i0];
		Vertex& v1 = verts[i1];
		Vertex& v2 = verts[i2];

		const auto& p0 = v0.position;
		const auto& p1 = v1.position;
		const auto& p2 = v2.position;

		const auto& uv0 = v0.uvs[0];
		const auto& uv1 = v1.uvs[0];
		const auto& uv2 = v2.uvs[0];

		Tga::Vector3f dp1 = p1 - p0;
		Tga::Vector3f dp2 = p2 - p0;

		Tga::Vector2f duv1 = uv1 - uv0;
		Tga::Vector2f duv2 = uv2 - uv0;

		float denom = duv1.x * duv2.y - duv1.y * duv2.x;
		if (fabs(denom) < 1e-6f)	continue;

		float r = 1.0f / denom;

		Tga::Vector3f tangent = (dp1 * duv2.y - dp2 * duv1.y) * r;
		Tga::Vector3f bitangent = (dp2 * duv1.x - dp1 * duv2.x) * r;

		v0.tangent += tangent;
		v1.tangent += tangent;
		v2.tangent += tangent;

		v0.binormal += bitangent;
		v1.binormal += bitangent;
		v2.binormal += bitangent;
	}

	for (auto& v : verts)
	{
		Tga::Vector3f n = v.normal;
		Tga::Vector3f t = v.tangent;

		if (t.LengthSqr() < 1e-6f)
		{
			// fallback: build arbitrary tangent to avoid broken meshes
			Tga::Vector3f up = fabs(n.z) < 0.999f ? Tga::Vector3f{ 0,0,1 } : Tga::Vector3f{ 0,1,0 };
			t = n.Cross(up).GetNormalized();
		}
		else
		{
			// Gram-Schmidt
			t = (t - n * n.Dot(t)).GetNormalized();
		}

		// compute handedness:
		Tga::Vector3f b = v.binormal;
		float w = (n.Cross(t).Dot(b) < 0.0f) ? 1.0f : -1.0f;

		v.tangent = t;
		v.binormal = n.Cross(t) * w;
	}
}



std::shared_ptr<Model> ModelFactory::LoadModel(StringId someFilePath)
{
	if (someFilePath.IsEmpty())
		return nullptr;
	FilePathStream resolved_path;
	if (!Tga::Settings::ResolveAssetPath(someFilePath, resolved_path))
		return nullptr;

	ufbx_load_opts opts = 
	{
		.generate_missing_normals = true,
		.target_axes = FBX_AXIS,
	};


	ufbx_error error;
	ufbx_scene* scene = ufbx_load_file(resolved_path.GetData(), &opts, &error);

	if (!scene)
	{
		ERROR_PRINT("ufbx load failed: %s", someFilePath.GetString());
		return nullptr;
	}

	std::vector<Model::MeshData> mdlMeshData;

	Skeleton mdlSkeleton;

	std::unordered_map<const ufbx_node*, int> nodeToJoint;
	std::vector<const ufbx_node*> jointNodes;

	// Skeleton
	for (size_t mesh_i = 0; mesh_i < scene->meshes.count; mesh_i++)
	{
		ufbx_mesh* mesh = scene->meshes.data[mesh_i];

		if (mesh->skin_deformers.count == 0)
			continue;

		ufbx_skin_deformer* skin = mesh->skin_deformers.data[0];

		for (size_t c = 0; c < skin->clusters.count; c++)
		{
			ufbx_skin_cluster* cluster = skin->clusters.data[c];
			if (!cluster->bone_node) continue;

			const ufbx_node* node = cluster->bone_node;

			if (nodeToJoint.contains(node))
				continue;

			int index = (int)mdlSkeleton.joints.size();
			nodeToJoint[node] = index;

			Skeleton::Joint& joint = mdlSkeleton.joints.emplace_back();
			jointNodes.push_back(node);

			joint.name = node->name.data ? node->name.data : "";

			if (node->parent && nodeToJoint.contains(node->parent))
				joint.parent = nodeToJoint[node->parent];
			else
				joint.parent = -1;

			mdlSkeleton.jointNameToIndex[joint.name] = index;
		}
	}

	ufbx_matrix skeletonParent = ufbx_identity_matrix;

	if (jointNodes.size() > 1)
	{
		const ufbx_node* skeletonRoot = jointNodes[0];
		if (skeletonRoot && skeletonRoot->parent)
		{
			skeletonParent = skeletonRoot->parent->node_to_world;
		}

		for (size_t mesh_i = 0; mesh_i < scene->meshes.count; mesh_i++)
		{
			ufbx_mesh* mesh = scene->meshes.data[mesh_i];
			if (mesh->skin_deformers.count == 0) continue;

			ufbx_skin_deformer* skin = mesh->skin_deformers.data[0];

			for (size_t c = 0; c < skin->clusters.count; c++)
			{
				ufbx_skin_cluster* cluster = skin->clusters.data[c];
				if (!cluster->bone_node) continue;

				int jointIndex = nodeToJoint[cluster->bone_node];

				ufbx_matrix geomToBone = cluster->geometry_to_bone;
				
				// Apply transform above the skeleton root (blender tends to add this)
				ufbx_matrix fixed = ufbx_matrix_mul(&skeletonParent, &geomToBone);

				mdlSkeleton.joints[jointIndex].bindPoseInverse = ConvertMatrix(fixed);

				mdlSkeleton.modelBindPose.jointTransforms[jointIndex] =
					mdlSkeleton.joints[jointIndex].bindPoseInverse.GetInverse();
			}
		}

		assert(MAX_ANIMATION_BONES >= mdlSkeleton.joints.size() && "More joints in animation than defined in EngineDefines.h");

		mdlSkeleton.ConvertPoseToLocalSpace(mdlSkeleton.modelBindPose, mdlSkeleton.localBindPose);
	}

	// Build hierarchy
	for (size_t i = 0; i < mdlSkeleton.joints.size(); i++)
	{
		int parent = mdlSkeleton.joints[i].parent;
		if (parent >= 0)
			mdlSkeleton.joints[parent].children.push_back((unsigned)i);
	}

	// Meshes
	for (size_t mesh_i = 0; mesh_i < scene->meshes.count; mesh_i++)
	{
		ufbx_mesh* mesh = scene->meshes.data[mesh_i];

		Model::MeshData meshData;

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		std::vector<VertexBoneData> boneData(mesh->num_vertices);

		// Skinning
		if (mesh->skin_deformers.count > 0)
		{
			ufbx_skin_deformer* skin = mesh->skin_deformers.data[0];

			for (size_t v = 0; v < skin->vertices.count; v++)
			{
				const ufbx_skin_vertex& sv = skin->vertices.data[v];

				for (uint32_t w = 0; w < sv.num_weights; w++)
				{
					const ufbx_skin_weight& weight =
						skin->weights.data[sv.weight_begin + w];

					ufbx_skin_cluster* cluster =
						skin->clusters.data[weight.cluster_index];

					if (!cluster->bone_node) continue;
					if (!nodeToJoint.count(cluster->bone_node)) continue;

					unsigned int boneIndex = nodeToJoint[cluster->bone_node];
					boneData[v].AddBoneData(boneIndex, (float)weight.weight);
				}
			}
		}

		// Triangulation
		std::vector<uint32_t> triBuf(mesh->max_face_triangles * 3);

		for (size_t f = 0; f < mesh->faces.count; f++)
		{
			ufbx_face face = mesh->faces.data[f];

			uint32_t numTris = ufbx_triangulate_face(
				triBuf.data(),
				triBuf.size(),
				mesh,
				face
			);

			for (uint32_t t = 0; t < numTris * 3; t++)
			{
				uint32_t ix = triBuf[t];

				Vertex vert{};

				auto pos = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
				vert.position = { (float)pos.x,(float)pos.y,(float)pos.z,1 };

				if (mesh->vertex_normal.exists)
				{
					auto n = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
					vert.normal = { (float)n.x,(float)n.y,(float)n.z };
				}

				if (mesh->vertex_tangent.exists)
				{
					auto v = ufbx_get_vertex_vec3(&mesh->vertex_tangent, ix);
					vert.tangent = { (float)v.x,(float)v.y,(float)v.z };
				}

				if (mesh->vertex_bitangent.exists)
				{
					auto b = ufbx_get_vertex_vec3(&mesh->vertex_bitangent, ix);
					vert.binormal = { (float)b.x,(float)b.y,(float)b.z };
				}

				if (mesh->vertex_uv.exists)
				{
					auto uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
					vert.uvs[0] = { (float)uv.x,1.f - (float)uv.y };
				}

				vert.vertexColors[0] = { 1,1,1,1 };

				uint32_t vIndex = mesh->vertex_indices.data[ix];

				vert.bones = {
					(float)boneData[vIndex].IDs[0],
					(float)boneData[vIndex].IDs[1],
					(float)boneData[vIndex].IDs[2],
					(float)boneData[vIndex].IDs[3]
				};

				vert.weights = {
					boneData[vIndex].Weights[0],
					boneData[vIndex].Weights[1],
					boneData[vIndex].Weights[2],
					boneData[vIndex].Weights[3]
				};

				indices.push_back((uint32_t)vertices.size());
				vertices.push_back(vert);
			}
		}

		if (!mesh->vertex_tangent.exists)
		{
			GenerateTangents(vertices, indices);
		}

		meshData.vertices = std::move(vertices);
		meshData.indices = std::move(indices);

		meshData.bounds = CalculateBoxSphereBounds(meshData.vertices);
		meshData.name = mesh->name.data ? StringRegistry::RegisterOrGetString(mesh->name.data) : "Mesh"_tgaid;

		rhi::BufferDesc vbDesc{};
		vbDesc.byteSize = UINT(meshData.vertices.size() * sizeof(Vertex));
		vbDesc.stride = sizeof(Vertex);
		vbDesc.usage = rhi::BufferUsage::Vertex;
		vbDesc.memory = rhi::MemoryType::Default;
		vbDesc.debugName = "Mesh_VB";

		rhi::BufferHandle vb = DX11::Rhi()->CreateBuffer(vbDesc, meshData.vertices.data());
		if (!vb.IsValid()) return nullptr;

		rhi::BufferDesc ibDesc{};
		ibDesc.byteSize = UINT(meshData.indices.size() * sizeof(uint32_t));
		ibDesc.stride = sizeof(uint32_t);
		ibDesc.usage = rhi::BufferUsage::Index;
		ibDesc.memory = rhi::MemoryType::Default;
		ibDesc.debugName = "Mesh_IB";

		rhi::BufferHandle ib = DX11::Rhi()->CreateBuffer(ibDesc, meshData.indices.data());
		if (!ib.IsValid()) return nullptr;

		meshData.vertexBuffer = vb;
		meshData.indexBuffer = ib;
		meshData.numberOfVertices = (UINT)meshData.vertices.size();
		meshData.numberOfIndices = (UINT)meshData.indices.size();
		meshData.stride = sizeof(Vertex);
		meshData.offset = 0;

		mdlMeshData.push_back(std::move(meshData));
	}

	auto model = std::make_shared<Model>();
	model->Init(mdlMeshData, std::string(resolved_path.GetStringView()));

	if (!mdlSkeleton.joints.empty())
		model->mySkeleton = std::make_shared<Skeleton>(std::move(mdlSkeleton));

	AssignDefaultMaterials(someFilePath, model.get());
	myLoadedModels[someFilePath] = model;

	ufbx_free_scene(scene);
	return model;
}


std::shared_ptr<const Animation> ModelFactory::GetAnimation(
	StringId someFilePath,
	const std::shared_ptr<const Skeleton>& aSkeleton)
{
	if (someFilePath.IsEmpty())
		return nullptr;

	FilePathStream resolvedPath;
	if (!Tga::Settings::ResolveAssetPath(someFilePath, resolvedPath))
		return nullptr;

	StringId resolvedPathId = StringRegistry::RegisterOrGetString(resolvedPath.GetStringView());

	auto it = myLoadedAnimations.find(AnimationIdentifer{ resolvedPathId, aSkeleton });
	if (it != myLoadedAnimations.end())
		return it->second;

	ufbx_load_opts opts = {
		.target_axes = FBX_AXIS,
	};
	ufbx_error error;
	ufbx_scene* scene = ufbx_load_file(resolvedPath.GetData(), &opts, &error);

	if (!scene) return nullptr;
	if (scene->anim_stacks.count == 0) return nullptr;

	ufbx_anim_stack* stack = scene->anim_stacks.data[0];
	ufbx_anim* anim = stack->anim;

	std::shared_ptr<Animation> animation = std::make_shared<Animation>();

	animation->name = someFilePath.GetString();

	float fps = 30.0f;
	animation->framesPerSecond = fps;

	animation->duration = (float)(stack->time_end - stack->time_begin);
	animation->length = (unsigned)(animation->duration * fps);

	animation->frames.resize(animation->length);

	const Skeleton& skeleton = *aSkeleton;

	ufbx_matrix skeletonParent = ufbx_identity_matrix;
	ufbx_matrix skeletonParentInv = ufbx_identity_matrix;

	// resolve root once
	const ufbx_node* skeletonRoot = ufbx_find_node(scene, skeleton.joints[0].name.c_str());
	if (skeletonRoot)
	{
		if (skeletonRoot->parent)
		{
			skeletonParent = skeletonRoot->parent->node_to_world;
			skeletonParentInv = ufbx_matrix_invert(&skeletonParent);
		}
	}
	for (unsigned int f = 0; f < animation->length; f++)
	{
		double time = stack->time_begin + (double)f / fps;

		ufbx_scene* eval = ufbx_evaluate_scene(scene, anim, time, nullptr, nullptr);

		animation->frames[f].count = skeleton.joints.size();

		for (size_t j = 0; j < skeleton.joints.size(); j++)
		{
			const std::string& jointName = skeleton.joints[j].name;

			ufbx_node* sourceNode = ufbx_find_node(scene, jointName.c_str());
			if (!sourceNode)
				continue;

			ufbx_node* node = eval->nodes[sourceNode->typed_id];

			ufbx_matrix original = node->node_to_parent;

 			// Compensate for transforms in nodes above the skeleton (blender tends to create this)
			ufbx_matrix tmp = ufbx_matrix_mul(&original, &skeletonParentInv);
			ufbx_matrix fixed = ufbx_matrix_mul(&skeletonParent, &tmp);

			Matrix4x4f mat = ConvertMatrix(fixed);

			animation->frames[f].jointTransforms[j] = ScaleRotationTranslationf::CreateFromMatrix(mat);
			
		}

		ufbx_free_scene(eval);
	}

	myLoadedAnimations[AnimationIdentifer{ resolvedPathId, aSkeleton }] = animation;

	ufbx_free_scene(scene);
	return animation;
}

#else

// ---------------------------------------------------------------------------
//  Static-mesh import cache.  Parsing a large .fbx via the FBX SDK is slow
//  (seconds). After the first import we serialise the converted MeshData to
//  <CookedAssets>/meshcache/<asset path>.tgmesh; later loads skip the SDK
//  entirely. Invalidated by the .fbx's write time + size. Skinned meshes are
//  not cached (kept on the SDK path) so animation import is unaffected.
// ---------------------------------------------------------------------------
namespace
{
	constexpr uint32_t kMeshCacheMagic   = 0x434D4754u; // 'TGMC'
	constexpr uint32_t kMeshCacheVersion = 3u;           // v3: sub-meshes merged by material at import

	// Collapse sub-meshes that share a material into one vertex/index buffer.
	// Sponza: 120 sub-meshes -> ~24 draws. Loses per-sub-mesh granularity (the
	// model draw path has no per-sub-mesh culling), keeps one name per material.
	constexpr bool kMergeByMaterial = true;

	Tga::BoxSphereBounds BoundsOf(const std::vector<Tga::Vertex>& v)
	{
		Tga::Vector3f mn{ 1e30f, 1e30f, 1e30f }, mx{ -1e30f, -1e30f, -1e30f };
		for (const Tga::Vertex& vert : v)
		{
			mn.x = std::min(mn.x, vert.position.x); mx.x = std::max(mx.x, vert.position.x);
			mn.y = std::min(mn.y, vert.position.y); mx.y = std::max(mx.y, vert.position.y);
			mn.z = std::min(mn.z, vert.position.z); mx.z = std::max(mx.z, vert.position.z);
		}
		if (v.empty()) { mn = { 0,0,0 }; mx = { 0,0,0 }; }
		Tga::BoxSphereBounds b{};
		b.center = (mn + mx) * 0.5f;
		b.boxExtents = (mx - mn) * 0.5f;
		b.radius = std::sqrt(b.boxExtents.x * b.boxExtents.x + b.boxExtents.y * b.boxExtents.y + b.boxExtents.z * b.boxExtents.z);
		return b;
	}

	// Most static meshes only use position / normal / tangent / binormal / uv0.
	// Those are stored as 15 floats/vertex instead of the full ~208-byte Vertex.
	// A mesh that actually uses vertex colours, extra UV sets or skin weights is
	// flagged and stored in full, so correctness never depends on the guess.
	constexpr uint32_t kCompactFloats = 15u;

	bool VertexIsCompactSafe(const Tga::Vertex& v)
	{
		auto nz2 = [](const Tga::Vector2f& a) { return a.x != 0.f || a.y != 0.f; };
		auto nz4 = [](const Tga::Vector4f& a) { return a.x != 0.f || a.y != 0.f || a.z != 0.f || a.w != 0.f; };
		if (nz4(v.vertexColors[0]) || nz4(v.vertexColors[1]) || nz4(v.vertexColors[2]) || nz4(v.vertexColors[3])) return false;
		if (nz2(v.uvs[1]) || nz2(v.uvs[2]) || nz2(v.uvs[3])) return false;
		if (nz4(v.bones) || nz4(v.weights)) return false;
		return true;
	}

	std::string MeshCachePath(const char* assetPath)
	{
		std::string key(assetPath ? assetPath : "");
		for (char& c : key) if (c == '/' || c == '\\' || c == ':') c = '_';
		std::string root = Tga::Settings::CookedAssetRoot();
		if (root.empty()) root = ".";
		return root + "/meshcache/" + key + ".tgmesh";
	}

	template <class T> void CacheW(std::ofstream& o, const T& v) { o.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
	template <class T> void CacheR(std::ifstream& i, T& v)       { i.read (reinterpret_cast<char*>(&v), sizeof(T)); }
	void CacheWStr(std::ofstream& o, const std::string& s) { uint32_t n = (uint32_t)s.size(); CacheW(o, n); if (n) o.write(s.data(), (std::streamsize)n); }
	std::string CacheRStr(std::ifstream& i) { uint32_t n = 0; CacheR(i, n); std::string s(n, '\0'); if (n) i.read(s.data(), (std::streamsize)n); return s; }

	bool CacheFileStamp(const char* path, int64_t& outTime, uint64_t& outSize)
	{
		std::error_code ec;
		const std::filesystem::path p(path);
		const auto t = std::filesystem::last_write_time(p, ec); if (ec) return false;
		const auto s = std::filesystem::file_size(p, ec);       if (ec) return false;
		outTime = (int64_t)t.time_since_epoch().count();
		outSize = (uint64_t)s;
		return true;
	}

	bool CacheCreateBuffers(Tga::Model::MeshData& md)
	{
		md.vertexBuffer = {};
		md.indexBuffer  = {};
		md.numberOfVertices = 0;
		md.numberOfIndices  = 0;
		md.stride = sizeof(Tga::Vertex);
		md.offset = 0;

		if (md.vertices.empty() || md.indices.empty())
		{
			ERROR_PRINT("mesh '%s': empty geometry (%zu verts, %zu indices) - skipped",
				md.name.GetString(), md.vertices.size(), md.indices.size());
			return false;
		}

		const uint64_t vbytes = (uint64_t)md.vertices.size() * sizeof(Tga::Vertex);
		if (vbytes > 0xFFFFFFFFull)
		{
			ERROR_PRINT("mesh '%s': vertex buffer %llu bytes exceeds 4 GB - skipped", md.name.GetString(), vbytes);
			return false;
		}

		Tga::rhi::BufferDesc vbd{};
		vbd.byteSize = (UINT)vbytes;
		vbd.stride = sizeof(Tga::Vertex);
		vbd.usage = Tga::rhi::BufferUsage::Vertex;
		vbd.memory = Tga::rhi::MemoryType::Default;
		vbd.debugName = "Mesh_VB";
		Tga::rhi::IDevice* dev = Tga::DX11::Rhi();
		Tga::rhi::BufferHandle vb = dev->CreateBuffer(vbd, md.vertices.data());
		if (!vb.IsValid()) { ERROR_PRINT("mesh '%s': vertex buffer create failed", md.name.GetString()); return false; }

		Tga::rhi::BufferDesc ibd{};
		ibd.byteSize = (UINT)(md.indices.size() * sizeof(unsigned int));
		ibd.stride = sizeof(unsigned int);
		ibd.usage = Tga::rhi::BufferUsage::Index;
		ibd.memory = Tga::rhi::MemoryType::Default;
		ibd.debugName = "Mesh_IB";
		Tga::rhi::BufferHandle ib = dev->CreateBuffer(ibd, md.indices.data());
		if (!ib.IsValid()) { ERROR_PRINT("mesh '%s': index buffer create failed", md.name.GetString()); dev->Destroy(vb); return false; }

		md.numberOfVertices = (UINT)md.vertices.size();
		md.numberOfIndices  = (UINT)md.indices.size();
		md.vertexBuffer = vb;
		md.indexBuffer  = ib;
		return true;
	}

	// Merge sub-meshes with the same material name; rebuilds GPU buffers for the
	// merged set and releases the originals. Safe to run on MeshData that already
	// has buffers (FBX path) or none yet (cache path).
	void MergeMeshesByMaterial(std::vector<Tga::Model::MeshData>& meshes)
	{
		if (!kMergeByMaterial || meshes.size() < 2) return;

		std::vector<Tga::Model::MeshData> out;
		out.reserve(meshes.size());
		std::unordered_map<std::string, size_t> byMat;

		size_t droppedEmpty = 0;
		for (Tga::Model::MeshData& m : meshes)
		{
			if (m.vertices.empty() || m.indices.empty()) { ++droppedEmpty; }

			const std::string key(m.materialName.GetStringView());
			auto it = byMat.find(key);
			if (it == byMat.end())
			{
				byMat.emplace(key, out.size());
				Tga::Model::MeshData nm;
				nm.name         = m.name;
				nm.materialName  = m.materialName;
				nm.vertexBuffer = {};
				nm.indexBuffer  = {};
				nm.vertices      = std::move(m.vertices);
				nm.indices       = std::move(m.indices);
				out.push_back(std::move(nm));
			}
			else
			{
				Tga::Model::MeshData& dst = out[it->second];
				const uint32_t base = (uint32_t)dst.vertices.size();
				dst.vertices.insert(dst.vertices.end(), m.vertices.begin(), m.vertices.end());
				dst.indices.reserve(dst.indices.size() + m.indices.size());
				for (unsigned int idx : m.indices) dst.indices.push_back(base + idx);
			}

			Tga::rhi::IDevice* dev = Tga::DX11::Rhi();
			if (m.vertexBuffer.IsValid()) { dev->Destroy(m.vertexBuffer); m.vertexBuffer = {}; }
			if (m.indexBuffer.IsValid())  { dev->Destroy(m.indexBuffer);  m.indexBuffer  = {}; }
		}

		size_t okMeshes = 0, totalTris = 0;
		for (Tga::Model::MeshData& md : out)
		{
			md.bounds = BoundsOf(md.vertices);
			if (CacheCreateBuffers(md)) { ++okMeshes; totalTris += md.indices.size() / 3; }
		}

		INFO_PRINT("mesh merge: %zu sub-meshes (%zu empty) -> %zu materials, %zu with geometry, %zu tris",
			meshes.size(), droppedEmpty, out.size(), okMeshes, totalTris);
		meshes = std::move(out);
	}

	bool TryLoadMeshCache(const std::string& cachePath, const char* fbxPath,
	                      Tga::Model* outModel, const std::string& modelPathForInit)
	{
		int64_t fbxTime = 0; uint64_t fbxSize = 0;
		if (!CacheFileStamp(fbxPath, fbxTime, fbxSize)) return false;

		std::ifstream in(cachePath, std::ios::binary);
		if (!in) return false;

		uint32_t magic = 0, version = 0; CacheR(in, magic); CacheR(in, version);
		if (magic != kMeshCacheMagic || version != kMeshCacheVersion) return false;
		int64_t cachedTime = 0; uint64_t cachedSize = 0; CacheR(in, cachedTime); CacheR(in, cachedSize);
		if (cachedTime != fbxTime || cachedSize != fbxSize) return false;

		uint32_t meshCount = 0; CacheR(in, meshCount);
		if (meshCount == 0 || meshCount > MAX_MESHES_PER_MODEL) return false;

		std::vector<Tga::Model::MeshData> meshes(meshCount);
		std::vector<float> scratch;
		for (uint32_t m = 0; m < meshCount; ++m)
		{
			Tga::Model::MeshData& md = meshes[m];
			md.name         = Tga::StringRegistry::RegisterOrGetString(CacheRStr(in));
			md.materialName  = Tga::StringRegistry::RegisterOrGetString(CacheRStr(in));
			CacheR(in, md.bounds);

			uint8_t layout = 0; CacheR(in, layout);   // 0 compact, 1 full
			uint32_t vc = 0; CacheR(in, vc); md.vertices.assign(vc, Tga::Vertex{});
			if (layout == 1)
			{
				if (vc) in.read(reinterpret_cast<char*>(md.vertices.data()), (std::streamsize)vc * sizeof(Tga::Vertex));
			}
			else
			{
				scratch.resize((size_t)vc * kCompactFloats);
				if (vc) in.read(reinterpret_cast<char*>(scratch.data()), (std::streamsize)scratch.size() * sizeof(float));
				for (uint32_t v = 0; v < vc; ++v)
				{
					const float* f = &scratch[(size_t)v * kCompactFloats];
					Tga::Vertex& o = md.vertices[v];
					o.position = { f[0], f[1], f[2], 1.0f };
					o.normal   = { f[4], f[5], f[6] };
					o.tangent  = { f[7], f[8], f[9] };
					o.binormal = { f[10], f[11], f[12] };
					o.uvs[0]   = { f[13], f[14] };
				}
			}

			uint32_t ic = 0; CacheR(in, ic); md.indices.resize(ic);
			if (ic) in.read(reinterpret_cast<char*>(md.indices.data()), (std::streamsize)ic * sizeof(unsigned int));
			if (!in) return false;
			CacheCreateBuffers(md);   // a failed mesh keeps null buffers and is skipped at draw time
		}

		outModel->Init(meshes, modelPathForInit);
		return true;
	}

	void WriteMeshCache(const std::string& cachePath, const char* fbxPath,
	                    const std::vector<Tga::Model::MeshData>& meshes)
	{
		int64_t fbxTime = 0; uint64_t fbxSize = 0;
		if (!CacheFileStamp(fbxPath, fbxTime, fbxSize)) return;
		if (meshes.empty() || meshes.size() > MAX_MESHES_PER_MODEL) return;

		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(cachePath).parent_path(), ec);

		std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
		if (!out) { INFO_PRINT("mesh cache: could not write %s", cachePath.c_str()); return; }
		CacheW(out, kMeshCacheMagic); CacheW(out, kMeshCacheVersion);
		CacheW(out, fbxTime); CacheW(out, fbxSize);
		const uint32_t meshCount = (uint32_t)meshes.size(); CacheW(out, meshCount);
		std::vector<float> scratch;
		for (const auto& md : meshes)
		{
			CacheWStr(out, std::string(md.name.GetStringView()));
			CacheWStr(out, std::string(md.materialName.GetStringView()));
			CacheW(out, md.bounds);

			bool compact = true;
			for (const Tga::Vertex& v : md.vertices) { if (!VertexIsCompactSafe(v)) { compact = false; break; } }
			const uint8_t layout = compact ? 0u : 1u; CacheW(out, layout);

			const uint32_t vc = (uint32_t)md.vertices.size(); CacheW(out, vc);
			if (compact)
			{
				scratch.resize((size_t)vc * kCompactFloats);
				for (uint32_t v = 0; v < vc; ++v)
				{
					const Tga::Vertex& s = md.vertices[v];
					float* f = &scratch[(size_t)v * kCompactFloats];
					f[0] = s.position.x; f[1] = s.position.y; f[2] = s.position.z; f[3] = s.position.w;
					f[4] = s.normal.x;   f[5] = s.normal.y;   f[6] = s.normal.z;
					f[7] = s.tangent.x;  f[8] = s.tangent.y;  f[9] = s.tangent.z;
					f[10] = s.binormal.x; f[11] = s.binormal.y; f[12] = s.binormal.z;
					f[13] = s.uvs[0].x;  f[14] = s.uvs[0].y;
				}
				if (vc) out.write(reinterpret_cast<const char*>(scratch.data()), (std::streamsize)scratch.size() * sizeof(float));
			}
			else if (vc)
			{
				out.write(reinterpret_cast<const char*>(md.vertices.data()), (std::streamsize)vc * sizeof(Tga::Vertex));
			}

			const uint32_t ic = (uint32_t)md.indices.size(); CacheW(out, ic);
			if (ic) out.write(reinterpret_cast<const char*>(md.indices.data()), (std::streamsize)ic * sizeof(unsigned int));
		}
	}
}

std::shared_ptr<Model> ModelFactory::LoadModel(StringId someFilePath)
{
	if (someFilePath.IsEmpty())
		return nullptr;

	FilePathStream resolved_path;
	if (!Tga::Settings::ResolveAssetPath(someFilePath, resolved_path))
		return nullptr;

	if (myWatchedPaths.find(someFilePath) == myWatchedPaths.end())
	{
		myWatchedPaths.insert(someFilePath);
		Application::GetInstance()->GetFileWatcher()->WatchFileChange(resolved_path.GetStringView(), std::bind(&Tga::ModelFactory::OnModelChanged, this, someFilePath));
	}

	const std::string resolvedStr = std::string(resolved_path.GetStringView());
	const std::string cachePath = MeshCachePath(someFilePath.GetString());
	{
		auto cachedModel = std::make_shared<Model>();
		if (TryLoadMeshCache(cachePath, resolved_path.GetData(), cachedModel.get(), resolvedStr))
		{
			AssignDefaultMaterials(someFilePath, cachedModel.get());
			myLoadedModels.insert(std::pair<StringId, std::shared_ptr<Model>>(someFilePath, cachedModel));
			return cachedModel;
		}
	}

	TGA::FBX::Mesh tgaModel;
	if (TGA::FBX::Importer::LoadMeshA(resolved_path.GetData(), tgaModel))
	{
		Skeleton mdlSkeleton;

		if (tgaModel.Skeleton.GetRoot())
		{
			mdlSkeleton.joints.resize(tgaModel.Skeleton.Bones.size());
			mdlSkeleton.jointNameToIndex.reserve(mdlSkeleton.joints.size());
			mdlSkeleton.jointNames.resize(mdlSkeleton.joints.size());
			for (size_t j = 0; j < tgaModel.Skeleton.Bones.size(); j++)
			{
				Skeleton::Joint& mdlJoint = mdlSkeleton.joints[j];
				TGA::FBX::Skeleton::Bone& tgaJoint = tgaModel.Skeleton.Bones[j];

				Matrix4x4f bindPoseInverseTranspose;
				memcpy(&bindPoseInverseTranspose, &tgaJoint.BindPoseInverse, sizeof(float) * 16);

				mdlJoint.bindPoseInverse = Matrix4x4f::Transpose(bindPoseInverseTranspose);

				mdlSkeleton.modelBindPose.jointTransforms[j] = mdlJoint.bindPoseInverse.GetInverse();

				mdlJoint.name = tgaJoint.Name;
				mdlJoint.parent = tgaJoint.ParentIdx;
				mdlJoint.children = tgaJoint.Children;

				mdlSkeleton.jointNameToIndex.insert({ mdlJoint.name, j });
				mdlSkeleton.jointNames[j] = mdlJoint.name;
			}
			assert(MAX_ANIMATION_BONES >= mdlSkeleton.joints.size() && "More joints in animation than defined in EngineDefines.h");

			mdlSkeleton.ConvertPoseToLocalSpace(mdlSkeleton.modelBindPose, mdlSkeleton.localBindPose);
		}

		std::vector<Model::MeshData> mdlMeshData;
		mdlMeshData.resize(tgaModel.Elements.size());

		// Convert model to our own format.
		for (size_t i = 0; i < tgaModel.Elements.size(); i++)
		{
			// The imported element data.
			TGA::FBX::Mesh::Element& element = tgaModel.Elements[i];

			// And where we'll put it in our structures.
			Model::MeshData& meshData = mdlMeshData[i];
			meshData.vertices.resize(element.Vertices.size());

			// Convert vertices to your own format
			//std::vector<Vertex> mdlVertices;
			//mdlVertices.resize(element.Vertices.size());

			for (size_t v = 0; v < element.Vertices.size(); v++)
			{
				// The most important part, the position! Force w = 1: some exporters
				// (e.g. the glTF->FBX New Sponza) leave Position[3] at 0, which makes
				// the VS treat the vertex as a direction and collapses the mesh.
				meshData.vertices[v].position = {
					element.Vertices[v].Position[0],
					element.Vertices[v].Position[1],
					element.Vertices[v].Position[2],
					1.0f
				};

				// All four vertex color channels I have.
				for (int vCol = 0; vCol < 4; vCol++)
				{
					meshData.vertices[v].vertexColors[vCol] = {
						element.Vertices[v].VertexColors[vCol][0],
						element.Vertices[v].VertexColors[vCol][1],
						element.Vertices[v].VertexColors[vCol][2],
						element.Vertices[v].VertexColors[vCol][3]
					};
					}

				meshData.vertices[v].normal = Vector3f(element.Vertices[v].Normal[0], element.Vertices[v].Normal[1], element.Vertices[v].Normal[2]);
				meshData.vertices[v].binormal = Vector3f(element.Vertices[v].BiNormal[0], element.Vertices[v].BiNormal[1], element.Vertices[v].BiNormal[2]);
				meshData.vertices[v].tangent = Vector3f(element.Vertices[v].Tangent[0], element.Vertices[v].Tangent[1], element.Vertices[v].Tangent[2]);

				for (unsigned int UVch = 0; UVch < 4; UVch++)
				{
					meshData.vertices[v].uvs[UVch] = {
						 element.Vertices[v].UVs[UVch][0],
						 element.Vertices[v].UVs[UVch][1]
					};
				}

				meshData.vertices[v].bones = {
					static_cast<float>(element.Vertices[v].BoneIDs[0]),
					static_cast<float>(element.Vertices[v].BoneIDs[1]),
					static_cast<float>(element.Vertices[v].BoneIDs[2]),
					static_cast<float>(element.Vertices[v].BoneIDs[3])
				};

				meshData.vertices[v].weights = {
					element.Vertices[v].BoneWeights[0],
					element.Vertices[v].BoneWeights[1],
					element.Vertices[v].BoneWeights[2],
					element.Vertices[v].BoneWeights[3]
				};
			}

			//std::vector<unsigned int>& mdlIndices = element.Indices;
			meshData.indices.resize(element.Indices.size());
			memcpy(meshData.indices.data(), element.Indices.data(), sizeof(unsigned int) * element.Indices.size());
			//meshData.Indices = element.Indices;

			rhi::BufferDesc vertexBufferDesc{};
			vertexBufferDesc.byteSize = static_cast<UINT>(meshData.vertices.size()) * static_cast<UINT>(sizeof(Vertex));
			vertexBufferDesc.stride = sizeof(Vertex);
			vertexBufferDesc.usage = rhi::BufferUsage::Vertex;
			vertexBufferDesc.memory = rhi::MemoryType::Default;
			vertexBufferDesc.debugName = "Mesh_VB";

			rhi::BufferHandle vertexBuffer = DX11::Rhi()->CreateBuffer(vertexBufferDesc, &meshData.vertices[0]);
			if (!vertexBuffer.IsValid())
			{
				return nullptr;
			}

			rhi::BufferDesc indexBufferDesc{};
			indexBufferDesc.byteSize = static_cast<UINT>(meshData.indices.size()) * static_cast<UINT>(sizeof(float)); // TODO: What :P Sizeof should be uint.
			indexBufferDesc.stride = sizeof(unsigned int);
			indexBufferDesc.usage = rhi::BufferUsage::Index;
			indexBufferDesc.memory = rhi::MemoryType::Default;
			indexBufferDesc.debugName = "Mesh_IB";

			rhi::BufferHandle indexBuffer = DX11::Rhi()->CreateBuffer(indexBufferDesc, &meshData.indices[0]);
			if (!indexBuffer.IsValid())
			{
				return nullptr;
			}

			meshData.numberOfVertices = static_cast<UINT>(meshData.vertices.size());
			meshData.numberOfIndices = static_cast<UINT>(meshData.indices.size());
			meshData.stride = sizeof(Vertex);
			meshData.offset = 0;
			meshData.vertexBuffer = vertexBuffer;
			meshData.indexBuffer = indexBuffer;
			meshData.name = StringRegistry::RegisterOrGetString(element.MeshName);
			if (tgaModel.Materials.size() > element.MaterialIndex)
			{
				meshData.materialName = StringRegistry::RegisterOrGetString(tgaModel.Materials[element.MaterialIndex].MaterialName);
			}
			else
			{
				meshData.materialName = ""_tgaid;
			}
			meshData.bounds = CalculateBoxSphereBounds(meshData.vertices);
		}

		std::shared_ptr<Model> model = std::make_shared<Model>();

		// Static meshes: collapse same-material sub-meshes before Init so the cache
		// stores the merged form. Skinned meshes keep their sub-mesh split.
		if (mdlSkeleton.joints.empty())
			MergeMeshesByMaterial(mdlMeshData);

		model->Init(mdlMeshData, std::string(resolved_path.GetStringView()));
		if (mdlSkeleton.joints.size() > 0)
		{
			model->mySkeleton = std::make_shared<Skeleton>(std::move(mdlSkeleton));
		}
		else
		{
			// Static mesh: cache the converted data so the next load skips the FBX SDK.
			WriteMeshCache(cachePath, resolved_path.GetData(), mdlMeshData);
		}
		AssignDefaultMaterials(someFilePath, model.get());
		myLoadedModels.insert(std::pair<StringId, std::shared_ptr<Model>>(someFilePath, model));

		return model;
	}

	return nullptr;
}

std::shared_ptr<const Animation> ModelFactory::GetAnimation(StringId someFilePath, const std::shared_ptr<const Skeleton>& aSkeleton)
{
	if (someFilePath.IsEmpty())
		return nullptr;
	FilePathStream resolvedPath;
	if (!Tga::Settings::ResolveAssetPath(someFilePath, resolvedPath))
		return nullptr;
	StringId resolvedPathId = StringRegistry::RegisterOrGetString(resolvedPath.GetStringView());

	// The FBX SDK doesn't like widechar :(.
	TGA::FBX::Animation fbxAnimation;

	auto it = myLoadedAnimations.find(AnimationIdentifer{ resolvedPathId , aSkeleton });
	if (it != myLoadedAnimations.end())
		return it->second;

	if (TGA::FBX::Importer::LoadAnimationA(resolvedPath.GetData(), fbxAnimation))
	{
		std::shared_ptr<Animation> animation = std::make_shared<Animation>();
		animation->name = fbxAnimation.Name;
		animation->length = fbxAnimation.Length;
		animation->framesPerSecond = fbxAnimation.FramesPerSecond;
		animation->frames.resize(fbxAnimation.Frames.size());
		animation->duration = static_cast<float>(fbxAnimation.Duration);

		const Tga::Skeleton& skeleton = *aSkeleton;
		
		for (size_t f = 0; f < animation->frames.size(); f++)
		{
			animation->frames[f].count = skeleton.joints.size();
			for (const auto& [boneName, boneTransform] : fbxAnimation.Frames[f].LocalTransforms)
			{
				Matrix4x4f localMatrix;
				memcpy_s(&localMatrix, sizeof(Matrix4x4f), boneTransform.Data, sizeof(float) * 16);

				auto jointIt = skeleton.jointNameToIndex.find(boneName);

				if (jointIt != skeleton.jointNameToIndex.end())
					animation->frames[f].jointTransforms[jointIt->second] = ScaleRotationTranslationf::CreateFromMatrix(localMatrix);
			}
		}

		myLoadedAnimations[AnimationIdentifer{ resolvedPathId , aSkeleton }] = animation;

		return animation;
	}

	return nullptr;
}
#endif

Tga::BoxSphereBounds Tga::ModelFactory::CalculateBoxSphereBounds(std::vector<Tga::Vertex> somePositions)
{
	Vector3f minExtents = FLT_MAX;
	Vector3f maxExtents = -FLT_MAX;

	for (unsigned int v = 0; v < somePositions.size(); v++)
	{
		if (somePositions[v].position.x > maxExtents.x)
			maxExtents.x = somePositions[v].position.x;
		if (somePositions[v].position.y > maxExtents.y)
			maxExtents.y = somePositions[v].position.y;
		if (somePositions[v].position.z > maxExtents.z)
			maxExtents.z = somePositions[v].position.z;

		if (somePositions[v].position.x < minExtents.x)
			minExtents.x = somePositions[v].position.x;
		if (somePositions[v].position.y < minExtents.y)
			minExtents.y = somePositions[v].position.y;
		if (somePositions[v].position.z < minExtents.z)
			minExtents.z = somePositions[v].position.z;
	}

	const Vector3f extentsCenter = 0.5f * (minExtents + maxExtents);
	const Vector3f boxExtents = 0.5f * (maxExtents - minExtents);
	const float radius = boxExtents.Length();
	return { radius, boxExtents, extentsCenter };
}


AnimationPlayer ModelFactory::GetAnimationPlayer(StringId someFilePath, const std::shared_ptr<const Skeleton>& aSkeleton)
{
	AnimationPlayer instance;
	std::shared_ptr<const Animation> animation = GetAnimation(someFilePath, aSkeleton);
	if (animation)
		instance.Init(animation);

	return instance;
}

std::shared_ptr<Model> ModelFactory::GetModel(StringId someFilePath)
{
	auto it = myLoadedModels.find(someFilePath);
	if (it != myLoadedModels.end())
		return it->second;

	return LoadModel(someFilePath);
}

void ModelFactory::OnModelChanged(StringId aUnresolvedPath)
{
	myLoadedModels.erase(aUnresolvedPath);
}
std::shared_ptr<Model> ModelFactory::GetModel(std::string_view aFilePath) { return GetModel(StringRegistry::RegisterOrGetString(aFilePath)); }

AnimatedModelInstance ModelFactory::GetAnimatedModelInstance(std::string_view aFilePath) { return GetAnimatedModelInstance(StringRegistry::RegisterOrGetString(aFilePath)); }

ModelInstance ModelFactory::GetModelInstance(std::string_view aFilePath) { return GetModelInstance(StringRegistry::RegisterOrGetString(aFilePath)); }

std::shared_ptr<const Animation> ModelFactory::GetAnimation(std::string_view aFilePath, const std::shared_ptr<const Skeleton>& aSkeleton) { return GetAnimation(StringRegistry::RegisterOrGetString(aFilePath), aSkeleton); }

AnimationPlayer ModelFactory::GetAnimationPlayer(std::string_view aFilePath, const std::shared_ptr<const Skeleton>& aSkeleton) { return GetAnimationPlayer(StringRegistry::RegisterOrGetString(aFilePath), aSkeleton); }
