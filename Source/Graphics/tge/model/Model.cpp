#include "stdafx.h"
#include <tge/model/Model.h>
#include <tge/log/Log.h>

#include <algorithm>
#include <cmath>

using namespace Tga;

namespace
{
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
	myPath = aPath;
	ComputeUnionBounds(myMeshData, myBounds);
}
