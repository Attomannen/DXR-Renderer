#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <vector>

#include "tge/rhi/Device.h"
#include "tge/rhi/Pool.h"
#include "tge/rhi/dx12/Dx12DescriptorHeap.h"

namespace Tga::rhi::dx12
{
	using Microsoft::WRL::ComPtr;
	class Dx12CommandContext;

	struct BufferRec  { ComPtr<ID3D12Resource> res; BufferDesc desc; D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON; };
	struct TextureRec { ComPtr<ID3D12Resource> res; TextureDesc desc; D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON; };
	struct ShaderRec  { std::vector<uint8_t> bytecode; ShaderKind kind = ShaderKind::Vertex; };
	struct GfxPipelineRec    { ComPtr<ID3D12PipelineState> pso; D3D12_PRIMITIVE_TOPOLOGY topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; };
	struct ComputePipelineRec { ComPtr<ID3D12PipelineState> pso; };
	struct TimestampRec { uint32_t queryIndex = 0; uint64_t frame = ~0ull; bool hasBegin = false, hasEnd = false; };

	// Stage 2, milestone 1: device + swapchain + descriptor heaps + frame
	// pacing + resource creation are real and verified (see
	// p5g3-dx12-port memory for how). The graphics pipeline / root signature
	// / full ICommandContext binding+draw+barrier surface is NOT implemented
	// yet -- SetGraphicsPipeline/SetComputePipeline/Draw*/Dispatch and the
	// bind-by-slot methods on Dx12CommandContext assert. This lets the device
	// itself (the highest-risk, hardest-to-change part: heap layout, frame
	// sync, resource lifetime) be built and tested against real hardware
	// before committing to the command-recording design on top of it.
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
		RtvHandle     GetBackBufferRtv(bool /*srgb*/) const override { return myBackBufferRtv[myFrameIndex]; }
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
		// GetNativeDevice/Context/ImGuiTextureId) exist only to interoperate
		// with legacy raw-D3D11 code and imgui_impl_dx11. DX12 code should
		// never reach them -- they assert if called.
		void* GetNativeDevice() override;
		void* GetNativeContext() override;
		void* GetNativeSrv(SrvHandle) override;
		void* GetNativeRtv(RtvHandle) override;
		void* GetNativeTexture(TextureHandle) override;
		void* ImGuiTextureId(SrvHandle) override;
		SrvHandle WrapNativeSrv(void*) override;
		RtvHandle WrapNativeRtv(void*) override;
		DsvHandle WrapNativeDsv(void*) override;
		void* CreateInputLayoutNative(const InputElement*, uint32_t, const void*, uint32_t) override;

		// ---- backend-internal accessors used by Dx12CommandContext ----
		ID3D12Device*        Raw() { return myDevice.Get(); }
		ID3D12GraphicsCommandList* RawList() { return myCmdList.Get(); }
		BufferRec*  GetBuffer(BufferHandle h)   { return myBuffers.Get(h); }
		TextureRec* GetTexture(TextureHandle h) { return myTextures.Get(h); }
		Dx12DescriptorHeap& CbvSrvUavHeap() { return myCbvSrvUavHeap; }
		Dx12DescriptorHeap& SamplerHeap()   { return mySamplerHeap; }
		D3D12_CPU_DESCRIPTOR_HANDLE RtvCpuHandle(uint32_t slot) { return myRtvHeap.Cpu(slot); }
		D3D12_CPU_DESCRIPTOR_HANDLE DsvCpuHandle(uint32_t slot) { return myDsvHeap.Cpu(slot); }
		uint32_t* GetSrvSlot(SrvHandle h)         { return mySrvSlots.Get(h); }
		uint32_t* GetUavSlot(UavHandle h)         { return myUavSlots.Get(h); }
		uint32_t* GetRtvSlot(RtvHandle h)         { return myRtvSlots.Get(h); }
		uint32_t* GetDsvSlot(DsvHandle h)         { return myDsvSlots.Get(h); }
		uint32_t* GetSamplerSlot(SamplerHandle h) { return mySamplerSlots.Get(h); }

	private:
		void CreateDeviceAndQueue(bool enableDebugLayer, bool enableGpuValidation);
		void CreateSwapchain(HWND hwnd, uint32_t w, uint32_t h);
		void CreateHeaps();
		void CreateFrameResources();
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
		static constexpr uint32_t kCbvSrvUavCapacity = 4096;
		static constexpr uint32_t kSamplerCapacity = 256;
		static constexpr uint32_t kRtvCapacity = 256;
		static constexpr uint32_t kDsvCapacity = 64;
		static constexpr uint32_t kDynRingBytes = 4u << 20;   // 4 MB per frame

		ComPtr<IDXGIFactory6> myFactory;
		ComPtr<ID3D12Device>  myDevice;
		ComPtr<ID3D12CommandQueue> myQueue;
		ComPtr<IDXGISwapChain3> mySwapChain;

		Dx12DescriptorHeap myRtvHeap;         // non-shader-visible
		Dx12DescriptorHeap myDsvHeap;         // non-shader-visible
		Dx12DescriptorHeap myCbvSrvUavHeap;    // shader-visible
		Dx12DescriptorHeap mySamplerHeap;      // shader-visible

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
		RtvHandle     myBackBufferRtv[kFramesInFlight];
		TextureHandle myDepthTex;
		DsvHandle     myDepthDsv;
		Vector2ui     myResolution;
		HWND          myHwnd = nullptr;

		Pool<BufferRec,  BufferHandle>  myBuffers;
		Pool<TextureRec, TextureHandle> myTextures;
		Pool<uint32_t, SrvHandle>     mySrvSlots;
		Pool<uint32_t, UavHandle>     myUavSlots;
		Pool<uint32_t, RtvHandle>     myRtvSlots;
		Pool<uint32_t, DsvHandle>     myDsvSlots;
		Pool<uint32_t, SamplerHandle> mySamplerSlots;
		Pool<ShaderRec, ShaderModuleHandle> myShaders;
		Pool<GfxPipelineRec, GraphicsPipelineHandle> myGfxPipelines;
		Pool<ComputePipelineRec, ComputePipelineHandle> myComputePipelines;
		Pool<TimestampRec, TimestampQueryHandle> myTimestamps;

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
