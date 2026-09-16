#pragma once

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <tge/stringRegistry/StringRegistry.h>
#include <tge/rhi/Handles.h>
#include <tge/rhi/StructuredBuffer.h>
#include <tge/log/Log.h>

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
		// Constant PBR overrides for procedural surfaces and material previews.
		// These follow the texture indices in the 64-byte RayMaterialRecord.
		struct FixedMaterial
		{
			float baseColor[3] = { 0.8f, 0.8f, 0.8f };
			uint32_t enabled = 0;
			float roughness = 0.5f, metalness = 0.f, ao = 1.f, emissiveStrength = 0.f;
			float emissiveColor[3] = { 1.f, 1.f, 1.f };
			// kRayUnclassified (default): nothing has told this material record
			// whether it needs a real cutout test, so AcceptRayTriangle
			// (DxrCommon.hlsli) falls back to the old, always-safe behaviour --
			// sample the albedo texture and compare to the cutout threshold
			// whenever one exists. This is deliberately the default so any model
			// loading path that never calls SetRayVisibility (this table is a
			// single process-wide append-only table shared by every loader) still
			// renders masked/cutout materials correctly instead of silently
			// treating them as solid.
			// kRayOpaque: a caller has positively confirmed (from real authored
			// surfaceType data, not a guess) that this material has no cutout, so
			// AcceptRayTriangle can accept it immediately with no texture sample
			// at all. This is the actual performance-relevant state: every
			// RayQuery in this engine carries RAY_FLAG_FORCE_NON_OPAQUE (needed so
			// masked/cutout geometry can be alpha-tested inline at all), which
			// means every candidate triangle of every material -- opaque included
			// -- runs through AcceptRayTriangle regardless of this flag. Without
			// this explicit fast path, ordinary opaque geometry (walls, floors,
			// static props -- most of a typical scene) paid the same texture
			// sample as real cutout geometry, for no visual difference.
			// kRayTransparent: alpha-blended, composited by the forward
			// transparent pass; must never become an opaque DXR blocker, so it's
			// excluded from candidacy entirely.
			// kRayMasked: a caller has positively confirmed real per-pixel
			// cutout -- same texture sample + threshold compare as Unclassified,
			// kept as its own explicit state for clarity at call sites.
			static constexpr uint32_t kRayUnclassified = 0, kRayTransparent = 1, kRayMasked = 2, kRayOpaque = 3;
			uint32_t rayVisibility = kRayUnclassified;
		};
		static void SetFixedMaterial(uint32_t index, const FixedMaterial& material)
		{
			if (index == 0 || index >= FixedMaterials().size()) return;
			const uint32_t rayVisibility = FixedMaterials()[index].rayVisibility;
			FixedMaterial next = material;
			next.enabled = 1;
			next.rayVisibility = rayVisibility;
			static_assert(sizeof(FixedMaterial) == 48);
			if (std::memcmp(&FixedMaterials()[index], &next, sizeof(next)) == 0) return;
			FixedMaterials()[index] = next;
			++Revision();
		}
		// aRayVisibility: one of FixedMaterial::kRayOpaque/kRayTransparent/kRayMasked.
		static void SetRayVisibility(uint32_t index, uint32_t aRayVisibility)
		{
			if (index == 0 || index >= FixedMaterials().size()) return;
			if (FixedMaterials()[index].rayVisibility == aRayVisibility) return;
			FixedMaterials()[index].rayVisibility = aRayVisibility;
			++Revision();
		}

		static uint32_t GetOrAssignMaterialIndex(StringId materialName)
		{
			auto& indices = Indices();
			if (const auto it = indices.find(materialName); it != indices.end()) return it->second;
			const uint32_t index = static_cast<uint32_t>(Names().size());
			Names().push_back(materialName);
			Textures().push_back({});
			FixedMaterials().push_back({});
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
			if (index == 0 || index >= FixedMaterials().size()) return false;
			const FixedMaterial& m = FixedMaterials()[index];
			if (m.rayVisibility == FixedMaterial::kRayOpaque) return true;
			if (m.rayVisibility == FixedMaterial::kRayTransparent ||
				m.rayVisibility == FixedMaterial::kRayMasked) return false;
			// kRayUnclassified: AcceptRayTriangle falls through to
			// `useFixedMaterial != 0 || albedoSrv == 0 -> return true`, i.e. an
			// unconditional accept, which is exactly what opaque means. Those
			// two terms are `FixedMaterial::enabled` and "no albedo texture
			// registered" on this side (see Upload's GpuRecord construction).
			return m.enabled != 0u || !Textures()[index].albedo.IsValid();
		}

		static uint32_t Count() { return static_cast<uint32_t>(Names().size() - 1); }
		static const std::vector<TextureSet>& AllTextures() { return Textures(); }

		// GPU-side record layout matches RayMaterialRecord in DxrCommon.hlsli.
		// A zero texture index means "no texture". Enabled fixed materials
		// bypass texture sampling and supply the preview's constant PBR values.
		struct GpuRecord
		{
			uint32_t albedoSrv, normalSrv, ormSrv, emissiveSrv;
			FixedMaterial fixed;
		};
		static_assert(sizeof(GpuRecord) == 64);

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
					FixedMaterials()[i],
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
		static std::vector<FixedMaterial>& FixedMaterials()
		{ static std::vector<FixedMaterial> value(1); return value; }
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
