#include "stdafx.h"
#include <tge/debugging/CpuProfiler.h>
#include "tge/rhi/dx12/Dx12Device.h"
#include "tge/rhi/dx12/Dx12CommandContext.h"
#include "tge/rhi/Format.h"
#include <tge/log/Log.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>
#include <cassert>
#include <cstdio>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

namespace Tga::rhi::dx12
{
	// ------------------------------------------------------------------ hashing (mirrors Dx11Device's HashDesc)
	static void HashCombine(uint64_t& h, uint64_t v)
	{
		h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
	}
	static uint64_t HashGraphicsDesc(const GraphicsPipelineDesc& d)
	{
		uint64_t h = 1469598103934665603ull;
		HashCombine(h, ((uint64_t)d.vs.index << 32) | d.vs.generation);
		HashCombine(h, ((uint64_t)d.ps.index << 32) | d.ps.generation);
		HashCombine(h, (uint64_t)d.blend | ((uint64_t)d.depth << 8) | ((uint64_t)d.raster << 16)
		             | ((uint64_t)d.topology << 24) | ((uint64_t)d.alphaToCoverage << 32));
		HashCombine(h, d.renderTargetCount);
		for (uint32_t i = 0; i < 8; ++i) HashCombine(h, (uint64_t)d.rtvFormats[i]);
		HashCombine(h, (uint64_t)d.dsvFormat);
		HashCombine(h, d.inputLayoutCount);
		for (uint32_t i = 0; i < d.inputLayoutCount; ++i)
		{
			const InputElement& e = d.inputLayout[i];
			uint64_t eh = 1469598103934665603ull;
			for (const char* p = e.semanticName; p && *p; ++p) HashCombine(eh, (uint8_t)*p);
			HashCombine(eh, e.semanticIndex);
			HashCombine(eh, (uint64_t)e.format);
			HashCombine(eh, e.inputSlot);
			HashCombine(eh, e.alignedByteOffset);
			HashCombine(eh, (uint64_t)e.perInstance | ((uint64_t)e.instanceStepRate << 1));
			HashCombine(h, eh);
		}
		return h;
	}

	// ------------------------------------------------------------------ fixed-function state translation
	static D3D12_BLEND_DESC BlendFor(BlendMode m)
	{
		D3D12_BLEND_DESC bd = {};
		D3D12_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
		rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		rt.LogicOpEnable = FALSE;
		rt.LogicOp = D3D12_LOGIC_OP_NOOP;
		if (m == BlendMode::Disabled)
		{
			rt.BlendEnable = FALSE;
			rt.SrcBlend = D3D12_BLEND_ONE; rt.DestBlend = D3D12_BLEND_ZERO; rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE; rt.DestBlendAlpha = D3D12_BLEND_ZERO; rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		}
		else
		{
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			rt.DestBlend = (m == BlendMode::AdditiveBlend) ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA;
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE; rt.DestBlendAlpha = D3D12_BLEND_ONE; rt.BlendOpAlpha = D3D12_BLEND_OP_MAX;
		}
		return bd;
	}
	static D3D12_DEPTH_STENCIL_DESC DepthFor(DepthMode m)
	{
		D3D12_DEPTH_STENCIL_DESC dd = {};
		dd.DepthEnable = TRUE;
		dd.StencilEnable = FALSE;
		switch (m)
		{
		case DepthMode::WriteLess:           dd.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;  dd.DepthFunc = D3D12_COMPARISON_FUNC_LESS; break;
		case DepthMode::WriteLessOrEqual:    dd.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;  dd.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; break;
		case DepthMode::ReadOnlyLess:        dd.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; dd.DepthFunc = D3D12_COMPARISON_FUNC_LESS; break;
		case DepthMode::ReadOnlyLessOrEqual: dd.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; dd.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; break;
		}
		return dd;
	}
	static D3D12_RASTERIZER_DESC RasterFor(RasterMode m)
	{
		D3D12_RASTERIZER_DESC rd = {};
		rd.FrontCounterClockwise = FALSE;
		rd.DepthClipEnable = TRUE;
		rd.FillMode = D3D12_FILL_MODE_SOLID;
		rd.CullMode = D3D12_CULL_MODE_BACK;
		rd.MultisampleEnable = FALSE;
		switch (m)
		{
		case RasterMode::BackfaceCulling:     break;   // matches D3D11's implicit default state
		case RasterMode::FrontFaceCulling:    rd.CullMode = D3D12_CULL_MODE_FRONT; rd.MultisampleEnable = TRUE; break;
		case RasterMode::NoFaceCulling:       rd.CullMode = D3D12_CULL_MODE_NONE;  rd.MultisampleEnable = TRUE; break;
		case RasterMode::Wireframe:           rd.FillMode = D3D12_FILL_MODE_WIREFRAME; rd.MultisampleEnable = TRUE; break;
		case RasterMode::WireframeNoCulling:  rd.FillMode = D3D12_FILL_MODE_WIREFRAME; rd.CullMode = D3D12_CULL_MODE_NONE; rd.MultisampleEnable = TRUE; break;
		}
		return rd;
	}
	static D3D12_PRIMITIVE_TOPOLOGY_TYPE TopoTypeFor(Topology t)
	{
		switch (t)
		{
		case Topology::TriangleList: case Topology::TriangleStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		case Topology::LineList: case Topology::LineStrip:         return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case Topology::PointList:                                  return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		}
		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	}
	D3D_PRIMITIVE_TOPOLOGY ToD3D12Topology(Topology t)
	{
		switch (t)
		{
		case Topology::TriangleList:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		case Topology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		case Topology::LineList:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		case Topology::LineStrip:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
		case Topology::PointList:     return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		}
		return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}

	// ------------------------------------------------------------------ ctor/dtor
	Dx12Device::Dx12Device(const DeviceDesc& d)
	{
		myHwnd = static_cast<HWND>(d.nativeWindowHandle);
		myResolution = { d.width, d.height };

		CreateDeviceAndQueue(d.enableDebugLayer, d.enableGpuValidation);
		{ TGA_CPU_SCOPE("Descriptor heaps"); CreateHeaps(); CreateNullDescriptors(); }
		{ TGA_CPU_SCOPE("Frame resources"); CreateFrameResources(); }
		{ TGA_CPU_SCOPE("Root signatures"); CreateRootSignatures(); }
		if (myHwnd) { TGA_CPU_SCOPE("Swap chain"); CreateSwapchain(myHwnd, d.width, d.height); }

		myContext = std::make_unique<Dx12CommandContext>(*this);

		// myCmdList was left CLOSED, with no root signature bound, by
		// CreateFrameResources() -- fine for a list that's only ever touched
		// between a real BeginFrame()/EndFrame() pair, but engine init code
		// legitimately records commands via GetContext() *before* the game
		// loop's first real BeginFrame() ever runs too (e.g. TextService::
		// Init()'s font-atlas UpdateTexture+GenerateMips -- see
		// IDevice::GetContext()'s own doc comment: "valid... before the
		// first BeginFrame during engine init"). Reopen it and bind the root
		// signatures right now so that's actually true, rather than a closed
		// list with nothing bound. BeginFrame()'s first-ever call skips
		// re-Reset()'ing this same list (see myFirstFrame) -- whatever got
		// recorded during init is submitted together with frame 1's own work.
		myAllocators[0]->Reset();
		myCmdList->Reset(myAllocators[0].Get(), nullptr);
		ID3D12DescriptorHeap* initHeaps[] = { myCbvSrvUavScratch[0].Heap(), mySamplerScratch[0].Heap() };
		myCmdList->SetDescriptorHeaps(2, initHeaps);
		myContext->OnBeginFrame();

		INFO_PRINT("Dx12Device: created (%ux%u, %u frames in flight)", d.width, d.height, kFramesInFlight);
	}

	Dx12Device::~Dx12Device()
	{
		// The dynamic-constant ring buffers are UPLOAD-heap and persistently
		// mapped (myDynRingCpu); Pool<BufferRec>'s teardown releases the
		// ComPtr, which implicitly unmaps -- nothing to do here explicitly.
		if (myDevice) WaitForGpuIdle();
		if (myFenceEvent) CloseHandle(myFenceEvent);
	}

	void Dx12Device::DrainDebugMessages(const char* tag)
	{
		ComPtr<ID3D12InfoQueue> infoQueue;
		if (FAILED(myDevice.As(&infoQueue)) || !infoQueue) return;
		UINT64 n = infoQueue->GetNumStoredMessages();
		for (UINT64 i = 0; i < n; ++i)
		{
			SIZE_T len = 0;
			infoQueue->GetMessage(i, nullptr, &len);
			if (len == 0) continue;
			std::vector<uint8_t> buf(len);
			D3D12_MESSAGE* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
			infoQueue->GetMessage(i, msg, &len);
			ERROR_PRINT("D3D12 debug [%s]: %s", tag, msg->pDescription);
		}
		infoQueue->ClearStoredMessages();
	}

	// ------------------------------------------------------------------ device / queue
	void Dx12Device::CreateDeviceAndQueue(bool enableDebugLayer, bool enableGpuValidation)
	{
		UINT dxgiFlags = 0;
#if defined(_DEBUG)
		// Configure DRED before the device exists. Keep it independent of the
		// optional debug-layer/GPU-validation switches: GPU validation is too
		// expensive for normal interactive Debug runs, while DRED is the useful
		// post-mortem evidence when DXR removes a device.
		ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(dredSettings.GetAddressOf()))))
		{
			dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			INFO_PRINT("Dx12Device: DRED breadcrumbs and page-fault diagnostics enabled");
		}

		if (enableDebugLayer)
		{
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf()))))
			{
				debugController->EnableDebugLayer();
				dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;

				if (enableGpuValidation)
				{
					ComPtr<ID3D12Debug1> debug1;
					if (SUCCEEDED(debugController.As(&debug1)))
						debug1->SetEnableGPUBasedValidation(TRUE);
				}
			}
		}
#endif
		// These switches are intentionally inactive outside Debug builds.
		(void)enableDebugLayer;
		(void)enableGpuValidation;
		TGA_CPU_SCOPE("Factory + adapter + D3D12CreateDevice");
		HRESULT hr = CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(myFactory.GetAddressOf()));
		assert(SUCCEEDED(hr)); (void)hr;

		ComPtr<IDXGIAdapter1> adapter;
		for (UINT i = 0; myFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(myDevice.GetAddressOf()))))
			{
				INFO_PRINT("Dx12Device: using adapter %ls", desc.Description);
				adapter.As(&myAdapter);
				break;
			}
		}
		assert(myDevice && "Dx12Device: no D3D12-capable adapter found");

		// RayQuery needs DXR 1.1.  Device5 exposes acceleration-structure
		// creation/build APIs; OPTIONS5 is the authoritative hardware feature
		// check (adapter branding is never a capability check).
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
		if (SUCCEEDED(myDevice.As(&myDevice5)) &&
			SUCCEEDED(myDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))) &&
			options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1)
		{
			myRaytracingTier11 = true;
			INFO_PRINT("Dx12Device: DXR Tier 1.1 available");
		}
		else
		{
			myDevice5.Reset();
			INFO_PRINT("Dx12Device: DXR Tier 1.1 unavailable; SSGI fallback remains active");
		}

#if defined(_DEBUG)
		if (enableDebugLayer)
		{
			// The debug layer's ID3D12InfoQueue defaults to calling DebugBreak()
			// on CORRUPTION/ERROR-severity messages. With no debugger attached
			// (the normal case for this engine outside an IDE), DebugBreak()
			// raises an exception nothing catches -- the whole process dies,
			// often several frames after whatever actually triggered the
			// message, with no visible error (found 2026-09-11: the DX12
			// backend rendered one real frame of the Sponza scene correctly,
			// then silently crashed with exit code 0x87D, a `DebugBreak`-raised
			// exception code, immediately after). Disable break-on-severity so
			// these messages only ever reach the log (via DrainDebugMessages or
			// the OutputDebugString the layer always also emits) instead of
			// killing the app.
			ComPtr<ID3D12InfoQueue> infoQueue;
			if (SUCCEEDED(myDevice.As(&infoQueue)))
			{
				infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
				infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
				infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
				infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, FALSE);
				infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_MESSAGE, FALSE);
			}
			// DrainDebugMessages only runs at EndFrame, so a message raised
			// immediately before a mid-frame crash never reaches the log. Print
			// errors the moment the layer raises them when the runtime supports it.
			ComPtr<ID3D12InfoQueue1> infoQueue1;
			if (SUCCEEDED(myDevice.As(&infoQueue1)))
			{
				DWORD cookie = 0;
				infoQueue1->RegisterMessageCallback(
					[](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID, LPCSTR description, void*)
					{
						if (severity <= D3D12_MESSAGE_SEVERITY_ERROR)
							ERROR_PRINT("D3D12 debug [immediate]: %s", description);
					},
					D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie);
			}

			// DXGI_CREATE_FACTORY_DEBUG (set above) turns on DXGI's OWN separate
			// debug layer (dxgidebug.dll) with its OWN independent break-on-
			// severity settings -- untouched by anything above, which only
			// covers D3D12's ID3D12InfoQueue. Disable there too.
			HMODULE dxgiDebugModule = GetModuleHandleW(L"dxgidebug.dll");
			if (dxgiDebugModule)
			{
				using PFN_DXGIGetDebugInterface1 = HRESULT(WINAPI*)(UINT, REFIID, void**);
				auto pGetDebugInterface1 = reinterpret_cast<PFN_DXGIGetDebugInterface1>(
					GetProcAddress(dxgiDebugModule, "DXGIGetDebugInterface1"));
				ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
				if (pGetDebugInterface1 && SUCCEEDED(pGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.GetAddressOf()))))
				{
					dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, FALSE);
					dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, FALSE);
					dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING, FALSE);
				}
			}
		}
