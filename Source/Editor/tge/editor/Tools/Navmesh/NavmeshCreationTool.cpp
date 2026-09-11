#include <tge/editor/Tools/Navmesh/NavmeshCreationTool.h>

#include <imgui.h>
#include <tge/editor/Editor.h>

#include <tge/scene/Scene.h>
#include <tge/editor/Scene/SceneSelection.h>
#include <tge/editor/Scene/ActiveScene.h>

#include <tge/graphics/DX11.h>
#include <tge/log/Log.h>

#include <fstream>
#include <algorithm>

#include <filesystem>
namespace fs = std::filesystem;

using namespace Tga;

static rcConfig cfg;
static rcPolyMesh* pmesh;
static rcPolyMeshDetail* dmesh;

struct Lines {
	std::vector<Tga::Color> colors;
	std::vector<Tga::Vector3f> from;
	std::vector<Tga::Vector3f> to;
	unsigned int numlines = 0;
};
static Lines lines;

NavmeshCreationTool::NavmeshCreationTool()
{
	pmesh = rcAllocPolyMesh();
	dmesh = rcAllocPolyMeshDetail();

	if (!pmesh) {
		ERROR_PRINT("Error calling: rcPolyMesh* pmesh = rcAllocPolyMesh()");
		// Handle error
	}
	if (!dmesh) {
		ERROR_PRINT("Error calling: rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail()");
		// Handle error
	}
}

NavmeshCreationTool::~NavmeshCreationTool()
{
	rcFreePolyMesh(pmesh);
	rcFreePolyMeshDetail(dmesh);
}

/*
static void SaveNavmesh(const rcPolyMesh* pmesh, const rcPolyMeshDetail* dmesh, const char* filename)
{
	std::ofstream file(filename, std::ios::binary);

	if (!file.is_open())
	{
		ERROR_PRINT("Can't open file to write navmesh");
		return;
	}

	file.write(reinterpret_cast<const char*>(&pmesh->nverts), sizeof(pmesh->nverts));
	file.write(reinterpret_cast<const char*>(pmesh->verts), pmesh->nverts * 3 * sizeof(unsigned short));

	file.write(reinterpret_cast<const char*>(&pmesh->npolys), sizeof(pmesh->npolys));
	file.write(reinterpret_cast<const char*>(pmesh->polys), pmesh->npolys * pmesh->nvp * 2 * sizeof(unsigned short));
	file.write(reinterpret_cast<const char*>(pmesh->flags), pmesh->npolys * sizeof(unsigned short));
	file.write(reinterpret_cast<const char*>(pmesh->areas), pmesh->npolys * sizeof(unsigned char));

	// Write PolyMeshDetail data
	file.write(reinterpret_cast<const char*>(&dmesh->nmeshes), sizeof(dmesh->nmeshes));
	file.write(reinterpret_cast<const char*>(dmesh->meshes), dmesh->nmeshes * 4 * sizeof(unsigned int));

	file.write(reinterpret_cast<const char*>(&dmesh->nverts), sizeof(dmesh->nverts));
	file.write(reinterpret_cast<const char*>(dmesh->verts), dmesh->nverts * 3 * sizeof(float));

	file.write(reinterpret_cast<const char*>(&dmesh->ntris), sizeof(dmesh->ntris));
	file.write(reinterpret_cast<const char*>(dmesh->tris), dmesh->ntris * 4 * sizeof(unsigned char));

	file.close();
}
*/

static void ExportPolymeshToOBJ(const std::string& filename)
{
	const rcPolyMesh& mesh = *pmesh;

	std::ofstream file(filename + ".obj");
	if (!file.is_open()) {
		ERROR_PRINT("Failed to open file for writing: %s", filename.c_str());
		return;
	}

	// Export vertices
	for (int i = 0; i < mesh.nverts; ++i) {
		const unsigned short* v = &mesh.verts[i * 3];
		//file << "v " << v[0] * mesh.cs << " " << v[1] * mesh.ch << " " << v[2] * mesh.cs << "\n";
		file << "v "
			<< v[0] * mesh.cs + mesh.bmin[0] << " "  // Apply bmin for x
			<< v[1] * mesh.ch + mesh.bmin[1] << " "  // Apply bmin for y
			<< v[2] * mesh.cs + mesh.bmin[2] << "\n"; // Apply bmin for z
	}

	// Export polygons
	for (int i = 0; i < mesh.npolys; ++i) {
		file << "f ";
		for (int j = 0; j < mesh.nvp; ++j) {
			if (mesh.polys[i * 2 * mesh.nvp + j] == RC_MESH_NULL_IDX)
				break;
			file << (mesh.polys[i * 2 * mesh.nvp + j] + 1) << " ";
		}
		file << "\n";
	}

	file.close();
}

