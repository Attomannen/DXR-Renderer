#pragma once
#include "age/rhi/Descs.h"

// Command recording surface. On DX11 every method forwards to the immediate
// ID3D11DeviceContext; on DX12 it records into an ID3D12GraphicsCommandList and
// the barrier tracker turns TransitionResource / bind-time usage into
// ResourceBarrier calls.

namespace Ag::rhi
{
	class ICommandContext
	{
	public:
		virtual ~ICommandContext() = default;

		// ---- targets / viewport / clears ----
		virtual void SetRenderTargets(uint32_t count, const RtvHandle* rtvs, DsvHandle dsv) = 0;
		virtual void SetViewport(float x, float y, float w, float h, float minZ = 0.f, float maxZ = 1.f) = 0;
		virtual void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) = 0;
		virtual void ClearRenderTarget(RtvHandle rtv, const float rgba[4]) = 0;
		virtual void ClearDepthStencil(DsvHandle dsv, float depth = 1.f, uint8_t stencil = 0,
		                               bool clearDepth = true, bool clearStencil = false) = 0;
		virtual void ClearUnorderedAccessFloat(UavHandle uav, const float rgba[4]) = 0;

		// ---- pipeline + bindings (by register slot) ----
		virtual void SetGraphicsPipeline(GraphicsPipelineHandle) = 0;
		virtual void SetComputePipeline(ComputePipelineHandle) = 0;

		// Fixed-function state set independently of a graphics pipeline. The DX11
		// backend binds the matching state object immediately; a DX12 backend
		// records the choice and folds it into the next pipeline it builds.
		// (Migration aid for GraphicsStateStack; draw-time PSO assembly by the
		// consumer supersedes these once every draw site is migrated.)
		virtual void SetBlendState(BlendMode) = 0;
		virtual void SetDepthStencilState(DepthMode) = 0;
		virtual void SetRasterizerState(RasterMode) = 0;
		// Immediate-style shader + input-layout binds (same migration seam as the
		// state setters above). `elems == nullptr || count == 0` => no input layout.
		virtual void SetVertexShader(ShaderModuleHandle) = 0;
		virtual void SetPixelShader(ShaderModuleHandle) = 0;
		virtual void SetInputLayout(ShaderModuleHandle vsForReflection,
		                            const InputElement* elems, uint32_t count) = 0;
		virtual void SetConstantBuffer(ShaderStage, uint32_t slot, BufferHandle) = 0;
		virtual void SetDynamicConstantBuffer(ShaderStage, uint32_t slot, const DynamicAlloc&) = 0;
		virtual void SetShaderResource(ShaderStage, uint32_t slot, SrvHandle) = 0;
		virtual void SetShaderResources(ShaderStage, uint32_t firstSlot, uint32_t count, const SrvHandle*) = 0;
		virtual void SetUnorderedAccess(uint32_t slot, UavHandle) = 0;
		virtual void SetUnorderedAccesses(uint32_t firstSlot, uint32_t count, const UavHandle*) = 0;
		virtual void SetSampler(ShaderStage, uint32_t slot, SamplerHandle) = 0;

		// ---- geometry ----
		virtual void SetVertexBuffer(uint32_t slot, BufferHandle, uint32_t stride, uint32_t offset = 0) = 0;
		virtual void SetIndexBuffer(BufferHandle, Format indexFormat, uint32_t offset = 0) = 0;
		virtual void SetPrimitiveTopology(Topology) = 0;

		// ---- draw / dispatch ----
		virtual void Draw(uint32_t vertexCount, uint32_t startVertex = 0) = 0;
		virtual void DrawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount,
		                           uint32_t startVertex = 0, uint32_t startInstance = 0) = 0;
		virtual void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0, int32_t baseVertex = 0) = 0;
		virtual void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
		                                  uint32_t startIndex = 0, int32_t baseVertex = 0,
		                                  uint32_t startInstance = 0) = 0;
		virtual void Dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

		// ---- resource ops ----
		virtual void UpdateBuffer(BufferHandle, const void* data, uint32_t byteSize, uint32_t dstOffset = 0) = 0;
		// Full upload of a single-mip, single-slice 2D texture's contents (DX11:
		// UpdateSubresource on mip 0 / slice 0; DX12 will route through an upload
		// heap + copy). `rowPitch` is the source data's tightly-packed row stride.
		virtual void UpdateTexture(TextureHandle, const void* data, uint32_t rowPitch) = 0;
		virtual void CopyTexture(TextureHandle dst, TextureHandle src) = 0;
		virtual void CopyTextureRegion(TextureHandle dst, uint32_t dstMip, uint32_t dstArray,
		                               TextureHandle src, uint32_t srcMip, uint32_t srcArray) = 0;
		// `owner` is the texture the srv views -- DX11's driver-magic
		// GenerateMips only needs the srv itself, but DX12 has no native
		// equivalent and must build per-mip RTVs/SRVs on the underlying
		// texture directly, which an SrvHandle alone can't recover (the SRV
		// pool doesn't track its owning texture). The caller already has
		// both handles at every real call site, so this is a free widening.
		virtual void GenerateMips(SrvHandle, TextureHandle owner) = 0;

		// ---- explicit state (no-ops on DX11) ----
		virtual void TransitionResource(TextureHandle, ResourceState after) = 0;
		virtual void TransitionResource(BufferHandle, ResourceState after) = 0;
		virtual void UavBarrier(TextureHandle) = 0;
		virtual void UavBarrier(BufferHandle) = 0;

		// ---- timestamps / markers ----
		virtual void WriteTimestampBegin(TimestampQueryHandle) = 0;
		virtual void WriteTimestampEnd(TimestampQueryHandle) = 0;
		virtual void PushMarker(const char*) = 0;
		virtual void PopMarker() = 0;
	};
}
