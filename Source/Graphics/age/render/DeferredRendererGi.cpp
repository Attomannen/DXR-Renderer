#include "stdafx.h"
#include "DeferredRendererInternal.h"

// DeferredRenderer: Irradiance probe volume: raster and ray-traced probe capture.

using namespace Ag;

void DeferredRenderer::SetGiVolume(const Vector3f& o, const Vector3f& s, int cx, int cy, int cz, float intensity, bool enabled, float autoSealStrength)
{
	if (!myGiVolumeCb.IsValid()) return;
	float d[12] = {
		o.x, o.y, o.z, enabled ? 1.f : 0.f,
		s.x, s.y, s.z, intensity,
		(float)0, (float)0, (float)0, autoSealStrength   // d[11] read as asfloat(gGiCounts.w)
	};
	// int4 counts -- reinterpret the first three of the last row's bits (d[11] stays float)
	int32_t* ci = (int32_t*)(d + 8);
	ci[0] = cx; ci[1] = cy; ci[2] = cz;
	myGiVolumeCb.Update(DX11::Rhi()->GetContext(), d, sizeof(d));
}

void DeferredRenderer::SetGiEnvironment(rhi::SrvHandle aSrv, const Vector3f& aTint, bool aEnabled)
{
	myGiEnvironmentSrv = aSrv;
	myGiEnvironmentTint = aTint;
	myGiEnvironmentEnabled = aEnabled && aSrv.IsValid();
}

void DeferredRenderer::ClearGi()
{
	if (!myGiShBuffer.Uav().IsValid()) return;
	const float z[4] = { 0.f, 0.f, 0.f, 0.f };
	DX11::Rhi()->GetContext().ClearUnorderedAccessFloat(myGiShBuffer.Uav(), z);
	DX11::Rhi()->GetContext().ClearUnorderedAccessFloat(myGiShPreviousBuffer.Uav(), z);
	DX11::Rhi()->GetContext().ClearUnorderedAccessFloat(myGiVisibilityBuffer.Uav(), z);
	DX11::Rhi()->GetContext().ClearUnorderedAccessFloat(myGiVisibilityPreviousBuffer.Uav(), z);
}

void DeferredRenderer::GiProjectProbe(rhi::SrvHandle aCubeSrv, int aProbeIndex, float aHysteresis, int aFaceRes)
{
	if (!HasGi() || !aCubeSrv.IsValid() || aProbeIndex < 0 || aProbeIndex >= kMaxGiProbes) return;

	{
		struct { uint32_t idx; float hyst; uint32_t faceRes; float pad; } p{
			(uint32_t)aProbeIndex, aHysteresis, (uint32_t)std::max(aFaceRes, 1), 0.f };
		myGiProjectCb.Update(DX11::Rhi()->GetContext(), p);
	}

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	rhi::ComputePipelineDesc pd;
	pd.cs = myGiProjectCS->module;
	ctx.SetComputePipeline(DX11::Rhi()->CreateComputePipeline(pd));
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, aCubeSrv);
	ctx.SetSampler(rhi::ShaderStage::Compute, 0, myGiLinearSampler);
	myGiProjectCb.Bind(ctx);
	ctx.SetUnorderedAccess(0, myGiShBuffer.Uav());
	ctx.SetUnorderedAccess(1, myGiVisibilityBuffer.Uav());

	ctx.Dispatch(1, 1, 1);

	ctx.SetUnorderedAccess(0, {});
	ctx.SetUnorderedAccess(1, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, {});
	ctx.SetComputePipeline({});
}

void DeferredRenderer::GiProjectProbeRT(const Vector3f& aProbePos, int aProbeIndex, float aHysteresis, int aRayCount, float aFireflyClamp)
{
	const GiProbeBatchEntry entry{ aProbePos, aProbeIndex };
	GiProjectProbeBatchRT(&entry, 1, aHysteresis, aRayCount, aFireflyClamp);
}

