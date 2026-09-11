#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <unordered_map>

#include "tge/rhi/Device.h"
#include "tge/rhi/Pool.h"
#include "tge/rhi/dx12/Dx12DescriptorHeap.h"

namespace Tga::rhi::dx12
{
	using Microsoft::WRL::ComPtr;
	class Dx12CommandContext;

	// Shared with Dx12CommandContext (IASetPrimitiveTopology needs the D3D
	// enum, not just the PSO's coarser D3D12_PRIMITIVE_TOPOLOGY_TYPE).
	D3D_PRIMITIVE_TOPOLOGY ToD3D12Topology(Topology t);

	struct BufferRec  { ComPtr<ID3D12Resource> res; BufferDesc desc; D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON; };
	struct TextureRec { ComPtr<ID3D12Resource> res; TextureDesc desc; D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON; };
	struct ShaderRec  { std::vector<uint8_t> bytecode; ShaderKind kind = ShaderKind::Vertex; };
	struct GfxPipelineRec     { ComPtr<ID3D12PipelineState> pso; D3D12_PRIMITIVE_TOPOLOGY topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; };
	struct ComputePipelineRec { ComPtr<ID3D12PipelineState> pso; };
	struct TimestampRec { uint32_t queryIndex = 0; uint64_t frame = ~0ull; bool hasBegin = false, hasEnd = false; };
	// RTV/DSV need their view format remembered (not just the heap slot) so
	// SetRenderTargets can hand ResolveGraphicsPipeline the RTVFormats/DSVFormat
	// a PSO must be created with -- CreateGraphicsPipelineState requires them
	// to match whatever's actually bound, and RtvHandle/DsvHandle alone don't
	// carry that back to the command context.
	struct RtvRec { uint32_t slot = 0; Format format = Format::Unknown; };
	struct DsvRec { uint32_t slot = 0; Format format = Format::Unknown; };

	// Stage 2 milestone 2: root signature + PSO cache + the full
	// ICommandContext bind/draw/dispatch surface. Milestone 1's device/
	// swapchain/heaps/resource-creation/present-loop foundation is unchanged;
	// this fills in everything that was stubbed there. See the "descriptor
	// heap layout" comment below and p5g3-dx12-port memory for the design
	// (in particular: why CBV/SRV/UAV and sampler creation now happen in
	// NON-shader-visible heaps, with separate shader-visible per-frame
	// scratch heaps for the actual bind-time descriptor tables).
	//
	// Still NOT implemented (asserts if reached): GenerateMips (no DX12
	// equivalent -- needs a compute-shader mip generator), timestamp query
	// begin/end (the query heap + readback buffer exist from milestone 1;
	// wiring WriteTimestampBegin/End + resolving GetTimestampMs is not done).
	// ImGui interop is now real (milestone 3): imgui_impl_dx12 is wired up in
	// ImGuiInterface.cpp via GetNativeCommandQueue/GetNativeCommandList/
	// GetImGuiSrvDescriptorHeap/ImGuiFontSrv*Handle; ImGuiTextureId (for
	// ImGui::Image on an arbitrary rhi SRV, e.g. Viewport.cpp's scene preview)
	// is UNVERIFIED and likely still wrong for the milestone-2
	// dual-heap redesign (it reads a GPU handle off the permanent,
	// non-shader-visible myCbvSrvUavHeap, which has no valid GPU handle at
	// all) -- but nothing currently calls it under DX12, since that call site
	// (Viewport.cpp) still uses a raw, DX11-only accessor and is one of the
	// already-documented separate gaps. Fix alongside that call site's own
	// DX12 migration, not here.
	// This backend is wired into DX11::Init()'s live bootstrap via
	// TGE_RHI=dx12 as of milestone 3 (RenderTarget/DepthBuffer storage
	// migration).
	class Dx12Device final : public IDevice
	{
	public:
		explicit Dx12Device(const DeviceDesc&);
		~Dx12Device() override;