static void ExportDetailmeshToOBJ(const std::string& filename)
{
	const rcPolyMeshDetail& mesh = *dmesh;

	std::ofstream file(filename + "_detail.obj");

	if (!file.is_open())
	{
		// error
		return;
	}

	for (size_t i = 0; i < mesh.nverts; ++i)
	{
		const float* v = &mesh.verts[i * 3];
		file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
	}

	for (size_t i = 0; i < mesh.nmeshes; ++i)
	{
		const unsigned int* m = &mesh.meshes[i * 4];
		const int vbase = m[0];
		const int tbase = m[2];
		const int ntris = m[3];

		for (size_t j = 0; j < ntris; ++j)
		{
			const unsigned char* t = &mesh.tris[(tbase + j) * 4];
			const int v0 = vbase + t[0] + 1;
			const int v1 = vbase + t[1] + 1;
			const int v2 = vbase + t[2] + 1;

			file << "f " << v0 << " " << v1 << " " << v2 << "\n";
		}
	}
	file.close();
}

void NavmeshCreationTool::Init()
{
	memset(&cfg, 0, sizeof(cfg));
	cfg.cs = 0.3f; // m_cellSize
	cfg.ch = 0.2f; // cell height
	cfg.walkableSlopeAngle = 43.f;
	cfg.walkableHeight = (int)ceilf(2.f / cfg.ch);
	cfg.walkableRadius = (int)ceilf(0.6f / cfg.cs);
	cfg.walkableClimb = (int)floorf(0.9f / cfg.ch);
	cfg.maxVertsPerPoly = 6;

	cfg.maxEdgeLen = (int)(12.f / cfg.cs);
	cfg.maxSimplificationError = 1.3f;

	cfg.minRegionArea = (int)rcSqr(8);
	cfg.mergeRegionArea = (int)rcSqr(20);

	cfg.tileSize = 64;

	cfg.detailSampleDist = cfg.cs * 6.0f;
	cfg.detailSampleMaxError = cfg.ch * 1.0f;
}

