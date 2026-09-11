#pragma once
#include <d3d12.h>
#include "tge/rhi/CommandContext.h"

namespace Tga::rhi::dx12
{
	class Dx12Device;

	// Stage 2 milestone 1: only the methods BeginFrame/EndFrame's clear-and-
	// present loop needs are implemented (SetRenderTargets, SetViewport,
	// SetScissor, ClearRenderTarget, ClearDepthStencil, TransitionResource,
	// resource copy/update). Pipeline binding, draw/dispatch, and the bind-
	// by-slot methods assert -- they need the root signature + PSO cache,
	// which is milestone 2. See Dx12Device.h's class comment.
	class Dx12CommandContext final : public ICommandContext
	{
	public:
		explicit Dx12CommandContext(Dx12Device& aDevice) : myDevice(aDevice) {}

		void SetRenderTargets(uint32_t count, const RtvHandle* rtvs, DsvHandle dsv) override;
		void SetViewport(float x, float y, float w, float h, float minZ, float maxZ) override;
		void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) override;
		void ClearRenderTarget(RtvHandle rtv, const float rgba[4]) override;
		void ClearDepthStencil(DsvHandle dsv, float depth, uint8_t stencil, bool clearDepth, bool clearStencil) override;
		void ClearUnorderedAccessFloat(UavHandle uav, const float rgba[4]) override;

		void SetGraphicsPipeline(GraphicsPipelineHandle) override;
		void SetComputePipeline(ComputePipelineHandle) override;
		void SetBlendState(BlendMode) override;
		void SetDepthStencilState(DepthMode) override;
		void SetRasterizerState(RasterMode) override;
		void SetVertexShader(ShaderModuleHandle) override;
		void SetPixelShader(ShaderModuleHandle) override;
		void SetInputLayout(ShaderModuleHandle vsForReflection, const InputElement* elems, uint32_t count) override;
		void SetConstantBuffer(ShaderStage, uint32_t slot, BufferHandle) override;
		void SetDynamicConstantBuffer(ShaderStage, uint32_t slot, const DynamicAlloc&) override;
		void SetShaderResource(ShaderStage, uint32_t slot, SrvHandle) override;
		void SetShaderResources(ShaderStage, uint32_t firstSlot, uint32_t count, const SrvHandle*) override;
		void SetUnorderedAccess(uint32_t slot, UavHandle) override;
		void SetUnorderedAccesses(uint32_t firstSlot, uint32_t count, const UavHandle*) override;
		void SetSampler(ShaderStage, uint32_t slot, SamplerHandle) override;

		void SetVertexBuffer(uint32_t slot, BufferHandle, uint32_t stride, uint32_t offset) override;
		void SetIndexBuffer(BufferHandle, Format indexFormat, uint32_t offset) override;
		void SetPrimitiveTopology(Topology) override;

		void Draw(uint32_t vertexCount, uint32_t startVertex) override;
		void DrawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance) override;
		void DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex) override;
		void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance) override;
		void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

		void UpdateBuffer(BufferHandle, const void* data, uint32_t byteSize, uint32_t dstOffset) override;
		void UpdateTexture(TextureHandle, const void* data, uint32_t rowPitch) override;
		void CopyTexture(TextureHandle dst, TextureHandle src) override;
		void CopyTextureRegion(TextureHandle dst, uint32_t dstMip, uint32_t dstArray, TextureHandle src, uint32_t srcMip, uint32_t srcArray) override;
		void GenerateMips(SrvHandle) override;

		void TransitionResource(TextureHandle, ResourceState after) override;
		void TransitionResource(BufferHandle, ResourceState after) override;
		void UavBarrier(TextureHandle) override;
		void UavBarrier(BufferHandle) override;

		void WriteTimestampBegin(TimestampQueryHandle) override;
		void WriteTimestampEnd(TimestampQueryHandle) override;
		void PushMarker(const char*) override;
		void PopMarker() override;

	private:
		ID3D12GraphicsCommandList* List();
		Dx12Device& myDevice;
	};
}
