#pragma once
#include <memory>
#include <tge/math/Vector.h>
#include "tge/rhi/Descs.h"
#include "tge/rhi/CommandContext.h"

namespace Tga::rhi
{
	enum class Backend : uint8_t { DX11, DX12 };

	class IDevice
	{
	public:
		virtual ~IDevice() = default;

		virtual Backend GetBackend() const = 0;
		// Hardware/API capability, not a user setting.  DX11 always reports
		// false; DX12 reports true only after Device5, CommandList4 and the
		// Tier 1.1 feature query have all succeeded.
		virtual bool SupportsRaytracingTier11() const = 0;
		// Returns an invalid handle on backends without DXR. Creation is intended
		// for immutable model geometry and may synchronously compact at load time.
		virtual RaytracingBlasHandle CreateRaytracingBlas(const RaytracingBlasDesc&) = 0;
		virtual void Destroy(RaytracingBlasHandle) = 0;
		virtual void BuildRaytracingTlas(const RaytracingInstanceDesc*, uint32_t count) = 0;
		virtual uint32_t RegisterRaySceneSrv(SrvHandle) = 0;
		// Must be called after BuildRaytracingTlas and before any RayQuery
		// dispatch. Root descriptors capture GPU virtual addresses at bind time;
		// false means this frame has no valid ray-tracing scene.
		virtual bool BindRaytracingSceneForCompute() = 0;
		// Index of the command-buffer slot currently being recorded.  Resources
		// written by the CPU and consumed by the GPU use this to select storage
		// that cannot still be referenced by an earlier frame.
		virtual uint32_t GetFrameIndex() const = 0;

		// ---- resources ----
		virtual BufferHandle  CreateBuffer(const BufferDesc&, const void* initialData = nullptr) = 0;
		virtual TextureHandle CreateTexture(const TextureDesc&, const SubresourceData* initial = nullptr,
		                                    uint32_t initialCount = 0) = 0;
		// The format a texture was actually created with -- Unknown for an
		// invalid handle. Lets generic code (e.g. CubemapPrefilter's capture
		// path) match a new texture's format to an existing one's without a
		// backend-specific desc query.
		virtual Format GetTextureFormat(TextureHandle) const = 0;

		virtual SrvHandle CreateSrv(TextureHandle, const SrvDesc&) = 0;
		virtual SrvHandle CreateSrv(BufferHandle, const SrvDesc&) = 0;
		virtual UavHandle CreateUav(TextureHandle, const UavDesc&) = 0;
		virtual UavHandle CreateUav(BufferHandle, const UavDesc&) = 0;
		virtual RtvHandle CreateRtv(TextureHandle, const RtvDesc&) = 0;
		virtual DsvHandle CreateDsv(TextureHandle, const DsvDesc&) = 0;
		virtual SamplerHandle CreateSampler(const SamplerDesc&) = 0;

		virtual ShaderModuleHandle CreateShaderModule(ShaderKind, const void* bytecode, size_t size) = 0;

		virtual GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc&) = 0;   // hash-cached
		virtual ComputePipelineHandle  CreateComputePipeline(const ComputePipelineDesc&) = 0;     // hash-cached

		virtual void Destroy(BufferHandle) = 0;
		virtual void Destroy(TextureHandle) = 0;
		virtual void Destroy(SrvHandle) = 0;
		virtual void Destroy(UavHandle) = 0;
		virtual void Destroy(RtvHandle) = 0;
		virtual void Destroy(DsvHandle) = 0;
		virtual void Destroy(SamplerHandle) = 0;
		virtual void Destroy(ShaderModuleHandle) = 0;

		// ---- per-frame dynamic constants (replaces Map(WRITE_DISCARD)) ----
		virtual DynamicAlloc AllocateDynamicConstants(const void* data, uint32_t byteSize) = 0;

		// ---- swapchain / frame ----
		virtual bool          Resize(uint32_t w, uint32_t h) = 0;
		// Requests exclusive fullscreen on the backend's own swapchain.  Kept on
		// IDevice because DX12 deliberately has no legacy DX11::SwapChain pointer.
		virtual bool          SetFullscreen(bool enabled) = 0;
		virtual TextureHandle GetBackBuffer() const = 0;
		virtual RtvHandle     GetBackBufferRtv(bool srgb) const = 0;
		virtual TextureHandle GetDefaultDepth() const = 0;
		virtual DsvHandle     GetDefaultDepthDsv() const = 0;
		virtual Vector2ui     GetResolution() const = 0;

		virtual ICommandContext& BeginFrame() = 0;
		virtual void             EndFrame(bool vsync) = 0;

		// The current frame's command context, without the per-frame reset that
		// BeginFrame() does. Valid between BeginFrame() and EndFrame() (and before
		// the first BeginFrame during engine init).
		virtual ICommandContext& GetContext() = 0;

		// ---- timestamp queries (for GpuProfiler) ----
		virtual TimestampQueryHandle CreateTimestampQuery() = 0;
		virtual void  DestroyTimestampQuery(TimestampQueryHandle) = 0;
		// Non-blocking; results are typically a few frames late. Returns false until ready.
		virtual bool  GetTimestampMs(TimestampQueryHandle, double& outMs) = 0;

