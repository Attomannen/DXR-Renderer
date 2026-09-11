#pragma once
#include <d3d12.h>
#include "tge/rhi/CommandContext.h"

namespace Tga::rhi::dx12
{
	class Dx12Device;

	// Stage 2 milestone 2: the full bind/draw/dispatch surface.
	//
	// Design notes (see p5g3-dx12-port memory for the full reasoning):
	// - Root CBVs (b0..b13) bind immediately via SetGraphicsRootConstantBufferView/
	//   SetComputeRootConstantBufferView -- D3D12 root arguments are "sticky"
	//   (persist across draws until overwritten), matching this engine's
	//   D3D11-derived "bind once, draw many times" usage pattern exactly, so
	//   there's no need to defer/re-resolve these at Draw time.
	// - SRV/UAV/Sampler descriptor TABLES can't be bound the same way (a table
	//   must be contiguous in the bound heap; this engine's arbitrary-slot
	//   binding produces scattered permanent-heap locations) -- these are
	//   tracked as pending CPU-side arrays and lazily copied into a fresh
	//   contiguous region of the device's per-frame scratch heap, then bound,
	//   right before the Draw/Dispatch that actually needs them. Whether that
	//   copy is skippable (nothing changed since the last Draw/Dispatch) is
	//   tracked per table via a dirty flag set on every Set*/cleared on use.
	// - The engine's ShaderStage parameter (Vertex/Pixel/Compute/AllGraphics/
	//   All) selects WHICH of the two root signatures (graphics vs compute --
	//   see Dx12Device::CreateRootSignatures) a bind targets, rather than
	//   selecting a D3D11-style per-stage independent slot: every root-
	//   signature slot in this design has D3D12_SHADER_VISIBILITY_ALL within
	//   its own root signature, matching the engine's free b/t/s/u register
	//   convention. A bind with Vertex/Pixel/AllGraphics touches the graphics
	//   side's pending state; Compute touches the compute side; All touches
	//   both (harmless -- whichever pipeline runs next picks up the value it
	//   needs).
	// - Fixed-function state (SetBlendState/DepthStencilState/RasterizerState)
	//   and shader/input-layout binds (SetVertexShader/PixelShader/
	//   SetInputLayout) are recorded as pending state and lazily assembled
	//   into a full GraphicsPipelineDesc -> CreateGraphicsPipeline (hash-
	//   cached) right before each Draw* call, exactly as the Stage 1 plan
	//   anticipated ("DX12 backend implements those by lazy-merging into its
	//   PSO"). SetGraphicsPipeline/SetComputePipeline (an already-built
	//   handle, the pattern DeferredRenderer's compute dispatch sites use)
	//   bind directly instead.
	class Dx12CommandContext final : public ICommandContext
	{
	public:
		explicit Dx12CommandContext(Dx12Device& aDevice);

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

		// Called once per frame by Dx12Device::BeginFrame (root signatures never
		// change across the app's life, so binding them once/frame -- rather
		// than tracking "did it change" -- is simplest and correct).
		void OnBeginFrame();

	private:
		ID3D12GraphicsCommandList* List();
		void ResolveGraphicsPipeline();     // lazily builds/looks up the PSO for pending state, binds it
		void FlushGraphicsTables();         // copies dirty SRV/Sampler tables into scratch, binds them
		void FlushComputeTables();          // copies dirty SRV/UAV/Sampler tables into scratch, binds them

		Dx12Device& myDevice;

		// ---- pending graphics fixed-function / shader state (lazily -> PSO) ----
		ShaderModuleHandle myPendingVs, myPendingPs;
		std::vector<InputElement> myPendingInputLayout;
		BlendMode myPendingBlend = BlendMode::Disabled;
		DepthMode myPendingDepth = DepthMode::WriteLess;
		RasterMode myPendingRaster = RasterMode::BackfaceCulling;
		Topology myPendingTopology = Topology::TriangleList;
		Format myBoundRtvFormats[8] = {};
		uint32_t myBoundRtvCount = 0;
		Format myBoundDsvFormat = Format::Unknown;
		GraphicsPipelineHandle myBoundGfxPipeline;   // last PSO actually bound (SetPipelineState skipped if unchanged)
		D3D_PRIMITIVE_TOPOLOGY myBoundTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

		// ---- bound resources, shared across graphics/compute (see class comment) ----
		static constexpr uint32_t kNumCbv = 14, kNumSrv = 24, kNumUav = 4, kNumSampler = 6;
		SrvHandle     myBoundSrv[kNumSrv] = {};
		UavHandle     myBoundUav[kNumUav] = {};
		SamplerHandle myBoundSampler[kNumSampler] = {};
		bool mySrvTableDirtyGraphics = true, mySrvTableDirtyCompute = true;
		bool myUavTableDirtyCompute = true;
		bool mySamplerTableDirtyGraphics = true, mySamplerTableDirtyCompute = true;

		BufferHandle myBoundVb[4] = {}; uint32_t myVbStride[4] = {}; uint32_t myVbOffset[4] = {};
		BufferHandle myBoundIb; Format myIbFormat = Format::R32_UInt; uint32_t myIbOffset = 0;
	};
}