		Backend GetBackend() const override { return Backend::DX12; }

		BufferHandle  CreateBuffer(const BufferDesc&, const void* initialData = nullptr) override;
		TextureHandle CreateTexture(const TextureDesc&, const SubresourceData* initial = nullptr, uint32_t initialCount = 0) override;

		SrvHandle CreateSrv(TextureHandle, const SrvDesc&) override;
		SrvHandle CreateSrv(BufferHandle, const SrvDesc&) override;
		UavHandle CreateUav(TextureHandle, const UavDesc&) override;
		UavHandle CreateUav(BufferHandle, const UavDesc&) override;
		RtvHandle CreateRtv(TextureHandle, const RtvDesc&) override;
		DsvHandle CreateDsv(TextureHandle, const DsvDesc&) override;
		SamplerHandle CreateSampler(const SamplerDesc&) override;

		ShaderModuleHandle CreateShaderModule(ShaderKind, const void* bytecode, size_t size) override;
		GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc&) override;
		ComputePipelineHandle  CreateComputePipeline(const ComputePipelineDesc&) override;

		void Destroy(BufferHandle) override;
		void Destroy(TextureHandle) override;
		void Destroy(SrvHandle) override;
		void Destroy(UavHandle) override;
		void Destroy(RtvHandle) override;
		void Destroy(DsvHandle) override;
		void Destroy(SamplerHandle) override;
		void Destroy(ShaderModuleHandle) override;

		DynamicAlloc AllocateDynamicConstants(const void* data, uint32_t byteSize) override;

		bool          Resize(uint32_t w, uint32_t h) override;
		TextureHandle GetBackBuffer() const override { return myBackBufferTex[myFrameIndex]; }
		RtvHandle     GetBackBufferRtv(bool srgb) const override { return srgb ? myBackBufferRtv[myFrameIndex] : myBackBufferRtvNoSrgb[myFrameIndex]; }
		TextureHandle GetDefaultDepth() const override { return myDepthTex; }
		DsvHandle     GetDefaultDepthDsv() const override { return myDepthDsv; }
		Vector2ui     GetResolution() const override { return myResolution; }

		ICommandContext& BeginFrame() override;
		void             EndFrame(bool vsync) override;
		ICommandContext& GetContext() override;

		TimestampQueryHandle CreateTimestampQuery() override;
		void  DestroyTimestampQuery(TimestampQueryHandle) override;
		bool  GetTimestampMs(TimestampQueryHandle, double& outMs) override;

		// Stage-1 migration bridges (WrapNative*/CreateInputLayoutNative/
		// GetNativeDevice/Context) exist only to interoperate with legacy
		// raw-D3D11 code and imgui_impl_dx11. DX12 code should never reach
		// them -- they assert if called. ImGuiTextureId IS implemented for
		// real (returns a GPU descriptor handle) even though nothing calls
		// it yet.
		void* GetNativeDevice() override;
		void* GetNativeContext() override;
		void* GetNativeSrv(SrvHandle) override;
		void* GetNativeRtv(RtvHandle) override;
		void* GetNativeTexture(TextureHandle) override;
		void* ImGuiTextureId(SrvHandle) override;
		void* GetNativeCommandQueue() override;
		void* GetNativeCommandList() override;
		void* GetImGuiSrvDescriptorHeap() override;
		void* ImGuiFontSrvCpuHandle() override;
		void* ImGuiFontSrvGpuHandle() override;
		SrvHandle WrapNativeSrv(void*) override;
		RtvHandle WrapNativeRtv(void*) override;
		DsvHandle WrapNativeDsv(void*) override;
		void* CreateInputLayoutNative(const InputElement*, uint32_t, const void*, uint32_t) override;

