#pragma once
#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "tge/rhi/Device.h"
#include "tge/rhi/dx11/Dx11Pools.h"

namespace Tga::rhi::dx11
{
	using Microsoft::WRL::ComPtr;
	class Dx11CommandContext;

	struct BufferRec  { ComPtr<ID3D11Buffer> res; BufferDesc desc; };
	struct TextureRec { ComPtr<ID3D11Resource> res; TextureDesc desc; bool ownsResource = true; };
	struct ShaderRec
	{
		ComPtr<ID3D11VertexShader>  vs;
		ComPtr<ID3D11PixelShader>   ps;
		ComPtr<ID3D11ComputeShader> cs;
		std::string bytecode;    // kept for VS input-layout reflection
		ShaderKind  kind = ShaderKind::Vertex;
	};
	struct GfxPipelineRec
	{
		ComPtr<ID3D11InputLayout>  layout;
		ID3D11VertexShader*        vs = nullptr;
		ID3D11PixelShader*         ps = nullptr;
		ID3D11BlendState*          blend = nullptr;
		ID3D11DepthStencilState*   depth = nullptr;
		ID3D11RasterizerState*     raster = nullptr;
		D3D11_PRIMITIVE_TOPOLOGY   topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		bool                       alphaToCoverage = false;
	};
	struct ComputePipelineRec { ID3D11ComputeShader* cs = nullptr; };
	struct TimestampRec { ComPtr<ID3D11Query> begin; ComPtr<ID3D11Query> end; uint64_t frame = ~0ull; };

	// The DX11 backend. In Stage 1 it wraps the already-created Tga::DX11 statics
	// for device/context/swapchain/backbuffer; resources it creates are its own.
	class Dx11Device final : public IDevice
	{
	public:
		explicit Dx11Device(const DeviceDesc&);
		~Dx11Device() override;

		Backend GetBackend() const override { return Backend::DX11; }
		bool SupportsRaytracingTier11() const override { return false; }
		RaytracingBlasHandle CreateRaytracingBlas(const RaytracingBlasDesc&) override { return {}; }
		void Destroy(RaytracingBlasHandle) override {}
		void BuildRaytracingTlas(const RaytracingInstanceDesc*, uint32_t) override {}
		uint32_t RegisterRaySceneSrv(SrvHandle) override { return 0; }
		bool BindRaytracingSceneForCompute() override { return false; }
		uint32_t GetFrameIndex() const override { return 0; }

		BufferHandle  CreateBuffer(const BufferDesc&, const void* initialData) override;
		TextureHandle CreateTexture(const TextureDesc&, const SubresourceData* initial, uint32_t initialCount) override;
		Format GetTextureFormat(TextureHandle) const override;

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
		bool          SetFullscreen(bool enabled) override;
		TextureHandle GetBackBuffer() const override { return myBackBufferTex; }
		RtvHandle     GetBackBufferRtv(bool srgb) const override { return srgb ? myBackBufferRtvSrgb : myBackBufferRtvLinear; }
		TextureHandle GetDefaultDepth() const override { return myDepthTex; }
		DsvHandle     GetDefaultDepthDsv() const override { return myDepthDsv; }
		Vector2ui     GetResolution() const override;

		ICommandContext& BeginFrame() override;
		ICommandContext& GetContext() override;
		void             EndFrame(bool vsync) override;

		TimestampQueryHandle CreateTimestampQuery() override;
		void  DestroyTimestampQuery(TimestampQueryHandle) override;
		bool  GetTimestampMs(TimestampQueryHandle, double& outMs) override;

		void* GetNativeDevice() override;
		void* GetNativeContext() override;
		void* GetNativeSrv(SrvHandle) override;
		void* GetNativeRtv(RtvHandle) override;
		void* GetNativeTexture(TextureHandle) override;
		void* ImGuiTextureId(SrvHandle) override;
		// DX12/imgui_impl_dx12-only; unused on this backend (imgui_impl_dx11
		// only ever asks for GetNativeDevice/GetNativeContext above).
		void* GetNativeCommandQueue() override { return nullptr; }
		void* GetNativeCommandList() override { return nullptr; }
		void* GetImGuiSrvDescriptorHeap() override { return nullptr; }
		void* ImGuiFontSrvCpuHandle() override { return nullptr; }
		bool CaptureBackBufferPng(const wchar_t*) override { return false; }   // DX11 has its own path, see Device.h's comment
		void* ImGuiFontSrvGpuHandle() override { return nullptr; }
		bool ReadBackUintPixel4(TextureHandle texture, uint32_t x, uint32_t y, uint32_t outValues[4]) override;

		SrvHandle WrapNativeSrv(void* nativeSrv) override;
		RtvHandle WrapNativeRtv(void* nativeRtv) override;
		DsvHandle WrapNativeDsv(void* nativeDsv) override;
		void* CreateInputLayoutNative(const InputElement* elems, uint32_t count,
		                              const void* vsBytecode, uint32_t vsSize) override;

