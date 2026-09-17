#include "stdafx.h"
#include "tge/model/MeshSimplify.h"
#include <tge/graphics/Vertex.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace Tga
{
	namespace
	{
		// Symmetric 4x4 quadric, stored as the 10 unique coefficients.
		struct Quadric
		{
			double a00 = 0, a01 = 0, a02 = 0, a03 = 0;
			double a11 = 0, a12 = 0, a13 = 0;
			double a22 = 0, a23 = 0;
			double a33 = 0;

			void AddPlane(double x, double y, double z, double w)
			{
				a00 += x * x; a01 += x * y; a02 += x * z; a03 += x * w;
				a11 += y * y; a12 += y * z; a13 += y * w;
				a22 += z * z; a23 += z * w;
				a33 += w * w;
			}
			void Add(const Quadric& o)
			{
				a00 += o.a00; a01 += o.a01; a02 += o.a02; a03 += o.a03;
				a11 += o.a11; a12 += o.a12; a13 += o.a13;
				a22 += o.a22; a23 += o.a23; a33 += o.a33;
			}
			// v^T Q v for a point (x,y,z,1): the squared distance to the set of
			// planes this quadric accumulated.
			double Evaluate(double x, double y, double z) const
			{
				return a00 * x * x + 2 * a01 * x * y + 2 * a02 * x * z + 2 * a03 * x
					+ a11 * y * y + 2 * a12 * y * z + 2 * a13 * y
					+ a22 * z * z + 2 * a23 * z
					+ a33;
			}
		};

		struct Vec3d { double x = 0, y = 0, z = 0; };

		inline const float* PosAt(const MeshSimplifySource& src, size_t i)
		{ return reinterpret_cast<const float*>(src.positions + (size_t)src.positionStride * i); }
		inline const float* UvAt(const MeshSimplifySource& src, size_t i)
		{ return src.uv0 ? reinterpret_cast<const float*>(src.uv0 + (size_t)src.uvStride * i) : nullptr; }

		uint64_t EdgeKey(uint32_t a, uint32_t b)
		{
			return a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
		}

		// Positions are welded so that a UV seam or a split normal -- several
		// vertices at one point in space -- collapses as a single piece of
		// surface. Simplifying the raw vertex list instead would tear the mesh
		// apart along every seam.
		struct PositionWeld
		{
			std::vector<uint32_t> vertexToPos;  // vertex index -> position id
			std::vector<Vec3d> positions;       // position id -> position
			std::vector<std::vector<uint32_t>> posToVertices;
		};

		PositionWeld WeldPositions(const MeshSimplifySource& src)
		{
			PositionWeld w;
			w.vertexToPos.resize(src.vertexCount, 0);
			// Quantising to a fixed grid keeps exported duplicates that differ in
			// the last bits together; the grid is far finer than any geometry we
			// care about (0.1 mm at engine scale).
			struct KeyHash { size_t operator()(const std::array<int64_t, 3>& k) const {
				size_t h = 1469598103934665603ull;
				for (int64_t v : k) { h ^= (size_t)v; h *= 1099511628211ull; }
				return h; } };
			std::unordered_map<std::array<int64_t, 3>, uint32_t, KeyHash> map;
			map.reserve(src.vertexCount);
			for (size_t i = 0; i < src.vertexCount; ++i)
			{
				const float* p = PosAt(src, i);
				const std::array<int64_t, 3> key{
					(int64_t)std::llround((double)p[0] * 10000.0),
					(int64_t)std::llround((double)p[1] * 10000.0),
					(int64_t)std::llround((double)p[2] * 10000.0) };
				auto it = map.find(key);
				if (it == map.end())
				{
					const uint32_t id = (uint32_t)w.positions.size();
					map.emplace(key, id);
					w.positions.push_back({ (double)p[0], (double)p[1], (double)p[2] });
					w.posToVertices.emplace_back();
					w.vertexToPos[i] = id;
				}
				else w.vertexToPos[i] = it->second;
			}
			for (size_t i = 0; i < src.vertexCount; ++i)
				w.posToVertices[w.vertexToPos[i]].push_back((uint32_t)i);
			return w;
		}
	}

	MeshSimplifyResult SimplifyMesh(const MeshSimplifySource& src,
		const uint32_t* indexPtr, size_t indexCount, float targetRatio)
	{
		MeshSimplifyResult out;
		const size_t triCount = indexCount / 3;
		out.indices.assign(indexPtr, indexPtr + indexCount);
		out.triangles = (uint32_t)triCount;
		if (triCount < 2 || src.vertexCount == 0 || !src.positions || targetRatio >= 1.0f) return out;

		const size_t targetTris = std::max<size_t>(1, (size_t)(triCount * std::max(0.0f, targetRatio)));

		const PositionWeld weld = WeldPositions(src);
		const size_t posCount = weld.positions.size();

		// ---- quadrics from triangle planes -----------------------------------
		std::vector<Quadric> quadrics(posCount);
		std::vector<uint32_t> tri(indexPtr, indexPtr + indexCount);
		auto triPos = [&](size_t t, int c) { return weld.vertexToPos[tri[t * 3 + c]]; };

		for (size_t t = 0; t < triCount; ++t)
		{
			const Vec3d& p0 = weld.positions[triPos(t, 0)];
			const Vec3d& p1 = weld.positions[triPos(t, 1)];
			const Vec3d& p2 = weld.positions[triPos(t, 2)];
			const double ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
			const double vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
			double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
			const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
			if (len < 1e-12) continue;          // degenerate triangle contributes nothing
			nx /= len; ny /= len; nz /= len;
			const double d = -(nx * p0.x + ny * p0.y + nz * p0.z);
			// Weighting by area makes large flat surfaces resist collapse less
			// than small detailed ones, which is the behaviour we want.
			const double area = len * 0.5;
			Quadric q; q.AddPlane(nx, ny, nz, d);
			q.a00 *= area; q.a01 *= area; q.a02 *= area; q.a03 *= area;
			q.a11 *= area; q.a12 *= area; q.a13 *= area;
			q.a22 *= area; q.a23 *= area; q.a33 *= area;
			for (int c = 0; c < 3; ++c) quadrics[triPos(t, c)].Add(q);
		}

		// ---- edges, and which are on an open boundary -------------------------
		// An edge used by exactly one triangle is a border. Collapsing across one
		// erodes the outline of the mesh and opens holes in anything built from
		// flat cards (foliage, decals, signs), so those vertices are locked.
		std::unordered_map<uint64_t, int> edgeUse;
		edgeUse.reserve(triCount * 3);
		for (size_t t = 0; t < triCount; ++t)
			for (int c = 0; c < 3; ++c)
			{
				const uint32_t a = triPos(t, c), b = triPos(t, (c + 1) % 3);
				if (a != b) ++edgeUse[EdgeKey(a, b)];
			}
		std::vector<bool> locked(posCount, false);
		for (const auto& [key, uses] : edgeUse)
			if (uses == 1)
			{
				locked[(uint32_t)(key >> 32)] = true;
				locked[(uint32_t)(key & 0xffffffffu)] = true;
			}

		// ---- adjacency --------------------------------------------------------
		std::vector<std::vector<uint32_t>> posToTris(posCount);
		for (size_t t = 0; t < triCount; ++t)
			for (int c = 0; c < 3; ++c) posToTris[triPos(t, c)].push_back((uint32_t)t);

		struct Collapse
		{
			double cost;
			uint32_t from, to;   // 'from' is removed, 'to' survives
			uint32_t version;
			bool operator<(const Collapse& o) const { return cost > o.cost; }  // min-heap
		};
		std::vector<uint32_t> version(posCount, 0);
		std::priority_queue<Collapse> queue;

		auto cost = [&](uint32_t from, uint32_t to) {
			Quadric q = quadrics[from]; q.Add(quadrics[to]);
			const Vec3d& p = weld.positions[to];
			return std::max(0.0, q.Evaluate(p.x, p.y, p.z));
		};
		auto push = [&](uint32_t a, uint32_t b) {
			if (!locked[a]) queue.push({ cost(a, b), a, b, version[a] });
			if (!locked[b]) queue.push({ cost(b, a), b, a, version[b] });
		};
		for (const auto& [key, uses] : edgeUse)
			push((uint32_t)(key >> 32), (uint32_t)(key & 0xffffffffu));

		// ---- collapse ---------------------------------------------------------
		std::vector<uint32_t> remapPos(posCount);
		for (uint32_t i = 0; i < posCount; ++i) remapPos[i] = i;
		auto resolve = [&](uint32_t p) { while (remapPos[p] != p) p = remapPos[p]; return p; };

		std::vector<bool> triDead(triCount, false);
		size_t liveTris = triCount;

		while (liveTris > targetTris && !queue.empty())
		{
			const Collapse c = queue.top(); queue.pop();
			if (c.version != version[c.from]) continue;     // stale entry
			const uint32_t from = resolve(c.from), to = resolve(c.to);
			if (from == to || locked[from]) continue;

			// Reject a collapse that would fold a triangle over on itself. Without
			// this, a level keeps its triangle budget but grows spikes and
			// inverted facets that flicker under any lighting.
			bool flips = false;
			for (uint32_t t : posToTris[from])
			{
				if (triDead[t]) continue;
				uint32_t p[3] = { resolve(triPos(t, 0)), resolve(triPos(t, 1)), resolve(triPos(t, 2)) };
				int hits = 0;
				for (int k = 0; k < 3; ++k) if (p[k] == from) { p[k] = to; ++hits; }
				if (hits == 0) continue;
				if (p[0] == p[1] || p[1] == p[2] || p[0] == p[2]) continue;   // becomes degenerate: fine
				const Vec3d& a0 = weld.positions[p[0]];
				const Vec3d& a1 = weld.positions[p[1]];
				const Vec3d& a2 = weld.positions[p[2]];
				const double ux = a1.x - a0.x, uy = a1.y - a0.y, uz = a1.z - a0.z;
				const double vx = a2.x - a0.x, vy = a2.y - a0.y, vz = a2.z - a0.z;
				double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;

				uint32_t o[3] = { resolve(triPos(t, 0)), resolve(triPos(t, 1)), resolve(triPos(t, 2)) };
				const Vec3d& b0 = weld.positions[o[0]];
				const Vec3d& b1 = weld.positions[o[1]];
				const Vec3d& b2 = weld.positions[o[2]];
				const double sx = b1.x - b0.x, sy = b1.y - b0.y, sz = b1.z - b0.z;
				const double tx = b2.x - b0.x, ty = b2.y - b0.y, tz = b2.z - b0.z;
				const double mx = sy * tz - sz * ty, my = sz * tx - sx * tz, mz = sx * ty - sy * tx;
				if (nx * mx + ny * my + nz * mz <= 0.0) { flips = true; break; }
			}
			if (flips) continue;

			remapPos[from] = to;
			quadrics[to].Add(quadrics[from]);
			++version[from]; ++version[to];
			out.error = (float)c.cost;

			for (uint32_t t : posToTris[from])
			{
				if (triDead[t]) continue;
				const uint32_t p0 = resolve(triPos(t, 0)), p1 = resolve(triPos(t, 1)), p2 = resolve(triPos(t, 2));
				if (p0 == p1 || p1 == p2 || p0 == p2) { triDead[t] = true; --liveTris; }
			}
			auto& dst = posToTris[to];
			dst.insert(dst.end(), posToTris[from].begin(), posToTris[from].end());
			posToTris[from].clear();

			// Re-cost the survivor's neighbourhood against its new quadric.
			for (uint32_t t : dst)
			{
				if (triDead[t]) continue;
				for (int k = 0; k < 3; ++k)
				{
					const uint32_t n = resolve(triPos(t, k));
					if (n != to) push(n, to);
				}
			}
		}

		// ---- rebuild indices --------------------------------------------------
		// Corners are remapped to a surviving VERTEX, not just a surviving
		// position: where several vertices share a point but carry different UVs,
		// pick the one whose UV is closest to the corner being replaced so the
		// texture does not jump across the seam.
		auto pickVertex = [&](uint32_t originalVertex, uint32_t survivingPos) -> uint32_t {
			const auto& candidates = weld.posToVertices[survivingPos];
			if (candidates.empty()) return originalVertex;
			if (candidates.size() == 1) return candidates[0];
			const float* uv = UvAt(src, originalVertex);
			if (!uv) return candidates[0];
			uint32_t best = candidates[0];
			float bestD = FLT_MAX;
			for (uint32_t cand : candidates)
			{
				const float* cuv = UvAt(src, cand);
				const float dx = cuv[0] - uv[0], dy = cuv[1] - uv[1];
				const float d = dx * dx + dy * dy;
				if (d < bestD) { bestD = d; best = cand; }
			}
			return best;
		};

		out.indices.clear();
		out.indices.reserve(liveTris * 3);
		for (size_t t = 0; t < triCount; ++t)
		{
			if (triDead[t]) continue;
			uint32_t corner[3];
			bool degenerate = false;
			for (int c = 0; c < 3; ++c)
			{
				const uint32_t original = tri[t * 3 + c];
				const uint32_t survivor = resolve(weld.vertexToPos[original]);
				corner[c] = (survivor == weld.vertexToPos[original]) ? original : pickVertex(original, survivor);
			}
			for (int c = 0; c < 3 && !degenerate; ++c)
				if (weld.vertexToPos[corner[c]] == weld.vertexToPos[corner[(c + 1) % 3]]) degenerate = true;
			if (degenerate) continue;
			out.indices.push_back(corner[0]);
			out.indices.push_back(corner[1]);
			out.indices.push_back(corner[2]);
		}
		out.triangles = (uint32_t)(out.indices.size() / 3);
		return out;
	}

	MeshSimplifyResult SimplifyMesh(const std::vector<Vertex>& verts,
		const std::vector<unsigned int>& indices, float targetRatio)
	{
		MeshSimplifySource src;
		src.positions = reinterpret_cast<const uint8_t*>(verts.data()) + offsetof(Vertex, position);
		src.positionStride = sizeof(Vertex);
		src.uv0 = reinterpret_cast<const uint8_t*>(verts.data()) + offsetof(Vertex, uvs);
		src.uvStride = sizeof(Vertex);
		src.vertexCount = (uint32_t)verts.size();
		return SimplifyMesh(src, reinterpret_cast<const uint32_t*>(indices.data()), indices.size(), targetRatio);
	}
}