		// ---- backend-internal accessors used by Dx12CommandContext ----
		ID3D12Device*        Raw() { return myDevice.Get(); }
		ID3D12GraphicsCommandList* RawList() { return myCmdList.Get(); }
		BufferRec*  GetBuffer(BufferHandle h)   { return myBuffers.Get(h); }
		TextureRec* GetTexture(TextureHandle h) { return myTextures.Get(h); }
		ShaderRec*  GetShader(ShaderModuleHandle h) { return myShaders.Get(h); }
		GfxPipelineRec*     GetGfxPipeline(GraphicsPipelineHandle h)  { return myGfxPipelines.Get(h); }
		ComputePipelineRec* GetComputePipeline(ComputePipelineHandle h) { return myComputePipelines.Get(h); }
		D3D12_CPU_DESCRIPTOR_HANDLE RtvCpuHandle(uint32_t slot) { return myRtvHeap.Cpu(slot); }
		D3D12_CPU_DESCRIPTOR_HANDLE DsvCpuHandle(uint32_t slot) { return myDsvHeap.Cpu(slot); }
		D3D12_CPU_DESCRIPTOR_HANDLE CbvSrvUavCpuHandle(uint32_t slot) { return myCbvSrvUavHeap.Cpu(slot); }
		D3D12_CPU_DESCRIPTOR_HANDLE SamplerCpuHandle(uint32_t slot)   { return mySamplerHeap.Cpu(slot); }
		uint32_t* GetSrvSlot(SrvHandle h)         { return mySrvSlots.Get(h); }
		uint32_t* GetUavSlot(UavHandle h)         { return myUavSlots.Get(h); }
		uint32_t* GetRtvSlot(RtvHandle h)          { RtvRec* r = myRtvSlots.Get(h); return r ? &r->slot : nullptr; }
		uint32_t* GetDsvSlot(DsvHandle h)          { DsvRec* r = myDsvSlots.Get(h); return r ? &r->slot : nullptr; }
		Format    GetRtvFormat(RtvHandle h)        { RtvRec* r = myRtvSlots.Get(h); return r ? r->format : Format::Unknown; }
		Format    GetDsvFormat(DsvHandle h)        { DsvRec* r = myDsvSlots.Get(h); return r ? r->format : Format::Unknown; }
		uint32_t* GetSamplerSlot(SamplerHandle h) { return mySamplerSlots.Get(h); }
		uint32_t  FrameIndex() const { return myFrameIndex; }
		// Real, permanently-allocated null SRV/UAV/sampler descriptors --
		// reading an uninitialized descriptor slot (e.g. an rhi Handle the
		// caller never bound) is undefined behavior for the GPU, so every
		// unbound table slot gets filled with one of these instead.
		uint32_t NullSrvSlot() const { return myNullSrvSlot; }
		uint32_t NullUavSlot() const { return myNullUavSlot; }
		uint32_t NullSamplerSlot() const { return myNullSamplerSlot; }

		ID3D12RootSignature* GraphicsRootSignature() { return myGraphicsRootSig.Get(); }
		ID3D12RootSignature* ComputeRootSignature()  { return myComputeRootSig.Get(); }
		// Per-frame shader-visible scratch heaps the command context copies
		// bind-time descriptor tables into (see the class comment + memory
		// for why these are separate from the CreateSrv/CreateUav/CreateSampler
		// storage heaps above). Reset once per frame in BeginFrame.
		Dx12DescriptorHeap& CbvSrvUavScratch() { return myCbvSrvUavScratch[myFrameIndex]; }
		Dx12DescriptorHeap& SamplerScratch()   { return mySamplerScratch[myFrameIndex]; }

		static constexpr uint32_t kNumCbvRegisters = 14;   // b0..b13
		static constexpr uint32_t kNumSrvRegisters = 24;   // t0..t23
		static constexpr uint32_t kNumUavRegisters = 4;    // u0..u3
		static constexpr uint32_t kNumSamplerRegisters = 6; // s0..s5