		// ---- backend-internal accessors used by Dx11CommandContext ----
		ID3D11Device*        Raw() { return myDevice; }
		ID3D11DeviceContext* RawCtx() { return myCtx; }
		BufferRec*         GetBuffer(BufferHandle h)   { return myBuffers.Get(h); }
		TextureRec*        GetTexture(TextureHandle h) { return myTextures.Get(h); }
		ID3D11ShaderResourceView*  GetSrvPtr(SrvHandle h) { auto* p = mySrvs.Get(h); return p ? p->Get() : nullptr; }
		ID3D11UnorderedAccessView* GetUavPtr(UavHandle h) { auto* p = myUavs.Get(h); return p ? p->Get() : nullptr; }
		ID3D11RenderTargetView*    GetRtvPtr(RtvHandle h) { auto* p = myRtvs.Get(h); return p ? p->Get() : nullptr; }
		ID3D11DepthStencilView*    GetDsvPtr(DsvHandle h) { auto* p = myDsvs.Get(h); return p ? p->Get() : nullptr; }
		ID3D11SamplerState*        GetSamplerPtr(SamplerHandle h) { auto* p = mySamplers.Get(h); return p ? p->Get() : nullptr; }
		ShaderRec*         GetShader(ShaderModuleHandle h) { return myShaders.Get(h); }
		GfxPipelineRec*    GetGfxPipeline(GraphicsPipelineHandle h) { return myGfxPipelines.Get(h); }
		ComputePipelineRec* GetComputePipeline(ComputePipelineHandle h) { return myComputePipelines.Get(h); }
		// state-object accessors (lazily build the shared set); used by Dx11CommandContext
		ID3D11BlendState*        BlendFor(BlendMode);
		ID3D11DepthStencilState* DepthFor(DepthMode);
		ID3D11RasterizerState*   RasterFor(RasterMode);
		D3D11_PRIMITIVE_TOPOLOGY TopoFor(Topology);

	private:
		void AdoptSwapchainResources();      // wrap DX11::BackBuffer / DepthBuffer as handles

		ID3D11Device*        myDevice = nullptr;   // borrowed from Tga::DX11
		ID3D11DeviceContext* myCtx = nullptr;
		ComPtr<ID3D11DeviceContext1> myCtx1;      // for *SetConstantBuffers1 (offset binds)

	public:
		ID3D11DeviceContext1* RawCtx1() { return myCtx1.Get(); }
		TimestampRec* GetTimestampRec(TimestampQueryHandle h) { return myTimestamps.Get(h); }
		uint64_t      FrameCounter() const { return myFrameCounter; }
	private:

		Pool<BufferRec,  BufferHandle>          myBuffers;
		Pool<TextureRec, TextureHandle>         myTextures;
		Pool<ComPtr<ID3D11ShaderResourceView>,  SrvHandle>  mySrvs;
		Pool<ComPtr<ID3D11UnorderedAccessView>, UavHandle>  myUavs;
		Pool<ComPtr<ID3D11RenderTargetView>,    RtvHandle>  myRtvs;
		Pool<ComPtr<ID3D11DepthStencilView>,    DsvHandle>  myDsvs;
		Pool<ComPtr<ID3D11SamplerState>,        SamplerHandle> mySamplers;
		Pool<ShaderRec,          ShaderModuleHandle>     myShaders;
		Pool<GfxPipelineRec,     GraphicsPipelineHandle> myGfxPipelines;
		Pool<ComputePipelineRec, ComputePipelineHandle>  myComputePipelines;
		Pool<TimestampRec,       TimestampQueryHandle>   myTimestamps;

		// GPU timestamp support: one disjoint query per in-flight frame; each
		// TimestampRec holds a begin/end pair stamped with the frame it was written.
		static constexpr uint32_t kTsFrames = 8;
		ComPtr<ID3D11Query> myDisjoint[kTsFrames];
		uint64_t myFrameCounter = 0;
		uint32_t myCurTsSlot = 0;
		bool     myTsSlotOpen = false;

		std::unordered_map<uint64_t, GraphicsPipelineHandle> myGfxCache;
		std::unordered_map<uint64_t, ComputePipelineHandle>  myComputeCache;
		std::unordered_map<uint64_t, ComPtr<ID3D11InputLayout>> myInputLayoutCache; // Stage-1 bridge

		void BuildInputElements(const InputElement* elems, uint32_t count,
		                        std::vector<D3D11_INPUT_ELEMENT_DESC>& out);

		ComPtr<ID3D11BlendState>        myBlendStates[3];
		ComPtr<ID3D11DepthStencilState> myDepthStates[4];
		ComPtr<ID3D11RasterizerState>   myRasterStates[5];   // [BackfaceCulling] stays null (D3D11 default)
		bool myStatesBuilt = false;
		void BuildStates();

		// per-frame dynamic-constant ring
		// Bump-allocated upload ring: a few large DYNAMIC constant buffers,
		// sub-allocated with 256-byte-aligned offsets and bound via
		// *SetConstantBuffers1. Reset each BeginFrame.
		static constexpr uint32_t kDynBufferBytes = 1u << 20;   // 1 MB per ring buffer
		std::vector<BufferHandle> myDynRing;
		uint32_t myDynBufferIndex = 0;
		uint32_t myDynByteCursor  = 0;

		TextureHandle myBackBufferTex;
		RtvHandle     myBackBufferRtvSrgb;
		RtvHandle     myBackBufferRtvLinear;
		TextureHandle myDepthTex;
		DsvHandle     myDepthDsv;

		std::unique_ptr<Dx11CommandContext> myContext;
	};
}
