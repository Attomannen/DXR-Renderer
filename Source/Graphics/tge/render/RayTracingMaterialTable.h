#pragma once

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <tge/stringRegistry/StringRegistry.h>
#include <tge/rhi/Handles.h>
#include <tge/rhi/StructuredBuffer.h>
#include <tge/log/Log.h>
#include <tge/material/MaterialParams.h>

namespace Tga
{
	// Process-wide, append-only identity table for ray-tracing materials. Slot
	// zero is reserved as an invalid/default record; live slots never move or
	// get reused, so a geometry lookup remains stable for its lifetime.
	//
	// Also holds each material's texture SRVs (set once, from ModelFactory's
	// AssignDefaultMaterials -- the same site that resolves them for the
	// raster path) so a hit shader can be handed a GPU-visible record per
	// materialIndex. The SRVs stored here are the engine's own permanent
	// handles; GameWorld re-registers them into the bindless ray-scene table
	// every frame (see BuildRaytracingTlas's caller) exactly like the raw
	// vertex/index buffers already are -- RegisterRaySceneSrv dedups by
	// source handle, so that's a cheap hashmap hit after the first frame,
	// not a real per-frame cost.
	class RayTracingMaterialTable
	{
	public:
		struct TextureSet
		{
			rhi::SrvHandle albedo, normal, orm, emissive;
		};

		// How inline ray queries treat a material's triangles.
		// kRayUnclassified: nothing has classified it, so AcceptRayTriangle
		//   (DxrCommon.hlsli) does the always-safe cutout test whenever a base
		//   colour map exists. Any loader that never calls SetRayVisibility
		//   still renders masked materials correctly.
		// kRayTransparent: forward-composited (glass, alpha blend); never a DXR
		//   blocker.
		// kRayMasked: confirmed per-pixel cutout.
		// kRayOpaque: confirmed solid, so the TLAS can mark the instance
		//   FORCE_OPAQUE and skip the candidate shader entirely.
		static constexpr uint32_t kRayUnclassified = 0, kRayTransparent = 1, kRayMasked = 2, kRayOpaque = 3;

		static void SetMaterialParams(uint32_t index, const MaterialParams& params)
		{
			if (index == 0 || index >= Params().size()) return;
			if (std::memcmp(&Params()[index], &params, sizeof(params)) == 0) return;
			Params()[index] = params;
			++Revision();
		}
		static const MaterialParams& GetMaterialParams(uint32_t index)
		{
			return Params()[index < Params().size() ? index : 0];
		}

		// aRayVisibility: one of kRayOpaque / kRayTransparent / kRayMasked.
		static void SetRayVisibility(uint32_t index, uint32_t aRayVisibility)
		{
			if (index == 0 || index >= Visibility().size()) return;
			if (Visibility()[index] == aRayVisibility) return;
			Visibility()[index] = aRayVisibility;
			++Revision();
		}
		static uint32_t GetRayVisibility(uint32_t index)
		{
			return index < Visibility().size() ? Visibility()[index] : kRayUnclassified;
		}

		static uint32_t GetOrAssignMaterialIndex(StringId materialName)
		{
			auto& indices = Indices();
			if (const auto it = indices.find(materialName); it != indices.end()) return it->second;
			const uint32_t index = static_cast<uint32_t>(Names().size());
			Names().push_back(materialName);
			Textures().push_back({});
			Params().push_back(MakeMaterialParams(true));
			Visibility().push_back(kRayUnclassified);
			indices.emplace(materialName, index);
			++Revision();
			return index;
		}

		// Called once per material from ModelFactory::AssignDefaultMaterials.
		// Slots are 0/invalid for any texture a material doesn't have; the GPU
		// record then falls back to a flat default rather than sampling garbage.
		static void SetMaterialTextures(uint32_t index, const TextureSet& textures)
		{
			if (index == 0 || index >= Textures().size()) return;   // slot 0 stays the reserved default
			const TextureSet& old = Textures()[index];
			if (old.albedo == textures.albedo && old.normal == textures.normal &&
				old.orm == textures.orm && old.emissive == textures.emissive) return;
			Textures()[index] = textures;
			++Revision();
		}

