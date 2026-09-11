#include "stdafx.h"
#include "tge/rhi/dx12/Dx12Device.h"
#include "tge/rhi/dx12/Dx12CommandContext.h"
#include "tge/rhi/Format.h"
#include <tge/log/Log.h>
#include <d3dcompiler.h>
#include <cassert>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace Tga::rhi::dx12
{
	// ------------------------------------------------------------------ ctor/dtor
	Dx12Device::Dx12Device(const DeviceDesc& d)
	{
		myHwnd = static_cast<HWND>(d.nativeWindowHandle);
		myResolution = { d.width, d.height };

		CreateDeviceAndQueue(d.enableDebugLayer, d.enableGpuValidation);
		CreateHeaps();
		CreateFrameResources();
		if (myHwnd) CreateSwapchain(myHwnd, d.width, d.height);

		myContext = std::make_unique<Dx12CommandContext>(*this);

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

	// ------------------------------------------------------------------ device / queue
	void Dx12Device::CreateDeviceAndQueue(bool enableDebugLayer, bool enableGpuValidation)
	{
		UINT dxgiFlags = 0;
#if defined(_DEBUG)
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
				break;
			}
		}
		assert(myDevice && "Dx12Device: no D3D12-capable adapter found");

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
		myCbvSrvUavHeap.Init(myDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kCbvSrvUavCapacity, true);
		mySamplerHeap.Init(myDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerCapacity, true);

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

	void Dx12Device::CreateFrameResources()
	{
		for (uint32_t i = 0; i < kFramesInFlight; ++i)
		{
			HRESULT hr = myDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(myAllocators[i].GetAddressOf()));
			assert(SUCCEEDED(hr)); (void)hr;
		}
		HRESULT hr = myDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, myAllocators[0].Get(), nullptr, IID_PPV_ARGS(myCmdList.GetAddressOf()));
		assert(SUCCEEDED(hr));
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
		scd.Flags = 0;

		ComPtr<IDXGISwapChain1> sc1;
		HRESULT hr = myFactory->CreateSwapChainForHwnd(myQueue.Get(), hwnd, &scd, nullptr, nullptr, sc1.GetAddressOf());
		assert(SUCCEEDED(hr)); (void)hr;
		myFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
		hr = sc1.As(&mySwapChain);
		assert(SUCCEEDED(hr));

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
			myBackBufferRtv[i] = myRtvSlots.Alloc(std::move(slot));
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

		const uint64_t waitValue = myFenceValues[myFrameIndex];
		if (waitValue != 0 && myFence->GetCompletedValue() < waitValue)
		{
			myFence->SetEventOnCompletion(waitValue, myFenceEvent);
			WaitForSingleObject(myFenceEvent, INFINITE);
		}

		myDynCursor = 0;   // reset this frame's dynamic-constant ring

		myAllocators[myFrameIndex]->Reset();
		myCmdList->Reset(myAllocators[myFrameIndex].Get(), nullptr);

		ID3D12DescriptorHeap* heaps[] = { myCbvSrvUavHeap.Heap(), mySamplerHeap.Heap() };
		myCmdList->SetDescriptorHeaps(2, heaps);

		if (mySwapChain)
			myContext->TransitionResource(myBackBufferTex[myFrameIndex], ResourceState::RenderTarget);

		return *myContext;
	}

	void Dx12Device::EndFrame(bool vsync)
	{
		if (mySwapChain)
			myContext->TransitionResource(myBackBufferTex[myFrameIndex], ResourceState::Present);

		myCmdList->Close();
		ID3D12CommandList* lists[] = { myCmdList.Get() };
		myQueue->ExecuteCommandLists(1, lists);

		if (mySwapChain)
			mySwapChain->Present(vsync ? 1 : 0, 0);

		const uint64_t v = myNextFenceValue++;
		myQueue->Signal(myFence.Get(), v);
		myFenceValues[myFrameIndex] = v;
	}

	ICommandContext& Dx12Device::GetContext() { return *myContext; }

	bool Dx12Device::Resize(uint32_t w, uint32_t h)
	{
		if (!mySwapChain || w == 0 || h == 0) return false;
		WaitForGpuIdle();

		for (uint32_t i = 0; i < kFramesInFlight; ++i)
		{
			if (uint32_t* slot = myRtvSlots.Get(myBackBufferRtv[i])) myRtvHeap.Free(*slot);
			myRtvSlots.Free(myBackBufferRtv[i]);
			myTextures.Free(myBackBufferTex[i]);
		}
		if (myDepthDsv.IsValid()) { Destroy(myDepthDsv); myDepthDsv = {}; }
		if (myDepthTex.IsValid()) { Destroy(myDepthTex); myDepthTex = {}; }

		myResolution = { w, h };
		HRESULT hr = mySwapChain->ResizeBuffers(kFramesInFlight, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
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

	// ------------------------------------------------------------------ resources
	BufferHandle Dx12Device::CreateBuffer(const BufferDesc& d, const void* initialData)
	{
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

		D3D12_HEAP_PROPERTIES heapProps = { heapType };
		ComPtr<ID3D12Resource> res;
		HRESULT hr = myDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rd, initState, nullptr, IID_PPV_ARGS(res.GetAddressOf()));
		if (FAILED(hr)) { ERROR_PRINT("Dx12Device::CreateBuffer: CreateCommittedResource failed 0x%08X", (unsigned)hr); return {}; }

		if (initialData && d.memory == MemoryType::Default)
			UploadBufferData(res.Get(), initialData, d.byteSize);
		else if (initialData && d.memory == MemoryType::Upload)
		{
			void* mapped = nullptr;
			D3D12_RANGE noRead{ 0, 0 };
			if (SUCCEEDED(res->Map(0, &noRead, &mapped)))
			{
				memcpy(mapped, initialData, d.byteSize);
				res->Unmap(0, nullptr);
			}
		}

		BufferRec rec; rec.res = res; rec.desc = d; rec.state = initState;
		return myBuffers.Alloc(std::move(rec));
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
			clearValue.Format = ToDxgi(d.format);
			pClear = &clearValue;
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

		D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
		ComPtr<ID3D12Resource> res;
		HRESULT hr = myDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rd, initState, pClear, IID_PPV_ARGS(res.GetAddressOf()));
		if (FAILED(hr)) { ERROR_PRINT("Dx12Device::CreateTexture: CreateCommittedResource failed 0x%08X", (unsigned)hr); return {}; }

		if (initial && initialCount)
			UploadTextureData(res.Get(), d, initial, initialCount);

		TextureRec rec; rec.res = res; rec.desc = d; rec.state = initState;
		return myTextures.Alloc(std::move(rec));
	}

	void Dx12Device::UploadBufferData(ID3D12Resource* dst, const void* data, size_t size)
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
		memcpy(mapped, data, size);
		upload->Unmap(0, nullptr);

		myUploadAllocator->Reset();
		myUploadCmdList->Reset(myUploadAllocator.Get(), nullptr);
		myUploadCmdList->CopyBufferRegion(dst, 0, upload.Get(), 0, size);
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = dst;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		myUploadCmdList->ResourceBarrier(1, &b);
		myUploadCmdList->Close();

		ID3D12CommandList* lists[] = { myUploadCmdList.Get() };
		myQueue->ExecuteCommandLists(1, lists);
		WaitForGpuIdle();   // simple + correct; batching/async is a later optimization
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

		myUploadAllocator->Reset();
		myUploadCmdList->Reset(myUploadAllocator.Get(), nullptr);
		for (uint32_t i = 0; i < count; ++i)
		{
			D3D12_TEXTURE_COPY_LOCATION dstLoc = { dst, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
			dstLoc.SubresourceIndex = i;
			D3D12_TEXTURE_COPY_LOCATION srcLoc = { upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {} };
			srcLoc.PlacedFootprint = footprints[i];
			myUploadCmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
		}
		const bool rt = HasBind(d.bind, TextureBind::RenderTarget);
		const bool ds = HasBind(d.bind, TextureBind::DepthStencil);
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = dst;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		b.Transition.StateAfter = rt ? D3D12_RESOURCE_STATE_RENDER_TARGET : ds ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		myUploadCmdList->ResourceBarrier(1, &b);
		myUploadCmdList->Close();

		ID3D12CommandList* lists[] = { myUploadCmdList.Get() };
		myQueue->ExecuteCommandLists(1, lists);
		WaitForGpuIdle();
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
		else if (d.asCube || t->desc.dimension == TextureDimension::TexCube)
		{
			vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			vd.TextureCube.MostDetailedMip = d.mostDetailedMip;
			vd.TextureCube.MipLevels = mips;
		}
		else if (t->desc.dimension == TextureDimension::Tex2DArray)
		{
			vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			vd.Texture2DArray.MostDetailedMip = d.mostDetailedMip;
			vd.Texture2DArray.MipLevels = mips;
			vd.Texture2DArray.FirstArraySlice = d.firstArraySlice;
			vd.Texture2DArray.ArraySize = (d.arraySize == kAllSlices) ? t->desc.depthOrArraySize : d.arraySize;
		}
		else
		{
			vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			vd.Texture2D.MostDetailedMip = d.mostDetailedMip;
			vd.Texture2D.MipLevels = mips;
		}

		uint32_t slot = myCbvSrvUavHeap.Allocate();
		myDevice->CreateShaderResourceView(t->res.Get(), &vd, myCbvSrvUavHeap.Cpu(slot));
		return mySrvSlots.Alloc(std::move(slot));
	}

	SrvHandle Dx12Device::CreateSrv(BufferHandle h, const SrvDesc& d)
	{
		BufferRec* b = myBuffers.Get(h);
		if (!b || !b->res) return {};
		D3D12_SHADER_RESOURCE_VIEW_DESC vd = {};
		vd.Format = DXGI_FORMAT_UNKNOWN;
		vd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		vd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		vd.Buffer.FirstElement = d.bufferFirstElement;
		vd.Buffer.NumElements = d.bufferNumElements ? d.bufferNumElements : (b->desc.stride ? (UINT)(b->desc.byteSize / b->desc.stride) : (UINT)b->desc.byteSize);
		vd.Buffer.StructureByteStride = b->desc.stride;
		vd.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		uint32_t slot = myCbvSrvUavHeap.Allocate();
		myDevice->CreateShaderResourceView(b->res.Get(), &vd, myCbvSrvUavHeap.Cpu(slot));
		return mySrvSlots.Alloc(std::move(slot));
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
		return myUavSlots.Alloc(std::move(slot));
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
		return myUavSlots.Alloc(std::move(slot));
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
		return myRtvSlots.Alloc(std::move(slot));
	}

	DsvHandle Dx12Device::CreateDsv(TextureHandle h, const DsvDesc& d)
	{
		TextureRec* t = myTextures.Get(h);
		if (!t || !t->res) return {};
		D3D12_DEPTH_STENCIL_VIEW_DESC vd = {};
		vd.Format = d.formatOverride != Format::Unknown ? ToDsvFormat(d.formatOverride) : ToDsvFormat(t->desc.format);
		vd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		vd.Texture2D.MipSlice = d.mipSlice;
		if (d.readOnly) vd.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;

		uint32_t slot = myDsvHeap.Allocate();
		myDevice->CreateDepthStencilView(t->res.Get(), &vd, myDsvHeap.Cpu(slot));
		return myDsvSlots.Alloc(std::move(slot));
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
		ShaderRec rec;
		rec.kind = kind;
		rec.bytecode.assign(static_cast<const uint8_t*>(bytecode), static_cast<const uint8_t*>(bytecode) + size);
		return myShaders.Alloc(std::move(rec));
	}

	GraphicsPipelineHandle Dx12Device::CreateGraphicsPipeline(const GraphicsPipelineDesc&)
	{
		assert(false && "Dx12: milestone 2 (needs the shared root signature)");
		return {};
	}

	ComputePipelineHandle Dx12Device::CreateComputePipeline(const ComputePipelineDesc&)
	{
		assert(false && "Dx12: milestone 2 (needs the shared root signature)");
		return {};
	}

	// ------------------------------------------------------------------ destroy
	void Dx12Device::Destroy(BufferHandle h)  { myBuffers.Free(h); }
	void Dx12Device::Destroy(TextureHandle h) { myTextures.Free(h); }
	void Dx12Device::Destroy(SrvHandle h)     { if (uint32_t* s = mySrvSlots.Get(h)) { myCbvSrvUavHeap.Free(*s); mySrvSlots.Free(h); } }
	void Dx12Device::Destroy(UavHandle h)     { if (uint32_t* s = myUavSlots.Get(h)) { myCbvSrvUavHeap.Free(*s); myUavSlots.Free(h); } }
	void Dx12Device::Destroy(RtvHandle h)     { if (uint32_t* s = myRtvSlots.Get(h)) { myRtvHeap.Free(*s); myRtvSlots.Free(h); } }
	void Dx12Device::Destroy(DsvHandle h)     { if (uint32_t* s = myDsvSlots.Get(h)) { myDsvHeap.Free(*s); myDsvSlots.Free(h); } }
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

	// ------------------------------------------------------------------ timestamps (milestone 2)
	TimestampQueryHandle Dx12Device::CreateTimestampQuery()
	{
		TimestampRec rec;
		rec.queryIndex = 0;   // real allocation is milestone 2 (needs WriteTimestampBegin/End wired up)
		return myTimestamps.Alloc(std::move(rec));
	}
	void Dx12Device::DestroyTimestampQuery(TimestampQueryHandle h) { myTimestamps.Free(h); }
	bool Dx12Device::GetTimestampMs(TimestampQueryHandle, double&) { return false; }

	// ------------------------------------------------------------------ Stage-1-only bridges (DX12 never uses these)
	void* Dx12Device::GetNativeDevice()  { assert(false && "Dx12: GetNativeDevice is a DX11/imgui_impl_dx11-only bridge"); return nullptr; }
	void* Dx12Device::GetNativeContext() { assert(false && "Dx12: GetNativeContext is a DX11/imgui_impl_dx11-only bridge"); return nullptr; }
	void* Dx12Device::GetNativeSrv(SrvHandle)     { assert(false && "Dx12: GetNativeSrv is a DX11-only legacy-interop bridge"); return nullptr; }
	void* Dx12Device::GetNativeRtv(RtvHandle)     { assert(false && "Dx12: GetNativeRtv is a DX11-only legacy-interop bridge"); return nullptr; }
	void* Dx12Device::GetNativeTexture(TextureHandle) { assert(false && "Dx12: GetNativeTexture is a DX11-only legacy-interop bridge"); return nullptr; }
	void* Dx12Device::ImGuiTextureId(SrvHandle h)
	{
		// imgui_impl_dx12 expects a GPU descriptor handle (as a UINT64), not a
		// raw view pointer -- real support is milestone 2 alongside the
		// imgui_impl_dx11 -> imgui_impl_dx12 swap.
		uint32_t* slot = mySrvSlots.Get(h);
		assert(slot && "Dx12: ImGuiTextureId milestone 2");
		return slot ? reinterpret_cast<void*>(myCbvSrvUavHeap.Gpu(*slot).ptr) : nullptr;
	}
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
}