void NavmeshCreationTool::DrawUI()
{
	ImGui::SetNextWindowClass(Editor::GetEditor()->GetGlobalWindowClass());
	{
		bool anyActivated = false;
		{
			ImGui::DragFloat("Cell size", &cfg.cs);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The xz-plane cell size to use for fields. [Limit: > 0] [Units: wu]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		{
			ImGui::DragFloat("Cell height", &cfg.ch);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The y-axis cell size to use for fields. [Limit: > 0] [Units: wu]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		{
			ImGui::SliderFloat("Walkable slope angle", &cfg.walkableSlopeAngle, 0.f, 90.0f);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The maximum slope that is considered walkable. [Limits: 0 <= value < 90] [Units: Degrees]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		{
			ImGui::DragInt("Walkable Height", &cfg.walkableHeight);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Minimum floor to 'ceiling' height that will still allow the floor area to be considered walkable. [Limit: >= 3] [Units: vx]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		{
			ImGui::DragInt("Walkable Climb", &cfg.walkableClimb);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Maximum ledge height that is considered to still be traversable. [Limit: >=0] [Units: vx]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		{
			ImGui::DragInt("Walkable Radius", &cfg.walkableRadius);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The distance to erode/shrink the walkable area of the heightfield away from obstructions.  [Limit: >=0] [Units: vx]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		{
			ImGui::DragInt("Max Edge Lenght", &cfg.maxEdgeLen);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The maximum allowed length for contour edges along the border of the mesh. [Limit: >=0] [Units: vx]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		ImGui::Separator();

		{
			ImGui::SliderFloat("Max Simplification Error", &cfg.maxSimplificationError, 0.f, 10.0f);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The maximum distance a simplified contour's border edges should deviate the original raw contour. [Limit: >=0] [Units: vx]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		{
			ImGui::DragInt("Min Region Area", &cfg.minRegionArea);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The minimum number of cells allowed to form isolated island areas. [Limit: >=0] [Units: vx]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		{
			ImGui::DragInt("Merge Region Area", &cfg.mergeRegionArea);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Any regions with a span count smaller than this value will, if possible, be merged with larger regions. [Limit: >=0] [Units: vx]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		{
			ImGui::DragInt("Max Verts per Polygon", &cfg.maxVertsPerPoly);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The maximum number of vertices allowed for polygons generated during the contour to polygon conversion process. [Limit: >= 3]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		{
			ImGui::DragFloat("Detail sample distance", &cfg.detailSampleDist);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Sets the sampling distance to use when generating the detail mesh. (For height detail only.) [Limits: 0 or >= 0.9] [Units: wu]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		{
			ImGui::DragFloat("detail sample max error", &cfg.detailSampleMaxError);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The maximum distance the detail mesh surface should deviate from heightfield data. (For height detail only.) [Limit: >=0] [Units: wu]");
			}
			if (ImGui::IsItemActivated())
			{
				anyActivated = true;
			}
		}

		if (anyActivated)
		{

		}
		if (ImGui::Button("Generate"))
		{
			BuildNavmesh();
		}
	}
//	ImGui::End();
	
}

static void SetupNavmeshLines()
{
	lines.colors.clear();
	lines.from.clear();
	lines.to.clear();

	for (size_t i = 0; i < dmesh->nmeshes; ++i)
	{
		const unsigned int* m = &dmesh->meshes[i * 4];
		const int ntris = (int)m[3];
		lines.numlines += ntris * 3;
	}
	lines.colors.reserve(lines.numlines);
	lines.from.reserve(lines.numlines);
	lines.to.reserve(lines.numlines);

	Color color(0.7f, 0.7f, 0.7f, 1.0f);
	float scale = 10.f;

	for (size_t i = 0; i < dmesh->nmeshes; ++i)
	{
		const unsigned int* mesh = &dmesh->meshes[i * 4];
		const int vbase = (int)mesh[0];
		const int tbase = (int)mesh[2];
		const int ntris = (int)mesh[3];

		for (size_t j = 0; j < ntris; ++j)
		{
			const unsigned char* t = &dmesh->tris[(tbase + j) * 4];

			int v0 = vbase + t[0];
			int v1 = vbase + t[1];
			int v2 = vbase + t[2];

			const float* v0pos = &dmesh->verts[v0 * 3];
			const float* v1pos = &dmesh->verts[v1 * 3];
			const float* v2pos = &dmesh->verts[v2 * 3];

			Vector3f p0 = { v0pos[0], v0pos[1], v0pos[2] };
			Vector3f p1 = { v1pos[0], v1pos[1], v1pos[2] };
			Vector3f p2 = { v2pos[0], v2pos[1], v2pos[2] };

			lines.colors.push_back(color);
			lines.from.push_back(p0 * scale);
			lines.to.push_back(p1 * scale);

			lines.colors.push_back(color);
			lines.from.push_back(p1 * scale);
			lines.to.push_back(p2 * scale);

			lines.colors.push_back(color);
			lines.from.push_back(p2 * scale);
			lines.to.push_back(p0 * scale);
		}
	}
}

void NavmeshCreationTool::DrawNavmesh() const
{
#ifdef DISABLED_DURING_MODULARIZATION
	if (lines.numlines <= 0)
	{
		return;
	}

	Tga::Application& engine = *Tga::Application::GetInstance();
	Tga::LineDrawer& drawer = GraphicsEngine::GetInstance()->GetLineDrawer();

	int batches = 1;
	while (lines.numlines/batches > 1000)
	{
		batches++;
	}

	for (size_t i = 0; i < batches; ++i)
	{
		int count = std::clamp(1000, 0, (int)lines.numlines);

		std::span<Color> colors(lines.colors.data() + i, count);
		std::span<Vector3f> from(lines.from.data() + i, count);
		std::span<Vector3f> to(lines.to.data() + i, count);

		Tga::LineMultiPrimitive primitives { 
			.colors = colors.data(), 
			.fromPositions = from.data(), 
			.toPositions = to.data(),
			.count = (unsigned int)to.size()
		};
		drawer.Draw(primitives);
	}
#endif
}

void NavmeshCreationTool::BuildNavmesh()
{
	std::vector<float> vertices;
	std::vector<int> indices;

	float bmin[3] = { 0 }, bmax[3] = { 0 };
	uint32_t vertexOffset = 0;

	auto &scene = *GetActiveScene();

	for (const auto &object : scene.GetSceneObjects())
	{
		// TGE uses 1 unit = 1 cm, while recast uses 1 unit = 1 meter. So we scale our transform by 0.1f so that recast can work on the correct sized mesh
		const Matrix4x4f& transform = object.second->GetTransform() * Matrix4x4f::CreateFromScale({ 0.1f });

		// Todo: reenable with new way of setting up meshes
		vertexOffset; transform; object;
		/*
		if (object.second->UsedInNavmesh)
		{
			for (const Model::MeshData& mesh : object.second->GetModelInstance().GetModel()->GetMeshDataList())
			{
				for (uint32_t i = 0; i < mesh.NumberOfVertices; ++i)
				{
					//const Vector4f pos = mesh.Vertices[i].Position;
					const Vector4f pos = mesh.Vertices[i].Position * transform;
					vertices.push_back(pos.x);
					vertices.push_back(pos.y);
					vertices.push_back(pos.z);

					//Vector3f min_bounds = mesh.Bounds.Center  - mesh.Bounds.BoxExtents ;
					if (pos.x < bmin[0]) bmin[0] = pos.x;
					if (pos.y < bmin[1]) bmin[1] = pos.y;
					if (pos.z < bmin[2]) bmin[2] = pos.z;

					//Vector3f max_bounds = mesh.Bounds.Center  + mesh.Bounds.BoxExtents ;
					if (pos.x > bmax[0]) bmax[0] = pos.x;
					if (pos.y > bmax[1]) bmax[1] = pos.y;
					if (pos.z > bmax[2]) bmax[2] = pos.z;
				}


				// @todo: probably there is no need to copy these
				for (uint32_t i = 0; i < mesh.NumberOfIndices; ++i)
				{
					indices.push_back(mesh.Indices[i] + vertexOffset);
				}

				vertexOffset = (uint32_t)vertices.size()/3;
			}
		}*/
	}

	if (vertices.size() <= 0 || indices.size() <= 0)
	{
		return;
	}

	rcContext *ctx = new rcContext();

	int width = (int)((bmax[0] - bmin[0]) / cfg.cs);
	int height = (int)((bmax[2] - bmin[2]) / cfg.cs);

	rcVcopy(cfg.bmin, bmin);
	rcVcopy(cfg.bmax, bmax);

	rcHeightfield* solid = rcAllocHeightfield();
	if (!solid) {
		ERROR_PRINT("error from calling: rcHeightfield* solid = rcAllocHeightfield()");
		// Handle the error (e.g., return or throw an exception)
	}

	if (!rcCreateHeightfield(ctx, *solid, width, height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
		ERROR_PRINT("error calling: rcCreateHeightfield(...)");
		// Handle the error
	}

	unsigned char* triareas = new unsigned char[indices.size() / 3];
	rcMarkWalkableTriangles(ctx, cfg.walkableSlopeAngle, &vertices[0], (int)vertices.size() / 3, &indices[0], (int)indices.size() / 3, triareas);

	//bool result = rcRasterizeTriangles(
	rcRasterizeTriangles(
		ctx,                     // The Recast context
		&vertices[0],            // Pointer to the first vertex in the vertices array
		static_cast<int>(vertices.size() / 3), // Number of vertices (each vertex has 3 floats)
		&indices[0],             // Pointer to the first index in the indices array
		triareas,                // Pointer to the first element in the triangle areas array
		static_cast<int>(indices.size() / 3),  // Number of triangles (each triangle has 3 indices)
		*solid,                  // The heightfield to rasterize into
		cfg.walkableClimb        // The walkable climb height
	);

	rcFilterLowHangingWalkableObstacles(ctx, cfg.walkableClimb, *solid);
	rcFilterLedgeSpans(ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
	rcFilterWalkableLowHeightSpans(ctx, cfg.walkableHeight, *solid);

	rcCompactHeightfield* chf = rcAllocCompactHeightfield();
	if (!chf) {
		ERROR_PRINT("Error calling: rcCompactHeightfield* chf = rcAllocCompactHeightfield()");
		// Handle error
	}
	if (!rcBuildCompactHeightfield(ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf)) {
		ERROR_PRINT("Error calling: rcBuildCompactHeightfield");
		// Handle error
	}

	if (!rcErodeWalkableArea(ctx, cfg.walkableRadius, *chf)) {
		ERROR_PRINT("Error calling: rcErodeWalkableArea");
		// Handle error
	}

	if (!rcBuildDistanceField(ctx, *chf)) {
		ERROR_PRINT("Error calling: rcBuildDistanceField");
		// Handle error
	}

	if (!rcBuildRegions(ctx, *chf, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea)) {
		ERROR_PRINT("Error calling: rcBuildRegions");
		// Handle error
	}

	rcContourSet* cset = rcAllocContourSet();
	if (!cset) {
		ERROR_PRINT("Error calling: rcContourSet* cset = rcAllocContourSet()");
		// Handle error
	}
	if (!rcBuildContours(ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset)) {
		ERROR_PRINT("Error calling: rcBuildContours");
		// Handle error
	}

	if (!rcBuildPolyMesh(ctx, *cset, cfg.maxVertsPerPoly, *pmesh)) {
		ERROR_PRINT("Error calling: rcBuildPolyMesh");
		// Handle error
	}

	if (!rcBuildPolyMeshDetail(ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh)) {
		ERROR_PRINT("Error calling: rcBuildPolyMeshDetail");
		// Handle error
	}

	INFO_PRINT("Done preparing and generating navmesh, saving in obj-file.");

	fs::path path = GetActiveScene()->GetPath();
	std::string name = (path.parent_path() / "navmesh").string();

	ExportPolymeshToOBJ(name);
	ExportDetailmeshToOBJ(name);

	SetupNavmeshLines();

	rcFreeHeightField(solid);
	rcFreeCompactHeightfield(chf);
	rcFreeContourSet(cset);
//	rcFreePolyMesh(pmesh);
//	rcFreePolyMeshDetail(dmesh);
	delete[] triareas;
	delete ctx;
}