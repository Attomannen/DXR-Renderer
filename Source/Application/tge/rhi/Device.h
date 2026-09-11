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

		// ---- resources ----
		virtual BufferHandle  CreateBuffer(const BufferDesc&, const void* initialData = nullptr) = 0;
		virtual TextureHandle CreateTexture(const TextureDesc&, const SubresourceData* initial = nullptr,
		                                    uint32_t initialCount = 0) = 0;

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