		// Mirrors AcceptRayTriangle (DxrCommon.hlsli) exactly: true only for a
		// material whose candidate test provably cannot return false, so the
		// TLAS can mark that instance FORCE_OPAQUE and let traversal hardware
		// commit its triangles without ever exiting to the shader. Anything
		// else -- confirmed cutout, forward-transparent, or simply not yet
		// classified -- stays FORCE_NON_OPAQUE so AcceptRayTriangle still gets
		// its say. Keep the two in lockstep: relaxing this without relaxing
		// AcceptRayTriangle turns cutout foliage into solid quads.
		static bool IsRayOpaque(uint32_t index)
		{
			if (index == 0 || index >= Params().size()) return false;
			const uint32_t visibility = Visibility()[index];
			if (visibility == kRayOpaque) return true;
			if (visibility != kRayUnclassified) return false;
			// Unclassified: AcceptRayTriangle accepts unconditionally when the
			// material is constants-only or has no base colour map.
			const bool textured = (Params()[index].flags & MaterialFlags::UseTextures) != 0;
			return !textured || !Textures()[index].albedo.IsValid();
		}

		static uint32_t Count() { return static_cast<uint32_t>(Names().size() - 1); }
		static const std::vector<TextureSet>& AllTextures() { return Textures(); }

		// GPU-side record layout matches RayMaterialRecord in DxrCommon.hlsli.
		// A zero texture index means "no texture". Constants-only materials
		// (no MaterialFlags::UseTextures) skip texture sampling.
		struct GpuRecord
		{
			uint32_t albedoSrv, normalSrv, ormSrv, emissiveSrv;
			MaterialParams params;
			uint32_t rayVisibility, _pad0, _pad1, _pad2;
		};
		static_assert(sizeof(GpuRecord) == 112);

		// Upload once per active frame/material revision. Later ray dispatches
		// share that version without re-registering textures or rewriting data.
		// Returns an invalid handle if there are no materials yet.
		static rhi::SrvHandle Upload(rhi::IDevice& aDevice, rhi::ICommandContext& aCtx)
		{
			const std::vector<TextureSet>& textures = Textures();
			const uint32_t count = (uint32_t)textures.size();
			if (count <= 1) return {};   // only the reserved default slot exists
			// Every active frame still uploads into its own backing slot. Reuse
			// that immutable version across probe, camera and volume dispatches.
			// A material edit invalidates the cache even within the same frame.
			const uint32_t frame = aDevice.GetFrameIndex();
			if (Buffer().IsValid() && UploadedFrame() == frame && UploadedRevision() == Revision())
				return Buffer().Srv();

			std::vector<GpuRecord> records(count);
			for (uint32_t i = 0; i < count; ++i)
			{
				const TextureSet& t = textures[i];
				records[i] = {
					t.albedo.IsValid()   ? aDevice.RegisterRaySceneSrv(t.albedo)   : 0u,
					t.normal.IsValid()   ? aDevice.RegisterRaySceneSrv(t.normal)   : 0u,
					t.orm.IsValid()      ? aDevice.RegisterRaySceneSrv(t.orm)      : 0u,
					t.emissive.IsValid() ? aDevice.RegisterRaySceneSrv(t.emissive) : 0u,
					Params()[i],
					Visibility()[i], 0u, 0u, 0u,
				};
			}

			rhi::StructuredBuffer& buf = Buffer();
			if (!buf.IsValid() || BufferCapacity() != count)
			{
				buf.Create(aDevice, sizeof(GpuRecord), count, /*withUav*/ false, /*cpuUpdatable*/ true, "RayMaterialTable");
				BufferCapacity() = count;
			}
			buf.Update(aCtx, records.data(), (uint32_t)(records.size() * sizeof(GpuRecord)));
			UploadedFrame() = frame;
			UploadedRevision() = Revision();
			return buf.Srv();
		}

	private:
		static uint64_t& Revision() { static uint64_t value = 1; return value; }
		static uint64_t& UploadedRevision() { static uint64_t value = 0; return value; }
		static uint32_t& UploadedFrame() { static uint32_t value = ~0u; return value; }
		static std::vector<MaterialParams>& Params()
		{ static std::vector<MaterialParams> value(1, MakeMaterialParams(true)); return value; }
		static std::vector<uint32_t>& Visibility()
		{ static std::vector<uint32_t> value(1, kRayUnclassified); return value; }
		static uint32_t& BufferCapacity() { static uint32_t value = 0; return value; }
		static rhi::StructuredBuffer& Buffer() { static rhi::StructuredBuffer value; return value; }
		static std::unordered_map<StringId, uint32_t>& Indices()
		{ static std::unordered_map<StringId, uint32_t> value; return value; }
		static std::vector<StringId>& Names()
		{ static std::vector<StringId> value(1); return value; }
		// Parallel to Names() -- index 0 is the reserved default (all null SRVs).
		static std::vector<TextureSet>& Textures()
		{ static std::vector<TextureSet> value(1); return value; }
	};
}