void DeferredRenderer::GiProjectProbeBatchRT(const GiProbeBatchEntry* aEntries, int aCount,
	float aHysteresis, int aRayCount, float aFireflyClamp)
{
	if (!HasGiRT() || !aEntries || aCount <= 0) return;
	AG_PROFILE_SCOPE(myProfiler, "GI probe trace");

	// More probes than one batch buffer holds: split instead of dropping the
	// tail, so a large giPrimeBatch still refreshes everything it asked for.
	// Bounded by StructuredBuffer's 4-updates-per-frame budget; beyond that the
	// buffer would be rewritten under a dispatch already in the command list.
	if (aCount > kMaxGiProbeBatch)
	{
		constexpr int kMaxChunks = 4;
		const int chunks = std::min(kMaxChunks, (aCount + kMaxGiProbeBatch - 1) / kMaxGiProbeBatch);
		for (int c = 0; c < chunks; ++c)
		{
			const int offset = c * kMaxGiProbeBatch;
			GiProjectProbeBatchRT(aEntries + offset, std::min(kMaxGiProbeBatch, aCount - offset),
			                      aHysteresis, aRayCount, aFireflyClamp);
		}
		return;
	}

	rhi::IDevice* dev = DX11::Rhi();
	rhi::ICommandContext& ctx = dev->GetContext();
	// GameWorld builds the TLAS after BeginFrame. Refresh the two root SRVs
	// now, and skip this batch when the current scene has no instances.
	if (!dev->BindRaytracingSceneForCompute()) return;

	// Compact out-of-range probes rather than dispatching groups for them: the
	// shader indexes GiSH/GiVisibility straight from this entry, so a bad index
	// would be an out-of-bounds UAV write.
	float batch[kMaxGiProbeBatch * 4];
	int groups = 0;
	for (int i = 0; i < aCount && groups < kMaxGiProbeBatch; ++i)
	{
		if (aEntries[i].index < 0 || aEntries[i].index >= kMaxGiProbes) continue;
		batch[groups * 4 + 0] = aEntries[i].position.x;
		batch[groups * 4 + 1] = aEntries[i].position.y;
		batch[groups * 4 + 2] = aEntries[i].position.z;
		// Bit-cast, not a float conversion: the shader reads this back with
		// asuint() and uses it directly as the probe's volume index.
		const uint32_t index = (uint32_t)aEntries[i].index;
		std::memcpy(&batch[groups * 4 + 3], &index, sizeof(index));
		++groups;
	}
	if (groups == 0) return;
	myGiProbeBatchBuffer.Update(ctx, batch, (uint32_t)(groups * 4 * sizeof(float)));

	{
		GiTraceCb cb{};
		cb.hysteresis = aHysteresis;
		cb.rayCount = (uint32_t)std::max(aRayCount, 1);
		cb.textureFiltering = myTunables.dxrTextureFiltering ? 1u : 0u;
		cb.specularAaStrength = myTunables.specularAaEnabled ? myTunables.specularAaStrength : 0.f;
		cb.lightCount = (uint32_t)myLightCount;
		// myShadowLightDir is the direction the sun's rays TRAVEL; shading
		// wants the opposite -- surface (or here, probe) toward the light.
		// Same convention as RenderDxrLighting's sunDirToLight.
		const float lx = -myShadowLightDir.x, ly = -myShadowLightDir.y, lz = -myShadowLightDir.z;
		const float lLen = std::sqrt(lx * lx + ly * ly + lz * lz);
		if (lLen > 1e-5f) { cb.sunDirToLight[0] = lx / lLen; cb.sunDirToLight[1] = ly / lLen; cb.sunDirToLight[2] = lz / lLen; }
		else { cb.sunDirToLight[0] = 0.f; cb.sunDirToLight[1] = 1.f; cb.sunDirToLight[2] = 0.f; }
		cb.sunRadiance[0] = myTunables.dxrSunTint[0] * myTunables.dxrSunIntensity;
		cb.sunRadiance[1] = myTunables.dxrSunTint[1] * myTunables.dxrSunIntensity;
		cb.sunRadiance[2] = myTunables.dxrSunTint[2] * myTunables.dxrSunIntensity;
		cb.ambientIntensity = myTunables.dxrAmbientIntensity * SkyBrightnessScale();
	// Use the same environment energy for primary lighting and sky bounce.
	constexpr float kDxrEnvironmentScale = 1.0f;
	const Vector3f environmentTint = EnvironmentTint() * kDxrEnvironmentScale;
	cb.environmentTint[0] = environmentTint.x;
	cb.environmentTint[1] = environmentTint.y;
	cb.environmentTint[2] = environmentTint.z;
		// Generated probe maps have four diffuse-tail mips after four specular
		// mips; LOD 6 is a stable diffuse approximation for both those maps and
		// the shipped fallback environment assets. -1 disables the term.
		cb.environmentDiffuseMip = myGiEnvironmentEnabled ? 6.f : -1.f;
		cb.sampleSequence = myGiTraceSequence++;
		cb.fireflyClamp = std::max(aFireflyClamp, 0.01f);
		cb.infiniteBounce = std::max(myTunables.dxrGiInfiniteBounce, 0.f);
		myGiTraceCb.Update(ctx, cb);
	}

	// Share the current frame's material version with other ray passes.
	const rhi::SrvHandle materialSrv = RayTracingMaterialTable::Upload(*dev, ctx);

	rhi::ComputePipelineDesc pd;
	pd.cs = myGiTraceCS->module;
	ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
	myGiTraceCb.Bind(ctx);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, materialSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, myLightBuffer.Srv());
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 3, myGiEnvironmentSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 6, myGiProbeBatchBuffer.Srv());
	ctx.SetSampler(rhi::ShaderStage::Compute, 0, myDxrMaterialSampler);
	ctx.SetUnorderedAccess(0, myGiShBuffer.Uav());
	ctx.SetUnorderedAccess(1, myGiVisibilityBuffer.Uav());

	// One 64-thread group per probe, all probes in one dispatch. Each group
	// writes only its own probe's 9 SH slots and 64 moment bins, so groups
	// never touch each other's storage and need no ordering between them --
	// see GiProjectProbeBatchRT's declaration for the one precondition that
	// keeps that true.
	ctx.Dispatch((uint32_t)groups, 1, 1);
	// Still needed once per batch: the camera pass (and the next batch) read
	// GiSH as an SRV, so the writes above must be visible before then.
	ctx.UavBarrier(myGiShBuffer.Handle());

	ctx.SetUnorderedAccess(0, {});
	ctx.SetUnorderedAccess(1, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 3, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 6, {});
	ctx.SetComputePipeline({});
}
