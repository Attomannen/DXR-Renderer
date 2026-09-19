#pragma once

#include <vector>
#include <cstdint>

namespace Ag
{
	struct Vertex;

	// Quadric-error-metric edge-collapse decimation.
	//
	// The result is an INDEX buffer over the original, untouched vertex buffer:
	// a level never adds or moves a vertex, it only stops referencing some of
	// them. That is what makes these levels cheap to keep around -- every level
	// shares one vertex buffer and one set of SRVs, so a ray-tracing BLAS or a
	// draw can switch level by swapping an index range and nothing else.
	//
	// Collapses are constrained so a level stays usable as a silhouette:
	//   - open boundary vertices are locked, so holes never open up,
	//   - a collapse that would flip a triangle's normal is rejected,
	//   - vertices that share a position but not their attributes (UV seams,
	//     split normals) remap to the nearest surviving attribute, rather than
	//     to an arbitrary one, which keeps texturing stable across the seam.
	struct MeshSimplifyResult
	{
		std::vector<uint32_t> indices;
		// Quadric error of the last accepted collapse, in squared world units.
		// Useful as the "this level is good until N pixels" figure later on.
		float error = 0.0f;
		uint32_t triangles = 0;
	};

	// Vertices are addressed by stride so this works equally on the importer's
	// Vertex array and on the mesh cache's packed float blocks, without either
	// side having to unpack into the other's layout first.
	struct MeshSimplifySource
	{
		const uint8_t* positions = nullptr;   // float3
		uint32_t positionStride = 0;
		const uint8_t* uv0 = nullptr;         // float2, optional
		uint32_t uvStride = 0;
		uint32_t vertexCount = 0;
	};

	// aTargetRatio is a fraction of the source triangle count (0..1). The result
	// can be larger than requested when the constraints above block collapses --
	// a mesh that is mostly boundary (a plane, a decal, a leaf card) barely
	// simplifies at all, and that is the correct outcome.
	MeshSimplifyResult SimplifyMesh(const MeshSimplifySource& aSource,
		const uint32_t* someIndices, size_t anIndexCount, float aTargetRatio);

	MeshSimplifyResult SimplifyMesh(const std::vector<Vertex>& someVertices,
		const std::vector<unsigned int>& someIndices, float aTargetRatio);
}