	private:
		void CreateDeviceAndQueue(bool enableDebugLayer, bool enableGpuValidation);
		void CreateSwapchain(HWND hwnd, uint32_t w, uint32_t h);
		void CreateHeaps();
		void CreateFrameResources();
		void CreateRootSignatures();
		void CreateNullDescriptors();
		void AdoptBackBuffers();
		void WaitForGpuIdle();
		// One-off upload: create a temp upload heap, copy CPU data in, record +
		// execute a copy command list, block until done. Simple and correct;
		// not meant for high-frequency use (mesh import / one-time asset
		// creation only -- per-frame streaming should use AllocateDynamicConstants
		// or a future dedicated upload ring instead).
		void UploadBufferData(ID3D12Resource* dst, const void* data, size_t size);
		void UploadTextureData(ID3D12Resource* dst, const TextureDesc&, const SubresourceData* initial, uint32_t count);

		static constexpr uint32_t kFramesInFlight = 2;
		// Descriptor heap layout: CreateSrv/CreateUav/CreateSampler/CreateRtv/
		// CreateDsv allocate from these NON-shader-visible heaps -- they're
		// permanent, freelist-managed CPU-side storage, never bound directly.
		// Dx12CommandContext copies the CURRENTLY BOUND set of descriptors
		// (per SetShaderResource(s)/SetUnorderedAccess(es)/SetSampler calls)
		// into the shader-visible scratch heaps below immediately before each
		// Draw/Dispatch, because a DX12 descriptor TABLE must be contiguous in
		// the bound heap and the permanent slots for an arbitrary bind set are
		// scattered. Two consequences of that split: (1) CBV/SRV/UAV scratch
		// capacity must cover one table-sized copy per draw call per frame
		// (not reused within a frame -- the GPU may not have executed an
		// earlier draw yet when a later one is being recorded), so it's sized
		// generously (a large heap costs little); (2) the SAMPLER heap has a
		// hard, universal 2048-descriptor limit on all hardware tiers, which
		// a naive "copy every draw" scheme could exceed on a scene with many
		// draws -- Dx12CommandContext only re-copies the sampler table when
		// SetSampler was actually called since the last draw (the engine's
		// own state-caching upstream, e.g. GraphicsStateStack, already avoids
		// redundant SetSampler calls), keeping real usage far under the cap.
		static constexpr uint32_t kCbvSrvUavCapacity = 8192;          // permanent (non-shader-visible)
		static constexpr uint32_t kSamplerCapacity = 256;             // permanent (non-shader-visible)
		static constexpr uint32_t kCbvSrvUavScratchPerFrame = 131072; // shader-visible, per frame-in-flight
		static constexpr uint32_t kSamplerScratchPerFrame = 800;      // shader-visible, per frame-in-flight (<2048 total across both)
		static constexpr uint32_t kRtvCapacity = 256;
		static constexpr uint32_t kDsvCapacity = 64;
		static constexpr uint32_t kDynRingBytes = 4u << 20;   // 4 MB per frame
		static constexpr uint32_t kImGuiSrvCapacity = 64;     // shader-visible, owned exclusively by imgui_impl_dx12

		ComPtr<IDXGIFactory6> myFactory;
		ComPtr<ID3D12Device>  myDevice;
		ComPtr<ID3D12CommandQueue> myQueue;
		ComPtr<IDXGISwapChain3> mySwapChain;

		Dx12DescriptorHeap myRtvHeap;         // non-shader-visible
		Dx12DescriptorHeap myDsvHeap;         // non-shader-visible
		Dx12DescriptorHeap myCbvSrvUavHeap;    // non-shader-visible (permanent storage)
		Dx12DescriptorHeap mySamplerHeap;      // non-shader-visible (permanent storage)
		Dx12DescriptorHeap myCbvSrvUavScratch[kFramesInFlight];   // shader-visible (bind-time tables)
		Dx12DescriptorHeap mySamplerScratch[kFramesInFlight];     // shader-visible (bind-time tables)
		// Small persistent shader-visible heap reserved exclusively for
		// imgui_impl_dx12's own use (its font atlas descriptor, and any future
		// ImGui::Image user textures) -- it needs allocations that survive
		// across frames, unlike the per-frame scratch heaps above which get
		// bulk-reset every BeginFrame. Unused/uninitialized on DX11.
		Dx12DescriptorHeap myImGuiSrvHeap;
		uint32_t myImGuiFontSrvSlot = 0;