		// ---- escape hatch (imgui bridge + editor only; removed in Stage 2) ----
		// GetNativeDevice() is real on both backends (ID3D11Device* / ID3D12Device*).
		// GetNativeContext() is DX11-only -- DX12 has no persistent "device
		// context" equivalent (see GetNativeCommandQueue/CommandList below for
		// what imgui_impl_dx12 uses instead) -- and asserts if called there.
		virtual void* GetNativeDevice() = 0;
		virtual void* GetNativeContext() = 0;
		// Raw pointers behind rhi-created resources/views, for legacy wrapper classes
		// (RenderTarget/DepthBuffer/TextureResource) whose own public API still hands
		// out raw D3D11 pointers to a wide, not-yet-migrated caller base. Caller does
		// not own a ref; take a copy (ComPtr(ptr) AddRefs) if retaining it.
		virtual void* GetNativeSrv(SrvHandle) = 0;
		virtual void* GetNativeRtv(RtvHandle) = 0;
		virtual void* GetNativeTexture(TextureHandle) = 0;

		// ---- ImGui interop: opaque texture id for ImGui::Image ----
		virtual void* ImGuiTextureId(SrvHandle) = 0;

		// ---- ImGui/DX12 interop only: raw D3D12 objects with no DX11 analogue.
		// DX12 has no persistent "device context" the way DX11 does, so
		// GetNativeContext doesn't apply there -- imgui_impl_dx12 instead needs
		// the command queue (once, at Init) and the CURRENT frame's command
		// list (every RenderDrawData call), plus a small persistent
		// shader-visible descriptor heap it owns exclusively (its font atlas
		// needs an allocation that survives across frames, unlike this engine's
		// own per-frame scratch heaps). No-op/unused on DX11 -- ImGuiInterface.cpp
		// branches on GetBackend() and only calls these for DX12. Descriptor
		// handles follow the same "D3D12_C/GPU_DESCRIPTOR_HANDLE.ptr reinterpreted
		// as void*" convention already used by ImGuiTextureId above.
		virtual void* GetNativeCommandQueue() = 0;      // ID3D12CommandQueue*
		virtual void* GetNativeCommandList() = 0;       // ID3D12GraphicsCommandList*
		virtual void* GetImGuiSrvDescriptorHeap() = 0;  // ID3D12DescriptorHeap*
		virtual void* ImGuiFontSrvCpuHandle() = 0;

		// ---- DX12-only debugging aid: saves the LAST FULLY PRESENTED backbuffer
		// (i.e. the previous frame's, not whatever is mid-recording right now) to
		// a PNG. Call at the very start of a frame, before any drawing -- by then
		// EndFrame's fence wait for that frame-in-flight slot has already
		// guaranteed the GPU is done with it. DX11 has its own working
		// SaveWICTextureToFile-based screenshot path in GameWorld.cpp already;
		// this exists only because that path is unusable under DX12 (it reaches
		// into DX11::SwapChain/DX11::Context directly, both null there). No-op
		// (returns false) on DX11.
		virtual bool CaptureBackBufferPng(const wchar_t* utf16Path) = 0;
		virtual void* ImGuiFontSrvGpuHandle() = 0;

		// ---- Synchronously reads back ONE pixel of `texture`, which must be
		// R32G32B32A32_UINT (16 bytes: 4x uint32) -- built for the editor
		// viewport's mouse-picking ID render target (Viewport.cpp's
		// MouseOver()), its only caller; not a general-purpose readback API.
		// Blocks the CPU until the GPU catches up (DX11: a blocking Map, its
		// existing behavior before this moved into the RHI; DX12: a full
		// WaitForGpuIdle -- both backends' already-established synchronous-
		// readback idiom, e.g. CaptureBackBufferPng above, not a new perf
		// class). Returns false (leaving outValues untouched) if `texture` is
		// invalid or (x,y) is out of bounds.
		virtual bool ReadBackUintPixel4(TextureHandle texture, uint32_t x, uint32_t y, uint32_t outValues[4]) = 0;

		// ---- Stage-1 migration bridge: adopt a view created by legacy raw-D3D11
		// code so the wrapper can hand out an rhi handle. Removed in Stage 2 when
		// the wrappers create their resources through the RHI directly.
		virtual SrvHandle WrapNativeSrv(void* nativeSrv) = 0;
		virtual RtvHandle WrapNativeRtv(void* nativeRtv) = 0;
		virtual DsvHandle WrapNativeDsv(void* nativeDsv) = 0;

		// Stage-1 migration bridge: build (or fetch from an internal cache) the
		// native input layout for `elems` reflected against `vsBytecode`, and
		// return it as an ID3D11InputLayout* with one AddRef the caller owns.
		// Removed in Stage 2 (input layout is folded into CreateGraphicsPipeline).
		virtual void* CreateInputLayoutNative(const InputElement* elems, uint32_t count,
		                                      const void* vsBytecode, uint32_t vsSize) = 0;
	};

	// Creates the backend chosen by `desc` semantics + the Backend arg. The DX11
	// backend in Stage 1 wraps the pre-existing Tga::DX11 statics.
	std::unique_ptr<IDevice> CreateDevice(Backend, const DeviceDesc&);
}