#endif

		D3D12_COMMAND_QUEUE_DESC qd = {};
		qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		hr = myDevice->CreateCommandQueue(&qd, IID_PPV_ARGS(myQueue.GetAddressOf()));
		assert(SUCCEEDED(hr));

		hr = myDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(myFence.GetAddressOf()));
		assert(SUCCEEDED(hr));
		myFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		assert(myFenceEvent);

		UINT64 freq = 0;
		if (SUCCEEDED(myQueue->GetTimestampFrequency(&freq)))
			myGpuTimestampFrequency = (double)freq;
	}

	void Dx12Device::CreateHeaps()
	{
		myRtvHeap.Init(myDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRtvCapacity, false);
		myDsvHeap.Init(myDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kDsvCapacity, false);
		// Permanent storage (CreateSrv/CreateUav/CreateSampler destinations) --
		// non-shader-visible; see Dx12Device.h's class comment for why bind-
		// time descriptor tables live in the separate scratch heaps instead.
		myCbvSrvUavHeap.Init(myDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kCbvSrvUavCapacity, false);
		mySamplerHeap.Init(myDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerCapacity, false);
		for (uint32_t i = 0; i < kFramesInFlight; ++i)
		{
			myCbvSrvUavScratch[i].Init(myDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kCbvSrvUavHeapPerFrame, true);
			myCbvSrvUavScratch[i].ReservePersistentPrefix(kRaySceneDescriptorCapacity);
			mySamplerScratch[i].Init(myDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerScratchPerFrame, true);
		}

		// Small persistent shader-visible heap owned exclusively by
		// imgui_impl_dx12 (see the member comment) -- separate from the two
		// scratch heaps above, which get bulk-reset every BeginFrame and so
		// can't hold anything that must survive across frames (the font atlas).
		myImGuiSrvHeap.Init(myDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kImGuiSrvCapacity, true);
		myImGuiFontSrvSlot = myImGuiSrvHeap.Allocate();

		D3D12_QUERY_HEAP_DESC qhd = {};
		qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		qhd.Count = kMaxTimestamps;
		myDevice->CreateQueryHeap(&qhd, IID_PPV_ARGS(myTimestampHeap.GetAddressOf()));

		D3D12_HEAP_PROPERTIES readbackHeap = { D3D12_HEAP_TYPE_READBACK };
		D3D12_RESOURCE_DESC readbackDesc = {};
		readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		readbackDesc.Width = kMaxTimestamps * sizeof(uint64_t);
		readbackDesc.Height = 1; readbackDesc.DepthOrArraySize = 1; readbackDesc.MipLevels = 1;
		readbackDesc.SampleDesc.Count = 1;
		readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		myDevice->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(myTimestampReadback.GetAddressOf()));
	}

	void Dx12Device::CreateNullDescriptors()
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
		nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		nullSrv.Texture2D.MipLevels = 1;
		myNullSrvSlot = myCbvSrvUavHeap.Allocate();
		myDevice->CreateShaderResourceView(nullptr, &nullSrv, myCbvSrvUavHeap.Cpu(myNullSrvSlot));

		D3D12_UNORDERED_ACCESS_VIEW_DESC nullUav = {};
		nullUav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		nullUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		myNullUavSlot = myCbvSrvUavHeap.Allocate();
		myDevice->CreateUnorderedAccessView(nullptr, nullptr, &nullUav, myCbvSrvUavHeap.Cpu(myNullUavSlot));

		D3D12_SAMPLER_DESC nullSampler = {};
		nullSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		nullSampler.AddressU = nullSampler.AddressV = nullSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		nullSampler.MaxLOD = D3D12_FLOAT32_MAX;
		myNullSamplerSlot = mySamplerHeap.Allocate();
		myDevice->CreateSampler(&nullSampler, mySamplerHeap.Cpu(myNullSamplerSlot));
	}

	void Dx12Device::CreateFrameResources()
	{
		for (uint32_t i = 0; i < kFramesInFlight; ++i)
		{
			HRESULT hr = myDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(myAllocators[i].GetAddressOf()));
			assert(SUCCEEDED(hr)); (void)hr;
		}
		HRESULT hr = myDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, myAllocators[0].Get(), nullptr, IID_PPV_ARGS(myCmdList.GetAddressOf()));
		assert(SUCCEEDED(hr));
		if (myRaytracingTier11 && FAILED(myCmdList.As(&myCmdList4)))
		{
			// Do not advertise a partially usable DXR implementation.  This can
			// occur with mismatched runtime components even if OPTIONS5 succeeded.
			myRaytracingTier11 = false;
			myDevice5.Reset();
			ERROR_PRINT("Dx12Device: DXR disabled because ID3D12GraphicsCommandList4 is unavailable");
		}
		myCmdList->Close();   // BeginFrame resets it before first use

		hr = myDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(myUploadAllocator.GetAddressOf()));
		assert(SUCCEEDED(hr));
		hr = myDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, myUploadAllocator.Get(), nullptr, IID_PPV_ARGS(myUploadCmdList.GetAddressOf()));
		assert(SUCCEEDED(hr));
		myUploadCmdList->Close();

		// Per-frame dynamic-constant upload ring: one persistently-mapped
		// UPLOAD buffer per frame-in-flight, matching the DX11 backend.
		for (uint32_t i = 0; i < kFramesInFlight; ++i)
		{
			BufferDesc bd = {};
			bd.byteSize = kDynRingBytes;
			bd.usage = BufferUsage::Constant;
			bd.memory = MemoryType::Upload;
			bd.debugName = "Dx12 dynamic-constant ring";
			myDynRing[i] = CreateBuffer(bd, nullptr);
			BufferRec* rec = myBuffers.Get(myDynRing[i]);
			assert(rec && rec->res);
			D3D12_RANGE noRead{ 0, 0 };
			rec->res->Map(0, &noRead, reinterpret_cast<void**>(&myDynRingCpu[i]));
			
			D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
			D3D12_RESOURCE_DESC ud = {};
			ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			ud.Width = kUploadRingBytes; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
			ud.SampleDesc.Count = 1;
			ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			myDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(myUploadRing[i].GetAddressOf()));
			myUploadRing[i]->Map(0, &noRead, reinterpret_cast<void**>(&myUploadRingCpu[i]));
		}
	}

	// One root signature covers every graphics PSO, one covers every compute
	// PSO -- mirrors the engine's existing free register convention (b0..b13,
	// t0..t23, s0..s5, u0..u3, see EngineAssets/Shaders/*.hlsl*) instead of
	// per-shader binding layouts, so HLSL register() declarations don't need
	// to change at all. Root CBVs (not a descriptor table) for b0..b13: every
	// constant buffer in this engine is either the per-frame dynamic-constant
	// ring (AllocateDynamicConstants, already an UPLOAD-heap GPU address) or a
	// persistent rhi::ConstantBuffer (also UPLOAD-heap) -- both have a GPU
	// virtual address ready to bind directly via SetGraphicsRootConstantBufferView,
	// no descriptor/table indirection needed. SRV/UAV/Sampler are descriptor
	// tables since there are many distinct textures/buffers/samplers, unlike
	// the small fixed set of cbuffer slots.
	void Dx12Device::CreateRootSignatures()
	{
		D3D12_DESCRIPTOR_RANGE srvRange = {};
		srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.NumDescriptors = kNumSrvRegisters;
		srvRange.BaseShaderRegister = 0;
		srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_DESCRIPTOR_RANGE uavRange = {};
		uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		uavRange.NumDescriptors = kNumUavRegisters;
		uavRange.BaseShaderRegister = 0;
		uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_DESCRIPTOR_RANGE samplerRange = {};
		samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		samplerRange.NumDescriptors = kNumSamplerRegisters;
		samplerRange.BaseShaderRegister = 0;
		samplerRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_DESCRIPTOR_RANGE rayGeometryRange = {};
		rayGeometryRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		rayGeometryRange.NumDescriptors = kRaySceneDescriptorCapacity;
		rayGeometryRange.BaseShaderRegister = 0;
		rayGeometryRange.RegisterSpace = 1;
		rayGeometryRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		// Same physical range as rayGeometryRange, re-declared under space4 so
		// a shader can read the identical bindless slots as Texture2D instead
		// of ByteAddressBuffer -- see kRaySceneTexRootParameter's comment.
		D3D12_DESCRIPTOR_RANGE rayGeometryTexRange = rayGeometryRange;
		rayGeometryTexRange.RegisterSpace = 4;

		auto buildRootCbvParams = [](std::vector<D3D12_ROOT_PARAMETER>& params)
		{
			for (uint32_t b = 0; b < kNumCbvRegisters; ++b)
			{
				D3D12_ROOT_PARAMETER p = {};
				p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
				p.Descriptor.ShaderRegister = b;
				p.Descriptor.RegisterSpace = 0;
				p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
				params.push_back(p);
			}
		};

		auto serializeAndCreate = [&](const D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>& out, const char* debugName)
		{
			ComPtr<ID3DBlob> blob, error;
			HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, blob.GetAddressOf(), error.GetAddressOf());
			if (FAILED(hr))
			{
				ERROR_PRINT("Dx12Device::CreateRootSignatures: %s failed to serialize: %s", debugName,
					error ? (const char*)error->GetBufferPointer() : "(no error blob)");
				assert(false);
				return;
			}
			hr = myDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(out.GetAddressOf()));
			assert(SUCCEEDED(hr)); (void)hr;
		};

		// ---- graphics: 14 root CBVs + SRV table + Sampler table ----
		{
			std::vector<D3D12_ROOT_PARAMETER> params;
			buildRootCbvParams(params);

			D3D12_ROOT_PARAMETER srvTable = {};
			srvTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			srvTable.DescriptorTable.NumDescriptorRanges = 1;
			srvTable.DescriptorTable.pDescriptorRanges = &srvRange;
			srvTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			params.push_back(srvTable);

			D3D12_ROOT_PARAMETER samplerTable = {};
			samplerTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			samplerTable.DescriptorTable.NumDescriptorRanges = 1;
			samplerTable.DescriptorTable.pDescriptorRanges = &samplerRange;
			samplerTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			params.push_back(samplerTable);

			D3D12_ROOT_SIGNATURE_DESC desc = {};
			desc.NumParameters = (UINT)params.size();
			desc.pParameters = params.data();
			desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
			serializeAndCreate(desc, myGraphicsRootSig, "graphics root signature");
		}

		// ---- compute: 14 root CBVs + SRV table + UAV table + Sampler table ----
		{
			std::vector<D3D12_ROOT_PARAMETER> params;
			buildRootCbvParams(params);

			D3D12_ROOT_PARAMETER srvTable = {};
			srvTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			srvTable.DescriptorTable.NumDescriptorRanges = 1;
			srvTable.DescriptorTable.pDescriptorRanges = &srvRange;
			srvTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			params.push_back(srvTable);

			D3D12_ROOT_PARAMETER uavTable = {};
			uavTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			uavTable.DescriptorTable.NumDescriptorRanges = 1;
			uavTable.DescriptorTable.pDescriptorRanges = &uavRange;
			uavTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			params.push_back(uavTable);

			D3D12_ROOT_PARAMETER samplerTable = {};
			samplerTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			samplerTable.DescriptorTable.NumDescriptorRanges = 1;
			samplerTable.DescriptorTable.pDescriptorRanges = &samplerRange;
			samplerTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			params.push_back(samplerTable);

			// DXR scene geometry is deliberately isolated in register space 1.
			// This table is only present on compute: regular graphics shaders retain
			// their established space-0 root layout unchanged.
			D3D12_ROOT_PARAMETER rayGeometryTable = {};
			rayGeometryTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rayGeometryTable.DescriptorTable.NumDescriptorRanges = 1;
			rayGeometryTable.DescriptorTable.pDescriptorRanges = &rayGeometryRange;
			rayGeometryTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			params.push_back(rayGeometryTable);

			// space2: the two fixed per-frame DXR records (TLAS + its
			// geometry-lookup buffer), bound directly by GPU virtual address --
			// see kRayTlasRootParameter's comment in Dx12Device.h. Root
			// descriptors, not a table, since there's exactly one of each.
			D3D12_ROOT_PARAMETER tlasRoot = {};
			tlasRoot.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
			tlasRoot.Descriptor.ShaderRegister = 0;
			tlasRoot.Descriptor.RegisterSpace = 2;
			tlasRoot.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			params.push_back(tlasRoot);

			D3D12_ROOT_PARAMETER geometryLookupRoot = {};
			geometryLookupRoot.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
			geometryLookupRoot.Descriptor.ShaderRegister = 1;
			geometryLookupRoot.Descriptor.RegisterSpace = 2;
			geometryLookupRoot.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			params.push_back(geometryLookupRoot);

			D3D12_ROOT_PARAMETER rayGeometryTexTable = {};
			rayGeometryTexTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rayGeometryTexTable.DescriptorTable.NumDescriptorRanges = 1;
			rayGeometryTexTable.DescriptorTable.pDescriptorRanges = &rayGeometryTexRange;
			rayGeometryTexTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			params.push_back(rayGeometryTexTable);

			D3D12_ROOT_SIGNATURE_DESC desc = {};
			desc.NumParameters = (UINT)params.size();
			desc.pParameters = params.data();
			desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
			serializeAndCreate(desc, myComputeRootSig, "compute root signature");
		}
	}

	void Dx12Device::CreateSwapchain(HWND hwnd, uint32_t w, uint32_t h)
	{
		DXGI_SWAP_CHAIN_DESC1 scd = {};
		scd.Width = w;
		scd.Height = h;
		scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;   // flip-model swapchains reject sRGB formats directly; sRGB is applied via the RTV
		scd.SampleDesc.Count = 1;
		scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scd.BufferCount = kFramesInFlight;
		scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		BOOL allowTearing = FALSE;
		if (SUCCEEDED(myFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))) && allowTearing)
		{
			myTearingSupported = true;
			scd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		}
		scd.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

		ComPtr<IDXGISwapChain1> sc1;
		HRESULT hr = myFactory->CreateSwapChainForHwnd(myQueue.Get(), hwnd, &scd, nullptr, nullptr, sc1.GetAddressOf());
		assert(SUCCEEDED(hr)); (void)hr;
		myFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
		hr = sc1.As(&mySwapChain);
		assert(SUCCEEDED(hr));
		
		mySwapChain->SetMaximumFrameLatency(kFramesInFlight - 1);
		mySwapChainWaitable = mySwapChain->GetFrameLatencyWaitableObject();

		myFrameIndex = mySwapChain->GetCurrentBackBufferIndex();
		AdoptBackBuffers();

		// Default depth buffer, matching the DX11 backend's GetDefaultDepth().
		TextureDesc dd = {};
		dd.width = w; dd.height = h;
		dd.format = Format::D32_Float;
		dd.bind = TextureBind::DepthStencil | TextureBind::ShaderResource;
		dd.debugName = "Dx12 default depth";
		myDepthTex = CreateTexture(dd);
		myDepthDsv = CreateDsv(myDepthTex, {});
	}

	void Dx12Device::AdoptBackBuffers()
	{
		for (uint32_t i = 0; i < kFramesInFlight; ++i)
		{
			ComPtr<ID3D12Resource> res;
			HRESULT hr = mySwapChain->GetBuffer(i, IID_PPV_ARGS(res.GetAddressOf()));
			assert(SUCCEEDED(hr)); (void)hr;

			TextureRec rec;
			rec.res = res;
			rec.state = D3D12_RESOURCE_STATE_PRESENT;
			rec.desc.width = myResolution.x;
			rec.desc.height = myResolution.y;
			rec.desc.format = Format::R8G8B8A8_UNorm;
			rec.desc.bind = TextureBind::RenderTarget;
			myBackBufferTex[i] = myTextures.Alloc(std::move(rec));

			uint32_t slot = myRtvHeap.Allocate();
			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;   // sRGB write view on the UNORM resource (flip-model requires the resource itself be non-sRGB)
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			myDevice->CreateRenderTargetView(res.Get(), &rtvDesc, myRtvHeap.Cpu(slot));
			myBackBufferRtv[i] = myRtvSlots.Alloc(RtvRec{ slot, Format::R8G8B8A8_UNorm_sRGB, myBackBufferTex[i] });

			// A second, non-sRGB (linear/UNORM write) view on the SAME resource --
			// mirrors DX11::BackBufferNoSrgbConversion. ImGui's colors are already
			// gamma-encoded, so writing through an sRGB RTV would double-apply
			// gamma; it renders through this view instead (see DX11::EndFrame's
			// BackBufferNoSrgbConversion->SetAsActiveTarget() call).
			uint32_t noSrgbSlot = myRtvHeap.Allocate();
			D3D12_RENDER_TARGET_VIEW_DESC noSrgbDesc = {};
			noSrgbDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			noSrgbDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			myDevice->CreateRenderTargetView(res.Get(), &noSrgbDesc, myRtvHeap.Cpu(noSrgbSlot));
			myBackBufferRtvNoSrgb[i] = myRtvSlots.Alloc(RtvRec{ noSrgbSlot, Format::R8G8B8A8_UNorm, myBackBufferTex[i] });
		}
	}

	void Dx12Device::WaitForGpuIdle()
	{
		const uint64_t v = myNextFenceValue++;
		myQueue->Signal(myFence.Get(), v);
		if (myFence->GetCompletedValue() < v)
		{
			myFence->SetEventOnCompletion(v, myFenceEvent);
			WaitForSingleObject(myFenceEvent, INFINITE);
		}
	}

	// ------------------------------------------------------------------ frame lifecycle
	ICommandContext& Dx12Device::BeginFrame()
	{
		myFrameIndex = mySwapChain ? mySwapChain->GetCurrentBackBufferIndex() : 0;

		if (myFirstFrame)
		{
			// The constructor left myCmdList open (Reset + root sigs bound)
			// specifically so engine-init code could record onto it before
			// this, the game loop's first-ever BeginFrame() (e.g. TextService::
			// Init()'s font-atlas UpdateTexture+GenerateMips -- see
			// IDevice::GetContext()'s own doc comment: "valid... before the
			// first BeginFrame during engine init"). Flush + wait for that
			// work HERE, synchronously and in isolation, rather than silently
			// folding it into frame 1's own submission -- keeps init-time GPU
			// work fully executed (and any problem with it caught) on its
			// own, and lets every frame after this go through the exact same
			// Reset() path uniformly.
			SubmitUploadList();
			myCmdList->Close();
			ID3D12CommandList* initLists[] = { myCmdList.Get() };
			myQueue->ExecuteCommandLists(1, initLists);
			WaitForGpuIdle();
			myFirstFrame = false;
		}
		else
		{
			if (mySwapChainWaitable)
				WaitForSingleObject(mySwapChainWaitable, INFINITE);
			
			if (mySwapChain)
				myFrameIndex = mySwapChain->GetCurrentBackBufferIndex();

			const uint64_t waitValue = myFenceValues[myFrameIndex];
			if (waitValue != 0 && myFence->GetCompletedValue() < waitValue)
			{
				myFence->SetEventOnCompletion(waitValue, myFenceEvent);
				WaitForSingleObject(myFenceEvent, INFINITE);
			}
		}

		// The fence wait above guarantees this frame-in-flight's LAST
		// submission (2 frames ago) has fully retired -- safe to release any
		// one-off UPLOAD resources UpdateTexture etc. queued for that frame.
		myPendingUploadReleases[myFrameIndex].clear();
		myPendingDxrReleases[myFrameIndex].clear();

		myDynCursor = 0;   // reset this frame's dynamic-constant ring
		myUploadCursor = 0;
		myCbvSrvUavScratch[myFrameIndex].ResetRange();
		mySamplerScratch[myFrameIndex].ResetRange();
		myTimestampFrameMin = UINT32_MAX;
		myTimestampFrameMax = 0;

		myAllocators[myFrameIndex]->Reset();
		myCmdList->Reset(myAllocators[myFrameIndex].Get(), nullptr);

		// Bind THIS frame's shader-visible scratch heaps (not the permanent,
		// non-shader-visible CreateSrv/CreateSampler storage heaps) -- see
		// Dx12Device.h's class comment.
		ID3D12DescriptorHeap* heaps[] = { myCbvSrvUavScratch[myFrameIndex].Heap(), mySamplerScratch[myFrameIndex].Heap() };
		myCmdList->SetDescriptorHeaps(2, heaps);

		myContext->OnBeginFrame();

		if (mySwapChain)
			myContext->TransitionResource(myBackBufferTex[myFrameIndex], ResourceState::RenderTarget);

		return *myContext;
	}

	void Dx12Device::EndFrame(bool vsync)
	{
		if (mySwapChain)
			myContext->TransitionResource(myBackBufferTex[myFrameIndex], ResourceState::Present);
		
		myContext->FlushBarriers();

		if (myTimestampFrameMin <= myTimestampFrameMax)
		{
			const uint32_t count = myTimestampFrameMax - myTimestampFrameMin + 1;
			myCmdList->ResolveQueryData(myTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, myTimestampFrameMin, count, myTimestampReadback.Get(), myTimestampFrameMin * sizeof(uint64_t));
		}

		SubmitUploadList();   // this frame may use resources uploaded in a batch
		myCmdList->Close();
		ID3D12CommandList* lists[] = { myCmdList.Get() };
		myQueue->ExecuteCommandLists(1, lists);

		if (mySwapChain)
		{
			UINT presentFlags = 0;
			if (!vsync && myTearingSupported)
				presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
			mySwapChain->Present(vsync ? 1 : 0, presentFlags);
		}

		const uint64_t v = myNextFenceValue++;
		myQueue->Signal(myFence.Get(), v);
		myFenceValues[myFrameIndex] = v;

		// Cheap (no wait, no message drain -- just the one HRESULT-returning
		// call every other per-frame API here lacks) but load-bearing: without
		// this, a dead device is invisible until something else happens to
		// call a device method that returns HRESULT (found 2026-09-11 --
		// GameMain ran for many frames looking fine, rendering nothing but the
		// clear color, with a device that had actually been DEVICE_HUNG since
		// frame 2). Kept permanently now that it's known this backend needs it.
		static uint64_t sFrameNo = 0; ++sFrameNo;
		HRESULT dr = myDevice->GetDeviceRemovedReason();
		if (FAILED(dr))
			ERROR_PRINT("Dx12Device: device removed at frame %llu, reason=0x%08X", (unsigned long long)sFrameNo, (unsigned)dr);

		// Cheap no-op unless TGE_DX12_DEBUG_LAYER actually enabled the debug
		// layer (see DX11::InitDx12): ID3D12InfoQueue is only queryable then,
		// so this costs one failed QueryInterface per frame otherwise.
		DrainDebugMessages("EndFrame");
	}

	ICommandContext& Dx12Device::GetContext() { return *myContext; }

	bool Dx12Device::Resize(uint32_t w, uint32_t h)
	{
		if (!mySwapChain || w == 0 || h == 0) return false;
		WaitForGpuIdle();

		for (uint32_t i = 0; i < kFramesInFlight; ++i)
		{
			if (RtvRec* r = myRtvSlots.Get(myBackBufferRtv[i])) myRtvHeap.Free(r->slot);
			myRtvSlots.Free(myBackBufferRtv[i]);
			if (RtvRec* r = myRtvSlots.Get(myBackBufferRtvNoSrgb[i])) myRtvHeap.Free(r->slot);
			myRtvSlots.Free(myBackBufferRtvNoSrgb[i]);
			myTextures.Free(myBackBufferTex[i]);
		}
		if (myDepthDsv.IsValid()) { Destroy(myDepthDsv); myDepthDsv = {}; }
		if (myDepthTex.IsValid()) { Destroy(myDepthTex); myDepthTex = {}; }

		UINT swapChainFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		if (myTearingSupported) swapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		
		myResolution = { w, h };
		HRESULT hr = mySwapChain->ResizeBuffers(kFramesInFlight, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, swapChainFlags);
		if (FAILED(hr)) { ERROR_PRINT("Dx12Device::Resize: ResizeBuffers failed 0x%08X", (unsigned)hr); return false; }

		myFrameIndex = mySwapChain->GetCurrentBackBufferIndex();
		AdoptBackBuffers();

		TextureDesc dd = {};
		dd.width = w; dd.height = h;
		dd.format = Format::D32_Float;
		dd.bind = TextureBind::DepthStencil | TextureBind::ShaderResource;
		dd.debugName = "Dx12 default depth";
		myDepthTex = CreateTexture(dd);
		myDepthDsv = CreateDsv(myDepthTex, {});
		return true;
	}

	bool Dx12Device::SetFullscreen(bool enabled)
	{
		if (!mySwapChain) return false;
		const HRESULT hr = mySwapChain->SetFullscreenState(enabled ? TRUE : FALSE, nullptr);
		if (FAILED(hr))
		{
			ERROR_PRINT("Dx12Device::SetFullscreen failed 0x%08X", (unsigned)hr);
			return false;
		}
		return true;
	}

	// ------------------------------------------------------------------ resources
	BufferHandle Dx12Device::CreateBuffer(const BufferDesc& d, const void* initialData)
	{
		if (!initialData) return CreateBufferImpl(d, nullptr);
		const std::function<void(void*)> copy = [&](void* mapped) { memcpy(mapped, initialData, d.byteSize); };
		return CreateBufferImpl(d, &copy);
	}

	BufferHandle Dx12Device::CreateBufferImpl(const BufferDesc& d, const std::function<void(void*)>* fill)
	{
		const bool initialData = fill != nullptr;
		D3D12_RESOURCE_DESC rd = {};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		rd.Width = d.byteSize ? d.byteSize : 1;
		rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
		rd.Format = DXGI_FORMAT_UNKNOWN;
		rd.SampleDesc.Count = 1;
		rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		rd.Flags = HasUsage(d.usage, BufferUsage::UAV) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

		D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_STATES initState = D3D12_RESOURCE_STATE_COMMON;
		if (d.memory == MemoryType::Upload) { heapType = D3D12_HEAP_TYPE_UPLOAD; initState = D3D12_RESOURCE_STATE_GENERIC_READ; }
		else if (d.memory == MemoryType::Readback) { heapType = D3D12_HEAP_TYPE_READBACK; initState = D3D12_RESOURCE_STATE_COPY_DEST; }
		else if (initialData) initState = D3D12_RESOURCE_STATE_COPY_DEST;   // uploaded into, then transitioned by the caller
		// A Default-heap buffer with no initial data and UAV usage is a GPU-only
		// scratch/output buffer (cluster lists, GI SH coefficients, ...) never
		// touched by CPU upload -- its very first real use is a compute shader
		// writing through its UAV. D3D12's implicit resource-state-promotion
		// rule ONLY promotes a buffer FROM COMMON into read states (SRV, COPY_
		// SOURCE, VERTEX_AND_CONSTANT_BUFFER, INDEX_BUFFER, INDIRECT_ARGUMENT)
		// on first access -- promotion to UNORDERED_ACCESS is explicitly NOT
		// allowed. Leaving such a buffer in COMMON and binding it as a UAV
		// anyway is undefined behavior; on this hardware/driver it manifests as
		// a silent crash on the first Dispatch that writes it (found debugging
		// DeferredRenderer::GiProjectProbe, but the same bug applies to every
		// GPU-only UAV buffer created this way, e.g. the cluster-culling
		// buffers). Create these already in UNORDERED_ACCESS state instead --
		// legal since nothing needs to read them before their first write.
		else if (!initialData && HasUsage(d.usage, BufferUsage::UAV)) initState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

		D3D12_HEAP_PROPERTIES heapProps = { heapType };
		ComPtr<ID3D12Resource> res;
		HRESULT hr = myDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rd, initState, nullptr, IID_PPV_ARGS(res.GetAddressOf()));
		if (FAILED(hr)) { ERROR_PRINT("Dx12Device::CreateBuffer: CreateCommittedResource failed 0x%08X", (unsigned)hr); return {}; }

		if (initialData && d.memory == MemoryType::Default)
			UploadBufferFill(res.Get(), d.byteSize, *fill);
		else if (initialData && d.memory == MemoryType::Upload)
		{
			void* mapped = nullptr;
			D3D12_RANGE noRead{ 0, 0 };
			if (SUCCEEDED(res->Map(0, &noRead, &mapped)))
			{
				(*fill)(mapped);
				res->Unmap(0, nullptr);
			}
		}

		// UploadBufferData records (and synchronously retires) COPY_DEST ->
		// GENERIC_READ.  Keep the CPU-side state tracker in lockstep with that
		// one-off command list; otherwise a later explicit barrier uses
		// COPY_DEST as StateBefore even though the GPU resource is already in
		// GENERIC_READ.  D3D12 does not repair a mismatched StateBefore for us.
		const D3D12_RESOURCE_STATES finalState =
			(initialData && d.memory == MemoryType::Default)
				? D3D12_RESOURCE_STATE_GENERIC_READ
				: initState;
		BufferRec rec; rec.res = res; rec.desc = d; rec.state = finalState;
		return myBuffers.Alloc(std::move(rec));
	}

	void Dx12Device::CreateRaytracingBlases(const RaytracingBlasDesc* descs, uint32_t count, RaytracingBlasHandle* out)
	{
		for (uint32_t i = 0; i < count; ++i) out[i] = {};
		if (!myRaytracingTier11 || !myDevice5 || count == 0) return;

		auto alignAs = [](uint64_t size) { return (size + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1) &
			~uint64_t(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1); };
		auto createBuffer = [&](uint64_t size, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags, ComPtr<ID3D12Resource>& res) -> bool
		{
			D3D12_HEAP_PROPERTIES heap = { heapType };
			D3D12_RESOURCE_DESC rd = {};
			rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			rd.Width = size; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
			rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; rd.Flags = flags;
			return SUCCEEDED(myDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr,
				IID_PPV_ARGS(res.GetAddressOf())));
		};

		struct Pending
		{
			uint32_t index = 0;
			ComPtr<ID3D12Resource> uncompacted, scratch;
			uint64_t uncompactedSize = 0;
		};
		std::vector<Pending> pending;
		pending.reserve(count);

		ComPtr<ID3D12Resource> postbuild, readback;
		const uint64_t postbuildBytes = uint64_t(count) * sizeof(uint64_t);
		if (!createBuffer(postbuildBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, postbuild) ||
			!createBuffer(postbuildBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_FLAG_NONE, readback))
		{
			for (uint32_t i = 0; i < count; ++i) out[i] = CreateRaytracingBlas(descs[i]);
			return;
		}

		// Pass 1: every build (and its compacted-size query) on the upload list,
		// behind whatever vertex/index uploads the batch already holds.
		ID3D12GraphicsCommandList* list = OpenUploadList();
		ComPtr<ID3D12GraphicsCommandList4> list4;
		if (FAILED(myUploadCmdList.As(&list4))) return;
		uint64_t scratchBytes = 0;
		for (uint32_t i = 0; i < count; ++i)
		{
			const RaytracingBlasDesc& desc = descs[i];
			if (!desc.vertexBuffer.IsValid() || !desc.indexBuffer.IsValid() || desc.vertexCount == 0 ||
				desc.indexCount < 3 || desc.vertexStride == 0 || desc.indexFormat != Format::R32_UInt) continue;
			BufferRec* vb = myBuffers.Get(desc.vertexBuffer);
			BufferRec* ib = myBuffers.Get(desc.indexBuffer);
			if (!vb || !ib || !vb->res || !ib->res) continue;

			D3D12_RAYTRACING_GEOMETRY_DESC geometry = {};
			geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
			geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
			geometry.Triangles.IndexCount = desc.indexCount;
			geometry.Triangles.VertexCount = desc.vertexCount;
			geometry.Triangles.IndexBuffer = ib->res->GetGPUVirtualAddress();
			geometry.Triangles.VertexBuffer.StartAddress = vb->res->GetGPUVirtualAddress();
			geometry.Triangles.VertexBuffer.StrideInBytes = desc.vertexStride;

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
			inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
			inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			inputs.NumDescs = 1;
			inputs.pGeometryDescs = &geometry;
			inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
			myDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
			if (prebuild.ResultDataMaxSizeInBytes == 0 || prebuild.ScratchDataSizeInBytes == 0) continue;

			Pending p;
			p.index = i;
			p.uncompactedSize = alignAs(prebuild.ResultDataMaxSizeInBytes);
			if (!createBuffer(p.uncompactedSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
					D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, p.uncompacted) ||
				!createBuffer(alignAs(prebuild.ScratchDataSizeInBytes), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
					D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, p.scratch))
			{
				ERROR_PRINT("DXR BLAS '%s': resource allocation failed", desc.debugName ? desc.debugName : "unnamed");
				continue;
			}
			scratchBytes += prebuild.ScratchDataSizeInBytes;

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postInfo = {};
			postInfo.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
			postInfo.DestBuffer = postbuild->GetGPUVirtualAddress() + uint64_t(i) * sizeof(uint64_t);
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
			build.Inputs = inputs;
			build.DestAccelerationStructureData = p.uncompacted->GetGPUVirtualAddress();
			build.ScratchAccelerationStructureData = p.scratch->GetGPUVirtualAddress();
			list4->BuildRaytracingAccelerationStructure(&build, 1, &postInfo);
			D3D12_RESOURCE_BARRIER uav = {};
			uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			uav.UAV.pResource = p.uncompacted.Get();
			list->ResourceBarrier(1, &uav);
			pending.push_back(std::move(p));
		}
		if (pending.empty()) return;

		D3D12_RESOURCE_BARRIER toCopy = {};
		toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		toCopy.Transition.pResource = postbuild.Get();
		toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		list->ResourceBarrier(1, &toCopy);
		list->CopyBufferRegion(readback.Get(), 0, postbuild.Get(), 0, postbuildBytes);
		SubmitUploadList();

		std::vector<uint64_t> compactedSizes(count, 0);
		void* mapped = nullptr;
		D3D12_RANGE readRange{ 0, postbuildBytes };
		if (SUCCEEDED(readback->Map(0, &readRange, &mapped)) && mapped)
		{
			memcpy(compactedSizes.data(), mapped, postbuildBytes);
			readback->Unmap(0, nullptr);
		}

		// Pass 2: compact everything in one submission.
		std::vector<ComPtr<ID3D12Resource>> results(pending.size());
		uint64_t totalUncompacted = 0, totalFinal = 0;
		for (size_t k = 0; k < pending.size(); ++k)
		{
			Pending& p = pending[k];
			results[k] = p.uncompacted;
			const uint64_t compactedSize = compactedSizes[p.index];
			if (compactedSize != 0 && compactedSize < p.uncompactedSize)
			{
				ComPtr<ID3D12Resource> compacted;
				if (createBuffer(alignAs(compactedSize), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
					D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, compacted))
				{
					OpenUploadList();
					list4->CopyRaytracingAccelerationStructure(compacted->GetGPUVirtualAddress(),
						p.uncompacted->GetGPUVirtualAddress(), D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
					results[k] = std::move(compacted);
				}
			}
			const uint64_t finalSize = (compactedSize && compactedSize < p.uncompactedSize) ? compactedSize : p.uncompactedSize;
			totalUncompacted += p.uncompactedSize;
			totalFinal += finalSize;
			out[p.index] = myBlas.Alloc(BlasRec{ results[k], finalSize, p.uncompactedSize });
		}
		SubmitUploadList();
		// myBlas holds its own references; the uncompacted/scratch buffers die here.
		INFO_PRINT("DXR BLAS batch: %zu meshes, %llu KiB -> %llu KiB (scratch %llu KiB)", pending.size(),
			(unsigned long long)(totalUncompacted / 1024), (unsigned long long)(totalFinal / 1024), (unsigned long long)(scratchBytes / 1024));
	}

	RaytracingBlasHandle Dx12Device::CreateRaytracingBlas(const RaytracingBlasDesc& desc)
	{
		if (!myRaytracingTier11 || !myDevice5 || !desc.vertexBuffer.IsValid() || !desc.indexBuffer.IsValid() ||
			desc.vertexCount == 0 || desc.indexCount < 3 || desc.vertexStride == 0 || desc.indexFormat != Format::R32_UInt)
			return {};

		BufferRec* vb = myBuffers.Get(desc.vertexBuffer);
		BufferRec* ib = myBuffers.Get(desc.indexBuffer);
		if (!vb || !ib || !vb->res || !ib->res) return {};

		D3D12_RAYTRACING_GEOMETRY_DESC geometry = {};
		geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
		geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		geometry.Triangles.IndexCount = desc.indexCount;
		geometry.Triangles.VertexCount = desc.vertexCount;
		geometry.Triangles.IndexBuffer = ib->res->GetGPUVirtualAddress();
		geometry.Triangles.VertexBuffer.StartAddress = vb->res->GetGPUVirtualAddress();
		geometry.Triangles.VertexBuffer.StrideInBytes = desc.vertexStride;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
		inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		inputs.NumDescs = 1;
		inputs.pGeometryDescs = &geometry;
		inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
		myDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
		if (prebuild.ResultDataMaxSizeInBytes == 0 || prebuild.ScratchDataSizeInBytes == 0)
		{
			ERROR_PRINT("DXR BLAS '%s': invalid prebuild sizes", desc.debugName ? desc.debugName : "unnamed");
			return {};
		}

		auto alignAs = [](uint64_t size) { return (size + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1) &
			~uint64_t(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1); };
		auto createBuffer = [&](uint64_t size, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags, ComPtr<ID3D12Resource>& out) -> bool
		{
			D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
			D3D12_RESOURCE_DESC rd = {};
			rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			rd.Width = size; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
			rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; rd.Flags = flags;
			return SUCCEEDED(myDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr,
				IID_PPV_ARGS(out.GetAddressOf())));
		};

		ComPtr<ID3D12Resource> uncompacted, scratch, postbuild, readback;
		const uint64_t uncompactedSize = alignAs(prebuild.ResultDataMaxSizeInBytes);
		if (!createBuffer(uncompactedSize, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, uncompacted) ||
			!createBuffer(alignAs(prebuild.ScratchDataSizeInBytes), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, scratch) ||
			!createBuffer(sizeof(uint64_t), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, postbuild))
		{
			ERROR_PRINT("DXR BLAS '%s': resource allocation failed", desc.debugName ? desc.debugName : "unnamed");
			return {};
		}

		D3D12_HEAP_PROPERTIES readbackHeap = { D3D12_HEAP_TYPE_READBACK };
		D3D12_RESOURCE_DESC readbackDesc = {};
		readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		readbackDesc.Width = sizeof(uint64_t); readbackDesc.Height = 1; readbackDesc.DepthOrArraySize = 1;
		readbackDesc.MipLevels = 1; readbackDesc.SampleDesc.Count = 1; readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if (FAILED(myDevice->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf())))) return {};

		ComPtr<ID3D12GraphicsCommandList4> list4;
		if (FAILED(myUploadCmdList.As(&list4))) return {};
		SubmitUploadList();   // vertex/index data of a pending batch must land first
		myUploadAllocator->Reset();
		myUploadCmdList->Reset(myUploadAllocator.Get(), nullptr);

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postInfo = {};
		postInfo.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
		postInfo.DestBuffer = postbuild->GetGPUVirtualAddress();
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
		build.Inputs = inputs;
		build.DestAccelerationStructureData = uncompacted->GetGPUVirtualAddress();
		build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
		list4->BuildRaytracingAccelerationStructure(&build, 1, &postInfo);
		D3D12_RESOURCE_BARRIER barriers[2] = {};
		barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[1].Transition.pResource = postbuild.Get();
		barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		myUploadCmdList->ResourceBarrier(2, barriers);
		myUploadCmdList->CopyBufferRegion(readback.Get(), 0, postbuild.Get(), 0, sizeof(uint64_t));
		myUploadCmdList->Close();
		ID3D12CommandList* buildLists[] = { myUploadCmdList.Get() };
		myQueue->ExecuteCommandLists(1, buildLists);
		WaitForGpuIdle();

		uint64_t compactedSize = 0;
		void* mapped = nullptr;
		D3D12_RANGE readRange{ 0, sizeof(uint64_t) };
		if (SUCCEEDED(readback->Map(0, &readRange, &mapped)) && mapped)
		{
			compactedSize = *static_cast<const uint64_t*>(mapped);
			readback->Unmap(0, nullptr);
		}

		ComPtr<ID3D12Resource> finalResult = uncompacted;
		if (compactedSize != 0 && compactedSize < uncompactedSize)
		{
			ComPtr<ID3D12Resource> compacted;
			if (createBuffer(alignAs(compactedSize), D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, compacted))
			{
				myUploadAllocator->Reset();
				myUploadCmdList->Reset(myUploadAllocator.Get(), nullptr);
				list4->CopyRaytracingAccelerationStructure(compacted->GetGPUVirtualAddress(),
					uncompacted->GetGPUVirtualAddress(), D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
				myUploadCmdList->Close();
				ID3D12CommandList* compactLists[] = { myUploadCmdList.Get() };
				myQueue->ExecuteCommandLists(1, compactLists);
				WaitForGpuIdle();
				finalResult = std::move(compacted);
			}
		}

		INFO_PRINT("DXR BLAS '%s': %llu KiB -> %llu KiB", desc.debugName ? desc.debugName : "unnamed",
			(unsigned long long)(uncompactedSize / 1024), (unsigned long long)((compactedSize ? compactedSize : uncompactedSize) / 1024));
		return myBlas.Alloc(BlasRec{ std::move(finalResult), compactedSize ? compactedSize : uncompactedSize, uncompactedSize });
	}

	void Dx12Device::BuildRaytracingTlas(const RaytracingInstanceDesc* instances, uint32_t count)
	{
		if (!myRaytracingTier11 || !myCmdList4) return;
		DxrFrameResources& frame = myDxrFrames[myFrameIndex];
		// Invalidate before every possible early return so a zero-instance frame
		// cannot trace the last populated contents of this slot.
		frame.sceneValid = false;
		if (!instances || count == 0) return;
		struct GeometryLookupGpu
		{
			uint32_t vertexSrv, indexSrv, materialIndex, vertexStride;
			uint32_t positionOffset, normalOffset, uv0Offset, tangentOffset;
			uint32_t binormalOffset, _pad0, _pad1, _pad2;
			float previousTransform[12];
			uint32_t motionHistoryValid, _motionPad[3];
		};
		static_assert(sizeof(GeometryLookupGpu) == 112);
		if (!frame.geometryLookup || frame.geometryLookupCapacity < count)
		{
			const uint32_t capacity = std::max(count, frame.geometryLookupCapacity ? frame.geometryLookupCapacity * 2 : 64u);
			D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
			D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = uint64_t(capacity) * sizeof(GeometryLookupGpu);
			rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			if (FAILED(myDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(frame.geometryLookup.ReleaseAndGetAddressOf())))) return;
			frame.geometryLookupCapacity = capacity;
		}
		GeometryLookupGpu* lookup = nullptr; D3D12_RANGE noRead{ 0, 0 };
		if (FAILED(frame.geometryLookup->Map(0, &noRead, reinterpret_cast<void**>(&lookup))) || !lookup) return;
		for (uint32_t i = 0; i < count; ++i) {
			lookup[i] = { instances[i].vertexSrv, instances[i].indexSrv, instances[i].materialIndex, instances[i].vertexStride,
				instances[i].positionOffset, instances[i].normalOffset, instances[i].uv0Offset, instances[i].tangentOffset,
				instances[i].binormalOffset, 0, 0, 0 };
			memcpy(lookup[i].previousTransform, instances[i].previousTransform, sizeof(lookup[i].previousTransform));
			lookup[i].motionHistoryValid = instances[i].motionHistoryValid;
		}
		frame.geometryLookup->Unmap(0, nullptr);
		if (!frame.instanceDescs || frame.instanceCapacity < count)
		{
			const uint32_t capacity = std::max(count, frame.instanceCapacity ? frame.instanceCapacity * 2 : 64u);
			D3D12_HEAP_PROPERTIES upload = { D3D12_HEAP_TYPE_UPLOAD };
			D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			rd.Width = uint64_t(capacity) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC); rd.Height = 1;
			rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			if (FAILED(myDevice->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &rd,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(frame.instanceDescs.ReleaseAndGetAddressOf())))) return;
			frame.instanceCapacity = capacity;
		}

		D3D12_RAYTRACING_INSTANCE_DESC* dst = nullptr;
		if (FAILED(frame.instanceDescs->Map(0, &noRead, reinterpret_cast<void**>(&dst))) || !dst) return;
		bool unchanged = frame.tlas && frame.builtInstances.size() == count && frame.builtBlas.size() == count;
		bool topologyUnchanged = unchanged;
		for (uint32_t i = 0; i < count; ++i)
		{
			BlasRec* blas = myBlas.Get(instances[i].blas);
			if (!blas || !blas->result) { frame.instanceDescs->Unmap(0, nullptr); return; }
			D3D12_RAYTRACING_INSTANCE_DESC d = {};
			memcpy(d.Transform, instances[i].transform, sizeof(d.Transform));
			d.InstanceID = instances[i].instanceId;
			d.InstanceMask = instances[i].instanceMask;
			// Every BLAS is built GEOMETRY_FLAG_OPAQUE, but that alone decides
			// nothing while a RayQuery carries RAY_FLAG_FORCE_NON_OPAQUE -- the
			// ray flag wins, and every candidate triangle of every ray leaves
			// traversal hardware for the shader's Proceed() loop. Deciding it
			// here instead, per instance, keeps the alpha-test round trip on
			// the geometry that actually needs one (see
			// RayTracingMaterialTable::IsRayOpaque) and lets the hardware
			// resolve the rest. Costs nothing: an instance flag is part of the
			// TLAS instance desc, so no BLAS is rebuilt for it.
			d.Flags = instances[i].rayOpaque
				? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE
				: D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
			d.AccelerationStructure = blas->result->GetGPUVirtualAddress();
			
			if (topologyUnchanged)
			{
				bool sameTopology = (frame.builtBlas[i] == instances[i].blas &&
					frame.builtInstances[i].InstanceID == d.InstanceID &&
					frame.builtInstances[i].InstanceMask == d.InstanceMask &&
					frame.builtInstances[i].Flags == d.Flags &&
					frame.builtInstances[i].AccelerationStructure == d.AccelerationStructure);
				topologyUnchanged = sameTopology;
				unchanged = unchanged && sameTopology && memcmp(frame.builtInstances[i].Transform, d.Transform, sizeof(d.Transform)) == 0;
			}
			dst[i] = d;
		}
		// Compare CPU snapshots, never read write-combined upload memory.
		// Handle generations also catch recycled BLAS GPU addresses.
		if (unchanged)
		{
			frame.instanceDescs->Unmap(0, nullptr);
			frame.sceneValid = true;
			return;
		}
		frame.instanceDescs->Unmap(0, nullptr);

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
		inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		inputs.NumDescs = count;
		inputs.InstanceDescs = frame.instanceDescs->GetGPUVirtualAddress();
		inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD |
					   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
		myDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
		auto alignAs = [](uint64_t v) { return (v + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1) & ~uint64_t(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1); };
		auto ensure = [&](ComPtr<ID3D12Resource>& resource, uint64_t bytes, D3D12_RESOURCE_STATES state) {
			if (resource && resource->GetDesc().Width >= bytes) return true;
			D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
			D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = bytes; rd.Height = 1;
			rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			return SUCCEEDED(myDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())));
		};

		bool doUpdate = topologyUnchanged && frame.tlas;
		uint64_t scratchSize = doUpdate ? info.UpdateScratchDataSizeInBytes : info.ScratchDataSizeInBytes;
		if (!ensure(frame.scratch, alignAs(scratchSize), D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
			(!doUpdate && !ensure(frame.tlas, alignAs(info.ResultDataMaxSizeInBytes), D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE))) return;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
		if (doUpdate)
		{
			inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
			build.SourceAccelerationStructureData = frame.tlas->GetGPUVirtualAddress();
		}
		build.Inputs = inputs;
		build.DestAccelerationStructureData = frame.tlas->GetGPUVirtualAddress();
		build.ScratchAccelerationStructureData = frame.scratch->GetGPUVirtualAddress();
		myCmdList4->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
		D3D12_RESOURCE_BARRIER uav = {}; uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		myCmdList->ResourceBarrier(1, &uav);
		frame.builtInstances.resize(count);
		frame.builtBlas.resize(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			D3D12_RAYTRACING_INSTANCE_DESC d = {};
			memcpy(d.Transform, instances[i].transform, sizeof(d.Transform));
			d.InstanceID = instances[i].instanceId;
			d.InstanceMask = instances[i].instanceMask;
			// Must match the Flags actually written into the upload buffer
			// above, or the next frame's topologyUnchanged compare (which reads
			// Flags) never matches and every frame pays a full TLAS rebuild
			// instead of a refit.
			d.Flags = instances[i].rayOpaque
				? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE
				: D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
			d.AccelerationStructure = myBlas.Get(instances[i].blas)->result->GetGPUVirtualAddress();
			frame.builtInstances[i] = d;
			frame.builtBlas[i] = instances[i].blas;
		}
		frame.sceneValid = true;
		static uint32_t sLastCount = ~0u;
		if (sLastCount != count) { INFO_PRINT("DXR TLAS: full rebuild, %u instances", count); sLastCount = count; }
	}

	bool Dx12Device::BindRaytracingSceneForCompute()
	{
		if (!myRaytracingTier11 || !myCmdList || !myDxrFrames[myFrameIndex].sceneValid)
			return false;

		const D3D12_GPU_VIRTUAL_ADDRESS tlas = RayTlasGpuAddress();
		const D3D12_GPU_VIRTUAL_ADDRESS lookup = RayGeometryLookupGpuAddress();
		if (!tlas || !lookup)
			return false;

		// Root arguments are immediate, not deferred. This must happen after the
		// current command list has recorded BuildRaytracingAccelerationStructure.
		myCmdList->SetComputeRootShaderResourceView(kRayTlasRootParameter, tlas);
		myCmdList->SetComputeRootShaderResourceView(kRayGeometryLookupRootParameter, lookup);
		return true;
	}

	TextureHandle Dx12Device::CreateTexture(const TextureDesc& d, const SubresourceData* initial, uint32_t initialCount)
	{
		const bool depth = IsDepth(d.format);
		D3D12_RESOURCE_DESC rd = {};
		rd.Dimension = (d.dimension == TextureDimension::Tex3D) ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		rd.Width = d.width;
		rd.Height = d.height;
		rd.DepthOrArraySize = (UINT16)((d.dimension == TextureDimension::TexCube) ? 6u * d.depthOrArraySize : d.depthOrArraySize);
		rd.MipLevels = (UINT16)d.mipLevels;
		rd.Format = depth ? ToTypeless(d.format) : ToDxgi(d.format);
		rd.SampleDesc.Count = d.sampleCount ? d.sampleCount : 1;
		rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		rd.Flags = D3D12_RESOURCE_FLAG_NONE;
		if (HasBind(d.bind, TextureBind::RenderTarget))    rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		if (HasBind(d.bind, TextureBind::DepthStencil))    rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		if (HasBind(d.bind, TextureBind::UnorderedAccess)) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		D3D12_CLEAR_VALUE clearValue = {};
		D3D12_CLEAR_VALUE* pClear = nullptr;
		D3D12_RESOURCE_STATES initState = D3D12_RESOURCE_STATE_COMMON;
		if (HasBind(d.bind, TextureBind::RenderTarget))
		{
			// A D3D12_CLEAR_VALUE must be a concrete, non-typeless format --
			// but a render target can legitimately be REQUESTED as a typeless
			// resource (e.g. RenderTarget's TYPELESS+sRGB-RTV+linear-SRV case),
			// and CreateTexture has no visibility into which RtvDesc::
			// formatOverride a later CreateRtv call will actually view it as.
			// Rather than guess, skip the optimized clear value entirely for a
			// typeless format -- legal in D3D12, it just forgoes the fast-clear
			// hint (irrelevant for the small, infrequently-cleared targets this
			// case is used for). A concrete format (the common case) still gets
			// its real clear value as before.
			if (!IsTypeless(d.format))
			{
				clearValue.Format = ToDxgi(d.format);
				pClear = &clearValue;
			}
			initState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		}
		else if (HasBind(d.bind, TextureBind::DepthStencil))
		{
			clearValue.Format = ToDsvFormat(d.format);
			clearValue.DepthStencil.Depth = 1.f;
			pClear = &clearValue;
			initState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		}
		else if (initial && initialCount)
			initState = D3D12_RESOURCE_STATE_COPY_DEST;
		// See the identical case in CreateBuffer just above: a UAV-only texture
		// (no RenderTarget/DepthStencil, no initial data -- a pure compute
		// output) must not be left in COMMON, since D3D12's implicit state
		// promotion never promotes into UNORDERED_ACCESS. Create it there
		// directly instead.
		else if (HasBind(d.bind, TextureBind::UnorderedAccess))
			initState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

		D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
		ComPtr<ID3D12Resource> res;
		HRESULT hr = myDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rd, initState, pClear, IID_PPV_ARGS(res.GetAddressOf()));
		if (FAILED(hr)) { ERROR_PRINT("Dx12Device::CreateTexture: CreateCommittedResource failed 0x%08X", (unsigned)hr); return {}; }

		if (initial && initialCount)
			UploadTextureData(res.Get(), d, initial, initialCount);

		// UploadTextureData synchronously changes the resource's state before
		// this record is published.  The tracked state must be the upload's
		// StateAfter, not the creation state (COPY_DEST for ordinary loaded
		// textures).  A stale COPY_DEST here made the next frame emit resource
		// barriers with an invalid StateBefore, which is undefined GPU work and
		// can manifest as DEVICE_HUNG rather than a CPU-side HRESULT.
		D3D12_RESOURCE_STATES finalState = initState;
		if (initial && initialCount)
		{
			finalState = HasBind(d.bind, TextureBind::RenderTarget)
				? D3D12_RESOURCE_STATE_RENDER_TARGET
				: HasBind(d.bind, TextureBind::DepthStencil)
					? D3D12_RESOURCE_STATE_DEPTH_WRITE
					: D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		}
		TextureRec rec; rec.res = res; rec.desc = d; rec.state = finalState;
		return myTextures.Alloc(std::move(rec));
	}

	Format Dx12Device::GetTextureFormat(TextureHandle h) const
	{
		const TextureRec* t = myTextures.Get(h);
		return t ? t->desc.format : Format::Unknown;
	}

	void Dx12Device::UploadBufferData(ID3D12Resource* dst, const void* data, size_t size)
	{
		UploadBufferFill(dst, size, [&](void* mapped) { memcpy(mapped, data, size); });
	}

	void Dx12Device::UploadBufferFill(ID3D12Resource* dst, size_t size, const std::function<void(void*)>& fill)
	{
		D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
		D3D12_RESOURCE_DESC ud = {};
		ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ud.Width = size; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
		ud.SampleDesc.Count = 1;
		ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ComPtr<ID3D12Resource> upload;
		myDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload.GetAddressOf()));

		void* mapped = nullptr;
		D3D12_RANGE noRead{ 0, 0 };
		upload->Map(0, &noRead, &mapped);
		fill(mapped);
		upload->Unmap(0, nullptr);

		ID3D12GraphicsCommandList* list = OpenUploadList();
		list->CopyBufferRegion(dst, 0, upload.Get(), 0, size);
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = dst;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		list->ResourceBarrier(1, &b);
		FinishUpload(std::move(upload), size);
	}

	ID3D12GraphicsCommandList* Dx12Device::OpenUploadList()
	{
		if (!myUploadListOpen)
		{
			myUploadAllocator->Reset();
			myUploadCmdList->Reset(myUploadAllocator.Get(), nullptr);
			myUploadListOpen = true;
		}
		return myUploadCmdList.Get();
	}

	void Dx12Device::SubmitUploadList()
	{
		if (!myUploadListOpen) return;
		myUploadCmdList->Close();
		ID3D12CommandList* lists[] = { myUploadCmdList.Get() };
		myQueue->ExecuteCommandLists(1, lists);
		WaitForGpuIdle();
		myUploadListOpen = false;
		myUploadStaging.clear();
		myUploadStagingBytes = 0;
	}

	void Dx12Device::FinishUpload(ComPtr<ID3D12Resource> staging, uint64_t bytes)
	{
		if (myUploadBatchDepth <= 0) { SubmitUploadList(); return; }
		myUploadStaging.push_back(std::move(staging));
		myUploadStagingBytes += bytes;
		// Bound the staging memory a large scene can pin at once.
		constexpr uint64_t kMaxBatchStagingBytes = 512ull << 20;
		if (myUploadStagingBytes >= kMaxBatchStagingBytes) SubmitUploadList();
	}

	bool Dx12Device::QueryVideoMemory(uint64_t& outUsage, uint64_t& outBudget)
	{
		DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
		if (!myAdapter || FAILED(myAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) return false;
		outUsage = info.CurrentUsage;
		outBudget = info.Budget;
		return true;
	}

	void Dx12Device::EndUploadBatch()
	{
		if (myUploadBatchDepth > 0 && --myUploadBatchDepth == 0)
			SubmitUploadList();
	}

	void Dx12Device::UploadTextureData(ID3D12Resource* dst, const TextureDesc& d, const SubresourceData* initial, uint32_t count)
	{
		D3D12_RESOURCE_DESC dstDesc = dst->GetDesc();
		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(count);
		std::vector<UINT> numRows(count);
		std::vector<UINT64> rowSizes(count);
		UINT64 totalBytes = 0;
		myDevice->GetCopyableFootprints(&dstDesc, 0, count, 0, footprints.data(), numRows.data(), rowSizes.data(), &totalBytes);

		D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
		D3D12_RESOURCE_DESC ud = {};
		ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ud.Width = totalBytes; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
		ud.SampleDesc.Count = 1;
		ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ComPtr<ID3D12Resource> upload;
		myDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload.GetAddressOf()));

		uint8_t* mapped = nullptr;
		D3D12_RANGE noRead{ 0, 0 };
		upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped));
		for (uint32_t i = 0; i < count; ++i)
		{
			const uint8_t* src = static_cast<const uint8_t*>(initial[i].data);
			uint8_t* rowDst = mapped + footprints[i].Offset;
			for (UINT row = 0; row < numRows[i]; ++row)
			{
				memcpy(rowDst + row * footprints[i].Footprint.RowPitch,
				       src + row * initial[i].rowPitch,
				       (size_t)rowSizes[i]);
			}
		}
		upload->Unmap(0, nullptr);

		ID3D12GraphicsCommandList* list = OpenUploadList();
		// CreateTexture creates render/depth targets directly in their natural
		// states so they are immediately usable when no initial data is supplied.
		// If such a resource *does* carry initial data, transition it explicitly
		// before CopyTextureRegion: copies require COPY_DEST, not RT/DEPTH_WRITE.
		const bool rt = HasBind(d.bind, TextureBind::RenderTarget);
		const bool ds = HasBind(d.bind, TextureBind::DepthStencil);
		const D3D12_RESOURCE_STATES creationState = rt ? D3D12_RESOURCE_STATE_RENDER_TARGET
			: ds ? D3D12_RESOURCE_STATE_DEPTH_WRITE
			: D3D12_RESOURCE_STATE_COPY_DEST;
		if (creationState != D3D12_RESOURCE_STATE_COPY_DEST)
		{
			D3D12_RESOURCE_BARRIER toCopyDest = {};
			toCopyDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			toCopyDest.Transition.pResource = dst;
			toCopyDest.Transition.StateBefore = creationState;
			toCopyDest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			toCopyDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			list->ResourceBarrier(1, &toCopyDest);
		}
		for (uint32_t i = 0; i < count; ++i)
		{
			D3D12_TEXTURE_COPY_LOCATION dstLoc = { dst, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
			dstLoc.SubresourceIndex = i;
			D3D12_TEXTURE_COPY_LOCATION srcLoc = { upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {} };
			srcLoc.PlacedFootprint = footprints[i];
			list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
		}
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = dst;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		b.Transition.StateAfter = rt ? D3D12_RESOURCE_STATE_RENDER_TARGET : ds ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		list->ResourceBarrier(1, &b);
		FinishUpload(std::move(upload), totalBytes);
	}

	// ------------------------------------------------------------------ views
	SrvHandle Dx12Device::CreateSrv(TextureHandle h, const SrvDesc& d)
	{
		TextureRec* t = myTextures.Get(h);
		if (!t || !t->res) return {};
		D3D12_SHADER_RESOURCE_VIEW_DESC vd = {};
		const Format vf = d.formatOverride != Format::Unknown ? d.formatOverride
		                : (IsDepth(t->desc.format) ? FromDxgi(ToDepthSrvFormat(t->desc.format)) : t->desc.format);
		vd.Format = IsDepth(t->desc.format) ? ToDepthSrvFormat(t->desc.format) : ToDxgi(vf);
		vd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		const uint32_t mips = (d.mipLevels == kAllMips) ? (t->desc.mipLevels ? t->desc.mipLevels : 1) : d.mipLevels;

		if (t->desc.dimension == TextureDimension::Tex3D)
		{
			vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			vd.Texture3D.MostDetailedMip = d.mostDetailedMip;
			vd.Texture3D.MipLevels = mips;
		}
		else if ((d.asCube || t->desc.dimension == TextureDimension::TexCube) && d.arraySize == kAllSlices)
		{
			// Full 6-face cube view -- the common case (every existing caller
			// before this comment used the SrvDesc defaults, i.e. this branch).
			vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			vd.TextureCube.MostDetailedMip = d.mostDetailedMip;
			vd.TextureCube.MipLevels = mips;
		}
		else if (t->desc.dimension == TextureDimension::Tex2DArray || t->desc.dimension == TextureDimension::TexCube)
		{
			// Tex2DArray view -- also how an individual cubemap FACE (or a
			// sub-range of faces) is viewed as a plain array slice when the
			// caller explicitly asks for one via firstArraySlice/arraySize
			// (e.g. GenerateMips's per-face blit) -- D3D12_SRV_DIMENSION_
			// TEXTURECUBE has no such per-slice option, but the underlying
			// resource is equally a Texture2DArray either way.
			vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			vd.Texture2DArray.MostDetailedMip = d.mostDetailedMip;
			vd.Texture2DArray.MipLevels = mips;
			vd.Texture2DArray.FirstArraySlice = d.firstArraySlice;
			vd.Texture2DArray.ArraySize = (d.arraySize == kAllSlices)
				? ((t->desc.dimension == TextureDimension::TexCube) ? 6u : t->desc.depthOrArraySize) : d.arraySize;
		}
		else
		{
			vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			vd.Texture2D.MostDetailedMip = d.mostDetailedMip;
			vd.Texture2D.MipLevels = mips;
		}

		uint32_t slot = myCbvSrvUavHeap.Allocate();
		myDevice->CreateShaderResourceView(t->res.Get(), &vd, myCbvSrvUavHeap.Cpu(slot));
		return mySrvSlots.Alloc(SrvRec{ slot, h, {} });
	}

	SrvHandle Dx12Device::CreateSrv(BufferHandle h, const SrvDesc& d)
	{
		BufferRec* b = myBuffers.Get(h);
		if (!b || !b->res) return {};
		D3D12_SHADER_RESOURCE_VIEW_DESC vd = {};
		vd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		vd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		vd.Buffer.FirstElement = d.bufferFirstElement;
		const bool raw = d.bufferType == BufferSrvType::Raw;
		const bool structured = d.bufferType == BufferSrvType::Structured ||
			(d.bufferType == BufferSrvType::Default && HasUsage(b->desc.usage, BufferUsage::Structured));
		if (raw)
		{
			assert((b->desc.byteSize & 3u) == 0 && "ByteAddressBuffer resources must have a 4-byte size");
			vd.Format = DXGI_FORMAT_R32_TYPELESS;
			vd.Buffer.NumElements = d.bufferNumElements ? d.bufferNumElements : (UINT)(b->desc.byteSize / 4);
			vd.Buffer.StructureByteStride = 0;
			vd.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		}
		else if (structured)
		{
			assert(b->desc.stride != 0 && "Structured buffer SRV requires a non-zero stride");
			vd.Format = DXGI_FORMAT_UNKNOWN;
			vd.Buffer.NumElements = d.bufferNumElements ? d.bufferNumElements : (UINT)(b->desc.byteSize / b->desc.stride);
			vd.Buffer.StructureByteStride = b->desc.stride;
			vd.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		}
		else
		{
			vd.Format = d.formatOverride != Format::Unknown ? ToDxgi(d.formatOverride) : DXGI_FORMAT_R32_UINT;
			vd.Buffer.NumElements = d.bufferNumElements ? d.bufferNumElements : (UINT)(b->desc.byteSize / 4);
			vd.Buffer.StructureByteStride = 0;
			vd.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		}

		uint32_t slot = myCbvSrvUavHeap.Allocate();
		myDevice->CreateShaderResourceView(b->res.Get(), &vd, myCbvSrvUavHeap.Cpu(slot));
		return mySrvSlots.Alloc(SrvRec{ slot, {}, h });
	}

	uint32_t Dx12Device::RegisterRaySceneSrv(SrvHandle source)
	{
		SrvRec* rec = mySrvSlots.Get(source);
		if (!rec) return 0;
		const uint64_t sourceKey = (uint64_t(source.generation) << 32) | source.index;
		if (const auto existing = myRaySceneDescriptorSlots.find(sourceKey); existing != myRaySceneDescriptorSlots.end())
			return existing->second;

		// The scene table is mirrored into each frame heap. Updating a descriptor
		// while any one of those heaps is executing would race the GPU, so this
		// load-time registration path intentionally refuses that operation.
		bool needsIdle = false;
		for (uint32_t i = 0; i < kFramesInFlight; ++i)
			needsIdle |= myFenceValues[i] != 0 && myFence->GetCompletedValue() < myFenceValues[i];
		// Imported scenes may finish loading after the first rendered frame. A
		// one-time idle here makes the descriptor mirrored into every frame heap
		// safe immediately; future streaming replaces this with deferred updates.
		if (needsIdle) WaitForGpuIdle();

		if (myNextRaySceneDescriptor >= kRaySceneDescriptorCapacity)
		{
			ERROR_PRINT("DXR scene descriptor table exhausted (%u entries)", kRaySceneDescriptorCapacity - 1);
			return 0;
		}

		const uint32_t sceneIndex = myNextRaySceneDescriptor++;
		const D3D12_CPU_DESCRIPTOR_HANDLE sourceCpu = CbvSrvUavCpuHandle(rec->slot);
		for (uint32_t i = 0; i < kFramesInFlight; ++i)
		{
			myDevice->CopyDescriptorsSimple(1, myCbvSrvUavScratch[i].Cpu(sceneIndex), sourceCpu,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		myRaySceneDescriptorSlots.emplace(sourceKey, sceneIndex);
		return sceneIndex;
	}

	UavHandle Dx12Device::CreateUav(TextureHandle h, const UavDesc& d)
	{
		TextureRec* t = myTextures.Get(h);
		if (!t || !t->res) return {};
		D3D12_UNORDERED_ACCESS_VIEW_DESC vd = {};
		vd.Format = d.formatOverride != Format::Unknown ? ToDxgi(d.formatOverride) : ToDxgi(t->desc.format);
		if (t->desc.dimension == TextureDimension::Tex2DArray || t->desc.dimension == TextureDimension::TexCube)
		{
			vd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			vd.Texture2DArray.MipSlice = d.mipSlice;
			vd.Texture2DArray.FirstArraySlice = d.firstArraySlice;
			vd.Texture2DArray.ArraySize = (d.arraySize == kAllSlices)
				? ((t->desc.dimension == TextureDimension::TexCube) ? 6u : t->desc.depthOrArraySize) : d.arraySize;
		}
		else
		{
			vd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			vd.Texture2D.MipSlice = d.mipSlice;
		}

		uint32_t slot = myCbvSrvUavHeap.Allocate();
		myDevice->CreateUnorderedAccessView(t->res.Get(), nullptr, &vd, myCbvSrvUavHeap.Cpu(slot));
		return myUavSlots.Alloc(UavRec{ slot, h, {} });
	}

	UavHandle Dx12Device::CreateUav(BufferHandle h, const UavDesc& d)
	{
		BufferRec* b = myBuffers.Get(h);
		if (!b || !b->res) return {};
		D3D12_UNORDERED_ACCESS_VIEW_DESC vd = {};
		vd.Format = DXGI_FORMAT_UNKNOWN;
		vd.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		vd.Buffer.FirstElement = d.bufferFirstElement;
		vd.Buffer.NumElements = d.bufferNumElements ? d.bufferNumElements : (b->desc.stride ? (UINT)(b->desc.byteSize / b->desc.stride) : (UINT)b->desc.byteSize);
		vd.Buffer.StructureByteStride = b->desc.stride;
		vd.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		uint32_t slot = myCbvSrvUavHeap.Allocate();
		myDevice->CreateUnorderedAccessView(b->res.Get(), nullptr, &vd, myCbvSrvUavHeap.Cpu(slot));
		return myUavSlots.Alloc(UavRec{ slot, {}, h });
	}

	RtvHandle Dx12Device::CreateRtv(TextureHandle h, const RtvDesc& d)
	{
		TextureRec* t = myTextures.Get(h);
		if (!t || !t->res) return {};
		D3D12_RENDER_TARGET_VIEW_DESC vd = {};
		vd.Format = d.formatOverride != Format::Unknown ? ToDxgi(d.formatOverride) : ToDxgi(t->desc.format);
		if (t->desc.dimension == TextureDimension::Tex2DArray || t->desc.dimension == TextureDimension::TexCube)
		{
			vd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			vd.Texture2DArray.MipSlice = d.mipSlice;
			vd.Texture2DArray.FirstArraySlice = d.firstArraySlice;
			vd.Texture2DArray.ArraySize = d.arraySize;
		}
		else
		{
			vd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			vd.Texture2D.MipSlice = d.mipSlice;
		}

		uint32_t slot = myRtvHeap.Allocate();
		myDevice->CreateRenderTargetView(t->res.Get(), &vd, myRtvHeap.Cpu(slot));
		const Format viewFormat = d.formatOverride != Format::Unknown ? d.formatOverride : t->desc.format;
		return myRtvSlots.Alloc(RtvRec{ slot, viewFormat, h });
	}

	DsvHandle Dx12Device::CreateDsv(TextureHandle h, const DsvDesc& d)
	{
		TextureRec* t = myTextures.Get(h);
		if (!t || !t->res) return {};
		D3D12_DEPTH_STENCIL_VIEW_DESC vd = {};
		vd.Format = d.formatOverride != Format::Unknown ? ToDsvFormat(d.formatOverride) : ToDsvFormat(t->desc.format);
		// NOT unconditionally TEXTURE2D (found 2026-09-12, live with the user):
		// a D3D12_DSV_DIMENSION_TEXTURE2D view on a Texture2DArray resource
		// implicitly targets array slice 0 ONLY, silently ignoring
		// firstArraySlice/arraySize entirely -- every one of DeferredRenderer's
		// 4 per-cascade shadow DSVs (CreateShadowMaps: firstArraySlice=0..3,
		// arraySize=1 each) was therefore secretly the SAME view of slice 0,
		// so all 4 cascades rendered into (and overwrote) one slice while the
		// lighting pass's SRV correctly sampled array index 0..3 -- slices 1-3
		// held whatever was last in that memory, never actual shadow data.
		// Visually: shadows appeared to "follow the camera" (only the last-
		// rendered cascade's real depth ever existed, always recentered on
		// the current view) and vanished outright wherever PickCascade
		// selected a non-zero cascade and sampled garbage. CreateRtv already
		// had the correct Tex2DArray branch; CreateDsv was just missing its
		// twin.
		if (t->desc.dimension == TextureDimension::Tex2DArray || t->desc.dimension == TextureDimension::TexCube)
		{
			vd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			vd.Texture2DArray.MipSlice = d.mipSlice;
			vd.Texture2DArray.FirstArraySlice = d.firstArraySlice;
			vd.Texture2DArray.ArraySize = d.arraySize;
		}
		else
		{
			vd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			vd.Texture2D.MipSlice = d.mipSlice;
		}
		if (d.readOnly) vd.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;

		uint32_t slot = myDsvHeap.Allocate();
		myDevice->CreateDepthStencilView(t->res.Get(), &vd, myDsvHeap.Cpu(slot));
		const Format viewFormat = d.formatOverride != Format::Unknown ? d.formatOverride : t->desc.format;
		return myDsvSlots.Alloc(DsvRec{ slot, viewFormat, h });
	}

	SamplerHandle Dx12Device::CreateSampler(const SamplerDesc& d)
	{
		D3D12_SAMPLER_DESC sd = {};
		switch (d.filter)
		{
		case FilterMode::Point:              sd.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT; break;
		case FilterMode::Bilinear:           sd.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT; break;
		case FilterMode::Trilinear:          sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; break;
		case FilterMode::Anisotropic:        sd.Filter = D3D12_FILTER_ANISOTROPIC; break;
		case FilterMode::ComparisonBilinear: sd.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; break;
		}
		if (d.comparison && d.filter != FilterMode::ComparisonBilinear)
			sd.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		D3D12_TEXTURE_ADDRESS_MODE am = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		switch (d.address)
		{
		case AddressMode::Clamp:  am = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; break;
		case AddressMode::Wrap:   am = D3D12_TEXTURE_ADDRESS_MODE_WRAP; break;
		case AddressMode::Mirror: am = D3D12_TEXTURE_ADDRESS_MODE_MIRROR; break;
		case AddressMode::Border: am = D3D12_TEXTURE_ADDRESS_MODE_BORDER; break;
		}
		sd.AddressU = sd.AddressV = sd.AddressW = am;
		sd.MipLODBias = d.mipBias;
		sd.MaxAnisotropy = d.maxAnisotropy ? d.maxAnisotropy : 1;
		sd.ComparisonFunc = (d.comparison || d.filter == FilterMode::ComparisonBilinear) ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_NEVER;
		for (int i = 0; i < 4; ++i) sd.BorderColor[i] = d.borderColor[i];
		sd.MinLOD = 0.f;
		sd.MaxLOD = D3D12_FLOAT32_MAX;

		uint32_t slot = mySamplerHeap.Allocate();
		myDevice->CreateSampler(&sd, mySamplerHeap.Cpu(slot));
		return mySamplerSlots.Alloc(std::move(slot));
	}

	// ------------------------------------------------------------------ shaders / pipelines (milestone 2)
	ShaderModuleHandle Dx12Device::CreateShaderModule(ShaderKind kind, const void* bytecode, size_t size)
	{
		// A genuinely missing/unreadable shader file reaches here with size==0
		// (DX11::ForceLoad*Shader's file.open() failed silently, or the asset
		// path just doesn't resolve on this try -- callers routinely try more
		// than one candidate path and fall back on failure). DX11 catches this
		// naturally: CreateXxxShader(nullptr, 0, ...) fails its own HRESULT
		// check. DX12 has no equivalent validating call here, so returning a
		// "valid" handle wrapping zero bytes would silently defeat every
		// caller's `module.IsValid()` load-succeeded check (found 2026-09-12:
		// this is exactly what made CubemapPrefilter's "try data/shaders/X,
		// fall back to Shaders/X" logic skip the fallback and hand
		// CreateComputePipelineState an empty compute shader).
		if (!bytecode || size == 0) return {};
		ShaderRec rec;
		rec.kind = kind;
		rec.bytecode.assign(static_cast<const uint8_t*>(bytecode), static_cast<const uint8_t*>(bytecode) + size);
		return myShaders.Alloc(std::move(rec));
	}

	GraphicsPipelineHandle Dx12Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& d)
	{
		const uint64_t key = HashGraphicsDesc(d);
		if (auto it = myGfxCache.find(key); it != myGfxCache.end()) return it->second;

		ShaderRec* vs = myShaders.Get(d.vs);
		ShaderRec* ps = myShaders.Get(d.ps);
		assert(vs && ps && "Dx12Device::CreateGraphicsPipeline: invalid vs/ps handle");

		std::vector<D3D12_INPUT_ELEMENT_DESC> elems(d.inputLayoutCount);
		for (uint32_t i = 0; i < d.inputLayoutCount; ++i)
		{
			const InputElement& e = d.inputLayout[i];
			D3D12_INPUT_ELEMENT_DESC& o = elems[i];
			o.SemanticName = e.semanticName;
			o.SemanticIndex = e.semanticIndex;
			o.Format = ToDxgi(e.format);
			o.InputSlot = e.inputSlot;
			o.AlignedByteOffset = (e.alignedByteOffset == ~0u) ? D3D12_APPEND_ALIGNED_ELEMENT : e.alignedByteOffset;
			o.InputSlotClass = e.perInstance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			o.InstanceDataStepRate = e.perInstance ? e.instanceStepRate : 0;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
		pso.pRootSignature = myGraphicsRootSig.Get();
		pso.VS = { vs->bytecode.data(), vs->bytecode.size() };
		pso.PS = { ps->bytecode.data(), ps->bytecode.size() };
		pso.BlendState = BlendFor(d.blend);
		pso.SampleMask = UINT_MAX;
		pso.RasterizerState = RasterFor(d.raster);
		pso.DepthStencilState = DepthFor(d.depth);
		pso.DepthStencilState.DepthEnable = (d.dsvFormat != Format::Unknown);
		pso.InputLayout = { elems.empty() ? nullptr : elems.data(), (UINT)elems.size() };
		pso.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
		pso.PrimitiveTopologyType = TopoTypeFor(d.topology);
		pso.NumRenderTargets = d.renderTargetCount;
		for (uint32_t i = 0; i < d.renderTargetCount && i < 8; ++i) pso.RTVFormats[i] = ToDxgi(d.rtvFormats[i]);
		pso.DSVFormat = (d.dsvFormat != Format::Unknown) ? ToDsvFormat(d.dsvFormat) : DXGI_FORMAT_UNKNOWN;
		pso.SampleDesc.Count = 1;

		ComPtr<ID3D12PipelineState> state;
		HRESULT hr = myDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(state.GetAddressOf()));
		if (FAILED(hr))
		{
			ERROR_PRINT("Dx12Device::CreateGraphicsPipeline: CreateGraphicsPipelineState failed 0x%08X", (unsigned)hr);
			return {};
		}

		GfxPipelineRec rec;
		rec.pso = state;
		rec.topo = ToD3D12Topology(d.topology);
		GraphicsPipelineHandle h = myGfxPipelines.Alloc(std::move(rec));
		myGfxCache.emplace(key, h);
		return h;
	}

	ComputePipelineHandle Dx12Device::CreateComputePipeline(const ComputePipelineDesc& d)
	{
		const uint64_t key = ((uint64_t)d.cs.index << 32) | d.cs.generation;
		if (auto it = myComputeCache.find(key); it != myComputeCache.end()) return it->second;

		ShaderRec* cs = myShaders.Get(d.cs);
		assert(cs && "Dx12Device::CreateComputePipeline: invalid cs handle");

		D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
		pso.pRootSignature = myComputeRootSig.Get();
		pso.CS = { cs->bytecode.data(), cs->bytecode.size() };

		ComPtr<ID3D12PipelineState> state;
		HRESULT hr = myDevice->CreateComputePipelineState(&pso, IID_PPV_ARGS(state.GetAddressOf()));
		if (FAILED(hr))
		{
			ERROR_PRINT("Dx12Device::CreateComputePipeline: CreateComputePipelineState failed 0x%08X", (unsigned)hr);
			return {};
		}

		ComputePipelineRec rec;
		rec.pso = state;
		ComputePipelineHandle h = myComputePipelines.Alloc(std::move(rec));
		myComputeCache.emplace(key, h);
		return h;
	}

	// ------------------------------------------------------------------ destroy
	void Dx12Device::Destroy(BufferHandle h)
	{
		// Pool::Free() overwrites the slot's BufferRec with a fresh default one
		// right here, on the spot -- for a ComPtr<ID3D12Resource> that drops the
		// GPU resource's last reference IMMEDIATELY, synchronously, regardless
		// of whether any command list still references it. Every resource this
		// engine destroys is destroyed from CPU-side game/engine code, which has
		// no idea whether a command list recorded earlier THIS SAME frame (not
		// yet submitted -- that happens at EndFrame) or a still-in-flight
		// previous frame's submission still touches it. Route it through the
		// same "keep alive until this frame-in-flight slot's fence retires"
		// mechanism already used for one-off upload resources (see
		// KeepAliveUntilFrameRetires's callers) instead of trusting the caller.
		// Found 2026-09-11: CubemapPrefilter::CaptureSceneToCubemap re-populates
		// the SAME CubemapData (and so calls CubemapData::Reset() -> this) many
		// times in a row across a GI probe bake with no GPU flush in between --
		// each Reset() was destroying the PREVIOUS capture's texture while its
		// own render/copy/GenerateMips commands were still sitting unexecuted
		// in the current command list, a real GPU-side use-after-free that
		// reliably produced a DXGI_ERROR_DEVICE_HUNG a couple of frames later
		// (confirmed via the D3D12 debug layer: "resource object ... was
		// deleted prior to executing the command list").
		if (BufferRec* b = myBuffers.Get(h))
			if (b->res) KeepAliveUntilFrameRetires(b->res);
		myBuffers.Free(h);
	}
	void Dx12Device::Destroy(TextureHandle h)
	{
		// See Destroy(BufferHandle)'s comment just above -- same hazard, same fix.
		if (TextureRec* t = myTextures.Get(h))
			if (t->res) KeepAliveUntilFrameRetires(t->res);
		myTextures.Free(h);
	}
	void Dx12Device::Destroy(RaytracingBlasHandle h) { myBlas.Free(h); }
	void Dx12Device::Destroy(SrvHandle h)     { if (SrvRec* s = mySrvSlots.Get(h)) { myCbvSrvUavHeap.Free(s->slot); mySrvSlots.Free(h); } }
	void Dx12Device::Destroy(UavHandle h)     { if (UavRec* s = myUavSlots.Get(h)) { myCbvSrvUavHeap.Free(s->slot); myUavSlots.Free(h); } }
	void Dx12Device::Destroy(RtvHandle h)     { if (RtvRec* r = myRtvSlots.Get(h)) { myRtvHeap.Free(r->slot); myRtvSlots.Free(h); } }
	void Dx12Device::Destroy(DsvHandle h)     { if (DsvRec* r = myDsvSlots.Get(h)) { myDsvHeap.Free(r->slot); myDsvSlots.Free(h); } }
	void Dx12Device::Destroy(SamplerHandle h) { if (uint32_t* s = mySamplerSlots.Get(h)) { mySamplerHeap.Free(*s); mySamplerSlots.Free(h); } }
	void Dx12Device::Destroy(ShaderModuleHandle h) { myShaders.Free(h); }

	// ------------------------------------------------------------------ dynamic constants
	DynamicAlloc Dx12Device::AllocateDynamicConstants(const void* data, uint32_t byteSize)
	{
		const uint32_t aligned = (byteSize + 255u) & ~255u;
		assert(myDynCursor + aligned <= kDynRingBytes && "Dx12Device: dynamic-constant ring exhausted for this frame");

		DynamicAlloc a;
		a.buffer = myDynRing[myFrameIndex];
		a.offset = myDynCursor;
		a.size = byteSize;
		a.cpuPtr = myDynRingCpu[myFrameIndex] + myDynCursor;
		if (data) memcpy(a.cpuPtr, data, byteSize);
		myDynCursor += aligned;
		return a;
	}

	void Dx12Device::AllocateUploadSpace(uint32_t size, uint32_t alignment, uint64_t& outOffset, void*& outCpuAddr, ID3D12Resource*& outRes)
	{
		uint32_t alignedOffset = (myUploadCursor + alignment - 1) & ~(alignment - 1);
		if (alignedOffset + size <= kUploadRingBytes)
		{
			outOffset = alignedOffset;
			outCpuAddr = myUploadRingCpu[myFrameIndex] + alignedOffset;
			outRes = myUploadRing[myFrameIndex].Get();
			myUploadCursor = alignedOffset + size;
			return;
		}

		D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
		D3D12_RESOURCE_DESC ud = {};
		ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ud.Width = size; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
		ud.SampleDesc.Count = 1;
		ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ComPtr<ID3D12Resource> upload;
		myDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload.GetAddressOf()));
		
		D3D12_RANGE noRead{ 0, 0 };
		upload->Map(0, &noRead, &outCpuAddr);
		outOffset = 0;
		outRes = upload.Get();
		KeepAliveUntilFrameRetires(std::move(upload));
	}

	// ------------------------------------------------------------------ timestamps
	TimestampQueryHandle Dx12Device::CreateTimestampQuery()
	{
		TimestampRec rec;
		rec.queryIndex = myNextTimestampSlot++;
		if (rec.queryIndex * 2 + 1 >= kMaxTimestamps)
		{
			ERROR_PRINT("Dx12Device: timestamp query pool exhausted (need %u, capacity %u)", rec.queryIndex * 2 + 2, kMaxTimestamps);
			--myNextTimestampSlot;
			return {};
		}
		return myTimestamps.Alloc(std::move(rec));
	}
	void Dx12Device::DestroyTimestampQuery(TimestampQueryHandle h) { myTimestamps.Free(h); }
	bool Dx12Device::GetTimestampMs(TimestampQueryHandle h, double& outMs)
	{
		TimestampRec* r = myTimestamps.Get(h);
		if (!r || !r->hasBegin || !r->hasEnd || myGpuTimestampFrequency == 0.0)
			return false;

		const uint32_t beginSlot = r->queryIndex * 2;
		const uint32_t endSlot   = r->queryIndex * 2 + 1;

		// Map the readback buffer to read resolved timestamps. The readback
		// resource is in COPY_DEST state permanently -- Map is always valid
		// for READBACK heaps regardless of resource state.
		D3D12_RANGE readRange{ (SIZE_T)(beginSlot * sizeof(uint64_t)),
		                       (SIZE_T)((endSlot + 1) * sizeof(uint64_t)) };
		uint64_t* data = nullptr;
		if (FAILED(myTimestampReadback->Map(0, &readRange, reinterpret_cast<void**>(&data))) || !data)
			return false;

		const uint64_t t0 = data[beginSlot];
		const uint64_t t1 = data[endSlot];

		D3D12_RANGE noWrite{ 0, 0 };
		myTimestampReadback->Unmap(0, &noWrite);

		if (t1 < t0)
			return false;

		outMs = double(t1 - t0) / myGpuTimestampFrequency * 1000.0;
		r->hasBegin = r->hasEnd = false;
		return true;
	}

	// ------------------------------------------------------------------ Stage-1-only bridges (DX12 never uses these)
	void* Dx12Device::GetNativeDevice()  { return myDevice.Get(); }
	void* Dx12Device::GetNativeContext() { assert(false && "Dx12: GetNativeContext has no DX12 equivalent -- see GetNativeCommandQueue/CommandList"); return nullptr; }
	void* Dx12Device::GetNativeSrv(SrvHandle)     { assert(false && "Dx12: GetNativeSrv is a DX11-only legacy-interop bridge"); return nullptr; }
	void* Dx12Device::GetNativeRtv(RtvHandle)     { assert(false && "Dx12: GetNativeRtv is a DX11-only legacy-interop bridge"); return nullptr; }
	void* Dx12Device::GetNativeTexture(TextureHandle h)
	{
		// Native texture access is deliberately narrow: vendor compute features
		// (Streamline DLAA today, NRD next) need the ID3D12Resource while the
		// RHI remains responsible for descriptor ownership and transitions.
		TextureRec* t = myTextures.Get(h);
		return t ? t->res.Get() : nullptr;
	}
	void* Dx12Device::ImGuiTextureId(SrvHandle h)
	{
		// imgui_impl_dx12 expects a GPU descriptor handle (as a UINT64) that is
		// resident in the heap ImGuiInterface::Render() actually binds before
		// calling ImGui_ImplDX12_RenderDrawData -- myImGuiSrvHeap, NOT
		// myCbvSrvUavHeap (the permanent, non-shader-visible storage heap;
		// .Gpu() on that one isn't even a valid handle, let alone one in the
		// currently-bound heap). Implemented 2026-09-12 alongside fixing
		// Viewport.cpp's ImGui::Image call site to use this instead of the
		// DX11-only TextureResource::GetShaderResourceView() raw pointer path
		// (see SceneUtil.cpp's DrawOutlines for an identical DX12-unsafe call
		// this session also fixed) -- this is the "fix alongside migrating
		// that call site" the old comment here deferred to.
		uint32_t* srcSlot = GetSrvSlot(h);
		if (!srcSlot) return nullptr;

		const uint64_t key = ((uint64_t)h.index << 32) | h.generation;
		auto it = myImGuiTextureSlots.find(key);
		uint32_t destSlot;
		if (it != myImGuiTextureSlots.end())
			destSlot = it->second;
		else
		{
			destSlot = myImGuiSrvHeap.Allocate();
			myImGuiTextureSlots.emplace(key, destSlot);
		}

		// Re-copy every call (cheap: one descriptor) rather than only on first
		// insert -- a texture handle can survive a resize with a brand-new
		// underlying resource/SRV at the same permanent slot index reused by
		// the pool, so the cached ImGui-visible copy must be kept in sync.
		myDevice->CopyDescriptorsSimple(1, myImGuiSrvHeap.Cpu(destSlot), myCbvSrvUavHeap.Cpu(*srcSlot), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		return reinterpret_cast<void*>(myImGuiSrvHeap.Gpu(destSlot).ptr);
	}
	void* Dx12Device::GetNativeCommandQueue() { return myQueue.Get(); }
	void* Dx12Device::GetNativeCommandList()  { return myCmdList.Get(); }
	void* Dx12Device::GetImGuiSrvDescriptorHeap() { return myImGuiSrvHeap.Heap(); }
	void* Dx12Device::ImGuiFontSrvCpuHandle() { return reinterpret_cast<void*>(myImGuiSrvHeap.Cpu(myImGuiFontSrvSlot).ptr); }
	void* Dx12Device::ImGuiFontSrvGpuHandle() { return reinterpret_cast<void*>(myImGuiSrvHeap.Gpu(myImGuiFontSrvSlot).ptr); }
	SrvHandle Dx12Device::WrapNativeSrv(void*) { assert(false && "Dx12: WrapNativeSrv is a DX11-only Stage-1 migration bridge"); return {}; }
	RtvHandle Dx12Device::WrapNativeRtv(void*) { assert(false && "Dx12: WrapNativeRtv is a DX11-only Stage-1 migration bridge"); return {}; }
	DsvHandle Dx12Device::WrapNativeDsv(void*) { assert(false && "Dx12: WrapNativeDsv is a DX11-only Stage-1 migration bridge"); return {}; }
	void* Dx12Device::CreateInputLayoutNative(const InputElement*, uint32_t, const void*, uint32_t)
	{
		// DX12 folds the input layout into CreateGraphicsPipeline's desc directly
		// (D3D12_INPUT_LAYOUT_DESC) -- there's no separate native object to hand
		// back, unlike DX11's ID3D11InputLayout bridge.
		assert(false && "Dx12: input layout is part of CreateGraphicsPipeline, not a separate bridge");
		return nullptr;
	}

	bool Dx12Device::CaptureBackBufferPng(const wchar_t* utf16Path)
	{
		if (!mySwapChain) return false;
		// The frame this refers to as "current" (myFrameIndex) is the one about
		// to be recorded into -- its LAST use was as a backbuffer several frames
		// ago and it's long since idle. Reading it now (before this frame's own
		// BeginFrame has transitioned/touched it) is always safe.
		TextureRec* t = myTextures.Get(myBackBufferTex[myFrameIndex]);
		if (!t || !t->res) return false;

		D3D12_RESOURCE_DESC desc = t->res->GetDesc();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		UINT numRows = 0; UINT64 rowSizeBytes = 0, totalBytes = 0;
		myDevice->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSizeBytes, &totalBytes);

		D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_READBACK };
		D3D12_RESOURCE_DESC bufDesc = {};
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Width = totalBytes;
		bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
		bufDesc.SampleDesc.Count = 1;
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ComPtr<ID3D12Resource> readback;
		HRESULT rbhr = myDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
		if (FAILED(rbhr)) { ERROR_PRINT("Dx12Device::CaptureBackBufferPng: readback CreateCommittedResource failed 0x%08X", (unsigned)rbhr); return false; }

		SubmitUploadList();
		myUploadAllocator->Reset();
		myUploadCmdList->Reset(myUploadAllocator.Get(), nullptr);

		// NOT t->state here: BeginFrame (called earlier this same frame, before
		// GameWorld::Render()) already flipped that CPU-side bookkeeping to
		// RENDER_TARGET as part of recording this frame's PRESENT->RENDER_TARGET
		// transition into the MAIN command list -- but that list hasn't been
		// submitted yet (it only runs at EndFrame). This capture's own one-off
		// command list gets submitted and executed synchronously RIGHT NOW,
		// before the main list, so on the actual GPU timeline the resource is
		// still physically in PRESENT. Assuming t->state here would record a
		// transition that doesn't match the resource's real state and misbehave.
		D3D12_RESOURCE_BARRIER toSrc = {};
		toSrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		toSrc.Transition.pResource = t->res.Get();
		toSrc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		toSrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		myUploadCmdList->ResourceBarrier(1, &toSrc);

		D3D12_TEXTURE_COPY_LOCATION dstLoc = { readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {} };
		dstLoc.PlacedFootprint = footprint;
		D3D12_TEXTURE_COPY_LOCATION srcLoc = { t->res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
		srcLoc.SubresourceIndex = 0;
		myUploadCmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

		D3D12_RESOURCE_BARRIER toOrig = toSrc;
		std::swap(toOrig.Transition.StateBefore, toOrig.Transition.StateAfter);
		myUploadCmdList->ResourceBarrier(1, &toOrig);

		myUploadCmdList->Close();
		ID3D12CommandList* lists[] = { myUploadCmdList.Get() };
		myQueue->ExecuteCommandLists(1, lists);
		WaitForGpuIdle();

		void* mapped = nullptr;
		if (FAILED(readback->Map(0, nullptr, &mapped))) return false;

		// Minimal, dependency-free BMP writer (uncompressed BGRA -> BGR, top-down
		// via negative height) -- avoids pulling DirectXTex into this backend-
		// agnostic-until-now file just for a debugging screenshot. Whatever
		// extension the caller's path has, the bytes written are BMP.
		const uint32_t w = (uint32_t)desc.Width, h = (uint32_t)desc.Height;
		const uint32_t rowBytes = w * 3;
		const uint32_t rowPadded = (rowBytes + 3) & ~3u;
		const uint32_t pixelDataSize = rowPadded * h;
		const uint32_t fileSize = 14 + 40 + pixelDataSize;
		std::vector<uint8_t> bmp(fileSize, 0);
		bmp[0] = 'B'; bmp[1] = 'M';
		*reinterpret_cast<uint32_t*>(&bmp[2]) = fileSize;
		*reinterpret_cast<uint32_t*>(&bmp[10]) = 14 + 40;
		*reinterpret_cast<uint32_t*>(&bmp[14]) = 40;
		*reinterpret_cast<int32_t*>(&bmp[18]) = (int32_t)w;
		*reinterpret_cast<int32_t*>(&bmp[22]) = -(int32_t)h;   // negative = top-down
		*reinterpret_cast<uint16_t*>(&bmp[26]) = 1;
		*reinterpret_cast<uint16_t*>(&bmp[28]) = 24;
		*reinterpret_cast<uint32_t*>(&bmp[34]) = pixelDataSize;

		const uint8_t* src = static_cast<const uint8_t*>(mapped);
		for (uint32_t y = 0; y < h; ++y)
		{
			const uint8_t* srow = src + (size_t)y * footprint.Footprint.RowPitch;
			uint8_t* drow = bmp.data() + 54 + (size_t)y * rowPadded;
			for (uint32_t x = 0; x < w; ++x)
			{
				// Resource is R8G8B8A8 -- BMP wants B,G,R.
				drow[x * 3 + 0] = srow[x * 4 + 2];
				drow[x * 3 + 1] = srow[x * 4 + 1];
				drow[x * 3 + 2] = srow[x * 4 + 0];
			}
		}
		readback->Unmap(0, nullptr);

		FILE* f = nullptr;
		errno_t werr = _wfopen_s(&f, utf16Path, L"wb");
		if (werr != 0 || !f) { ERROR_PRINT("Dx12Device::CaptureBackBufferPng: fopen failed errno=%d", (int)werr); return false; }
		fwrite(bmp.data(), 1, bmp.size(), f);
		fclose(f);
		return true;
	}

	bool Dx12Device::ReadBackUintPixel4(TextureHandle texture, uint32_t x, uint32_t y, uint32_t outValues[4])
	{
		TextureRec* t = myTextures.Get(texture);
		if (!t || !t->res || t->res->GetDesc().Format != DXGI_FORMAT_R32G32B32A32_UINT) return false;
		return ReadBackPixel16(texture, x, y, outValues);
	}

	bool Dx12Device::ReadBackFloatPixel4(TextureHandle texture, uint32_t x, uint32_t y, float outValues[4])
	{
		TextureRec* t = myTextures.Get(texture);
		if (!t || !t->res || t->res->GetDesc().Format != DXGI_FORMAT_R32G32B32A32_FLOAT) return false;
		return ReadBackPixel16(texture, x, y, outValues);
	}

	bool Dx12Device::ReadBackPixel16(TextureHandle texture, uint32_t x, uint32_t y, void* outValues)
	{
		// Moved here from Viewport.cpp's MouseOver() (2026-09-12), which called
		// straight into raw D3D11 (GetShaderResourceView()->GetResource(...),
		// a null pointer on DX12 -- this was the actual, 100%-reproducible
		// cause of GameEditor terminating with no error/dump the moment the
		// mouse moved over the viewport under DX12). Same one-off synchronous
		// readback pattern as CaptureBackBufferPng above (its own comment
		// explains why: a dedicated upload command list + WaitForGpuIdle,
		// rather than the main per-frame list).
		TextureRec* t = myTextures.Get(texture);
		if (!t || !t->res) return false;

		D3D12_RESOURCE_DESC desc = t->res->GetDesc();
		if (x >= desc.Width || y >= desc.Height) return false;

		// D3D12_TEXTURE_DATA_PITCH_ALIGNMENT (256) -- the mandatory row-pitch
		// alignment for a buffer used as a texture-copy destination. One pixel
		// (16 bytes: 4x uint32) fits trivially inside a single aligned row.
		const UINT rowPitch = 256;

		if (!myPixelReadbackBuffer)
		{
			D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_READBACK };
			D3D12_RESOURCE_DESC bufDesc = {};
			bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			bufDesc.Width = rowPitch;
			bufDesc.Height = 1;
			bufDesc.DepthOrArraySize = 1;
			bufDesc.MipLevels = 1;
			bufDesc.SampleDesc.Count = 1;
			bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			HRESULT hr = myDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(myPixelReadbackBuffer.GetAddressOf()));
			if (FAILED(hr)) return false;
		}

		SubmitUploadList();
		myUploadAllocator->Reset();
		myUploadCmdList->Reset(myUploadAllocator.Get(), nullptr);

		const D3D12_RESOURCE_STATES before = t->state;
		D3D12_RESOURCE_BARRIER toSrc = {};
		toSrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		toSrc.Transition.pResource = t->res.Get();
		toSrc.Transition.StateBefore = before;
		toSrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		if (before != D3D12_RESOURCE_STATE_COPY_SOURCE)
			myUploadCmdList->ResourceBarrier(1, &toSrc);

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		footprint.Footprint.Format = desc.Format;
		footprint.Footprint.Width = 1;
		footprint.Footprint.Height = 1;
		footprint.Footprint.Depth = 1;
		footprint.Footprint.RowPitch = rowPitch;

		D3D12_TEXTURE_COPY_LOCATION dstLoc = { myPixelReadbackBuffer.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {} };
		dstLoc.PlacedFootprint = footprint;
		D3D12_TEXTURE_COPY_LOCATION srcLoc = { t->res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
		srcLoc.SubresourceIndex = 0;

		D3D12_BOX box = { x, y, 0, x + 1, y + 1, 1 };
		myUploadCmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &box);

		if (before != D3D12_RESOURCE_STATE_COPY_SOURCE)
		{
			D3D12_RESOURCE_BARRIER toOrig = toSrc;
			std::swap(toOrig.Transition.StateBefore, toOrig.Transition.StateAfter);
			myUploadCmdList->ResourceBarrier(1, &toOrig);
		}

		myUploadCmdList->Close();
		ID3D12CommandList* lists[] = { myUploadCmdList.Get() };
		myQueue->ExecuteCommandLists(1, lists);
		WaitForGpuIdle();

		D3D12_RANGE readRange = { 0, sizeof(uint32_t) * 4 };
		void* mapped = nullptr;
		if (FAILED(myPixelReadbackBuffer->Map(0, &readRange, &mapped))) return false;
		memcpy(outValues, mapped, 16);
		D3D12_RANGE writtenRange = { 0, 0 };
		myPixelReadbackBuffer->Unmap(0, &writtenRange);
		return true;
	}
}