		ComPtr<ID3D12RootSignature> myGraphicsRootSig;
		ComPtr<ID3D12RootSignature> myComputeRootSig;
		uint32_t myNullSrvSlot = 0, myNullUavSlot = 0, myNullSamplerSlot = 0;

		ComPtr<ID3D12CommandAllocator> myAllocators[kFramesInFlight];
		ComPtr<ID3D12GraphicsCommandList> myCmdList;
		ComPtr<ID3D12Fence> myFence;
		HANDLE myFenceEvent = nullptr;
		uint64_t myFenceValues[kFramesInFlight] = {};
		uint64_t myNextFenceValue = 1;
		uint32_t myFrameIndex = 0;

		// One-off upload command list, reused synchronously by UploadBufferData/
		// UploadTextureData (separate from the main per-frame list/allocator).
		ComPtr<ID3D12CommandAllocator> myUploadAllocator;
		ComPtr<ID3D12GraphicsCommandList> myUploadCmdList;

		TextureHandle myBackBufferTex[kFramesInFlight];
		RtvHandle     myBackBufferRtv[kFramesInFlight];        // sRGB write view (default DX11::BackBuffer)
		RtvHandle     myBackBufferRtvNoSrgb[kFramesInFlight];  // linear/UNORM write view on the SAME resource (DX11::BackBufferNoSrgbConversion -- ImGui renders here)
		TextureHandle myDepthTex;
		DsvHandle     myDepthDsv;
		Vector2ui     myResolution;
		HWND          myHwnd = nullptr;

		Pool<BufferRec,  BufferHandle>  myBuffers;
		Pool<TextureRec, TextureHandle> myTextures;
		Pool<uint32_t, SrvHandle>     mySrvSlots;
		Pool<uint32_t, UavHandle>     myUavSlots;
		Pool<RtvRec, RtvHandle>       myRtvSlots;
		Pool<DsvRec, DsvHandle>       myDsvSlots;
		Pool<uint32_t, SamplerHandle> mySamplerSlots;
		Pool<ShaderRec, ShaderModuleHandle> myShaders;
		Pool<GfxPipelineRec, GraphicsPipelineHandle> myGfxPipelines;
		Pool<ComputePipelineRec, ComputePipelineHandle> myComputePipelines;
		Pool<TimestampRec, TimestampQueryHandle> myTimestamps;

		// Desc-hash -> pipeline cache, mirroring the DX11 backend's myGfxCache/
		// myComputeCache. CreateGraphicsPipeline/CreateComputePipeline are
		// called every draw/dispatch by design (see DeferredRenderer's compute
		// dispatch sites from Stage 1) -- this makes repeat calls a cheap hash
		// lookup, not a PSO rebuild.
		std::unordered_map<uint64_t, GraphicsPipelineHandle> myGfxCache;
		std::unordered_map<uint64_t, ComputePipelineHandle>  myComputeCache;

		// Per-frame dynamic-constant upload ring (bump-allocated, matches the
		// DX11 backend's design): one persistently-mapped UPLOAD buffer per
		// frame-in-flight, reset (bump cursor to 0) at that frame's BeginFrame.
		BufferHandle myDynRing[kFramesInFlight];
		uint8_t*     myDynRingCpu[kFramesInFlight] = {};
		uint32_t     myDynCursor = 0;

		ComPtr<ID3D12QueryHeap> myTimestampHeap;
		ComPtr<ID3D12Resource>  myTimestampReadback;
		static constexpr uint32_t kMaxTimestamps = 512;
		double myGpuTimestampFrequency = 0.0;

		std::unique_ptr<Dx12CommandContext> myContext;
	};
}
