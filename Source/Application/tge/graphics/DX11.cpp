#include "stdafx.h"
#include <tge/graphics/DX11.h>
#include <tge/rhi/Device.h>
#include <tge/log/Log.h>

#include <fstream>
#include <tge/windows/WindowsWindow.h>
#include <nlohmann/json.hpp>
#include <tge/util/StringCast.h>

#ifndef _RETAIL

#include <tge/filewatcher/FileWatcher.h>

#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")

#endif

using namespace Tga;

#define REPORT_DX_WARNINGS

ID3D11Device* DX11::Device;
ID3D11DeviceContext* DX11::Context;
IDXGISwapChain* DX11::SwapChain;
RenderTarget* DX11::BackBuffer;
RenderTarget* DX11::BackBufferNoSrgbConversion;
DepthBuffer* DX11::DepthBuffer;

WindowsWindow* DX11::ourWindowHandler;

bool DX11::ourIsCreated;
std::thread::id DX11::ourRenderThreadId;
int DX11::ourDrawCallCount = 0;
int DX11::ourPreviousDrawCallCount = 0;

std::unordered_map<StringId, PixelShader> DX11::ourLoadedPixelShaders;
std::unordered_map<StringId, VertexShader> DX11::ourLoadedVertexShaders;
std::unordered_map<StringId, ComputeShader> DX11::ourLoadedComputeShaders;

std::unique_ptr<rhi::IDevice> DX11::ourRhiDevice;

DX11::DX11()
{
	assert(ourIsCreated == false);
	ourIsCreated = true;

	Device = nullptr;
	Context = nullptr;
	SwapChain = nullptr;
	BackBuffer = nullptr;
	BackBufferNoSrgbConversion = nullptr;
	DepthBuffer = nullptr;
}

DX11::~DX11()
{
	assert(ourIsCreated == true);
	ourIsCreated = false;

	// Release RHI-held views before the raw device goes away.
	ourRhiDevice.reset();

#ifdef REPORT_DX_WARNINGS
	if (myD3dDebug)
	{
		myD3dDebug->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL);
	}
#endif

	Device = nullptr;
	Context = nullptr;
	SwapChain = nullptr;
	BackBuffer = nullptr;
	BackBufferNoSrgbConversion = nullptr;
	DepthBuffer = nullptr;
}

IDXGIAdapter* FindBestAdapter()
{
	HRESULT result = S_OK;
	IDXGIFactory* factory;

	DXGI_MODE_DESC* displayModeList = nullptr;

	result = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory);
	if (FAILED(result))
	{
		return nullptr;
	}
	// Use the factory to create an adapter for the primary graphics interface (video card).
	IDXGIAdapter* usingAdapter = nullptr;
	int adapterIndex = 0;
	std::vector<DXGI_ADAPTER_DESC> myAdapterDescs;
	std::vector<IDXGIAdapter*> myAdapters;
	while (factory->EnumAdapters(adapterIndex, &usingAdapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC adapterDesc;
		usingAdapter->GetDesc(&adapterDesc);
		myAdapterDescs.push_back(adapterDesc);
		myAdapters.push_back(usingAdapter);
		++adapterIndex;
	}

	if (adapterIndex == 0)
	{
		return nullptr;
	}

	INFO_PRINT("%s", "Video card(s) detected: ");
	for (DXGI_ADAPTER_DESC desc : myAdapterDescs)
	{
		int memory = (int)(desc.DedicatedVideoMemory / 1024 / 1024);
		INFO_PRINT("	%ls%s%i%s", desc.Description, " Mem: ", memory, "Mb");
		memory;
	}

	DXGI_ADAPTER_DESC usingAdapterDesc = myAdapterDescs[0];
	usingAdapter = myAdapters[0];

	INFO_PRINT("%s", "Detecting best card...");


	const std::wstring nvidia = L"NVIDIA";
	const std::wstring ati = L"ATI";

	int memory = (int)(usingAdapterDesc.DedicatedVideoMemory / 1024 / 1024);
	int mostMem = 0;

	for (unsigned int i = 0; i < myAdapterDescs.size(); i++)
	{
		DXGI_ADAPTER_DESC desc = myAdapterDescs[i];
		memory = (int)(desc.DedicatedVideoMemory / 1024 / 1024);
		std::wstring name = desc.Description;
		if (name.find(nvidia) != std::wstring::npos || name.find(ati) != std::wstring::npos)
		{
			if (memory > mostMem)
			{
				mostMem = memory;
				usingAdapterDesc = desc;
				usingAdapter = myAdapters[i];
			}
		}
	}

	INFO_PRINT("%s%ls%s%i", "Using graphic card: ", usingAdapterDesc.Description, " Dedicated Mem: ", mostMem);

	// Get the adapter (video card) description.
	result = usingAdapter->GetDesc(&usingAdapterDesc);
	if (FAILED(result))
	{
		return nullptr;
	}

	// Release the display mode list.
	delete[] displayModeList;
	displayModeList = 0;

	// Release the factory.
	factory->Release();
	factory = 0;

	return usingAdapter;
}


bool DX11::Init(WindowsWindow* aWindowHandler)
{
	ourWindowHandler = aWindowHandler;

	// Backend selection: TGE_RHI=dx12 opts into the DX12 backend (Stage 2, in
	// progress); anything else (unset included) keeps the shipping DX11 path
	// below untouched. No CLI-flag plumbing exists yet, so this follows the
	// codebase's existing BENCH_* env-var convention.
	{
		const char* rhiEnv = std::getenv("TGE_RHI");
		if (rhiEnv && _stricmp(rhiEnv, "dx12") == 0)
			return InitDx12(aWindowHandler);
	}

	HRESULT result;

	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = 1;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = ourWindowHandler->GetWindowHandle();
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.Windowed = true;

	UINT creationFlags = 0;
#if defined(_DEBUG)
#if defined(REPORT_DX_WARNINGS)
	creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
#endif

	IDXGIAdapter* adapter = FindBestAdapter();
	if (!adapter)
	{
		return false;
	}

	result = D3D11CreateDeviceAndSwapChain(
		adapter,
		D3D_DRIVER_TYPE_UNKNOWN,
		nullptr,
		creationFlags,
		nullptr,
		0,
		D3D11_SDK_VERSION,
		&swapChainDesc,
		&mySwapChain,
		&myDevice,
		nullptr,
		&myContext
	);

#if defined(_DEBUG)
#if defined(REPORT_DX_WARNINGS)
	if (FAILED(result))
	{
		ERROR_PRINT("%s", "Device could not create itself in debug mode, trying without debug layer... If you have Win10, try this:  Settings panel -> System -> Apps & features -> Manage optional Features -> Add a feature -> Select ""Graphics Tools""");
		creationFlags = 0;

		INFO_PRINT("%s", "Creating device without debug layer");
		result = D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			creationFlags,
			nullptr,
			0,
			D3D11_SDK_VERSION,
			&swapChainDesc,
			&mySwapChain,
			&myDevice,
			nullptr,
			&myContext
		);
	}
#endif
#endif

	if (FAILED(result))
	{
		return false;
	}

	myD3dDebug = nullptr;
#ifdef _DEBUG
	if (SUCCEEDED(myDevice.As(&myD3dDebug)))
	{
		ComPtr<ID3D11InfoQueue> d3dInfoQueue = nullptr;
		if (SUCCEEDED(myD3dDebug.As(&d3dInfoQueue)))
		{
			d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
			d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, false);

			D3D11_MESSAGE_ID hide[] =
			{
				D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,
				// Add more message IDs here as needed
			};

			D3D11_INFO_QUEUE_FILTER filter;
			memset(&filter, 0, sizeof(filter));
			filter.DenyList.NumIDs = _countof(hide);
			filter.DenyList.pIDList = hide;
			d3dInfoQueue->AddStorageFilterEntries(&filter);
		}
	}
#endif

	Device = myDevice.Get();
	Context = myContext.Get();
	SwapChain = mySwapChain.Get();

	ID3D11Texture2D* backBufferTexture;
	result = mySwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBufferTexture);
	myBackBuffer = RenderTarget::Create(backBufferTexture, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
	myBackBufferNoSrgbConversion = RenderTarget::Create(backBufferTexture);
	D3D11_TEXTURE2D_DESC textureDesc;
	backBufferTexture->GetDesc(&textureDesc);
	backBufferTexture->Release();
	myDepthBuffer = DepthBuffer::Create({ static_cast<unsigned int>(textureDesc.Width), static_cast<unsigned int>(textureDesc.Height) });

	myBackBuffer.SetAsActiveTarget(&myDepthBuffer);

	BackBuffer = &myBackBuffer;
	BackBufferNoSrgbConversion = &myBackBufferNoSrgbConversion;
	DepthBuffer = &myDepthBuffer;

	ourRenderThreadId = std::this_thread::get_id();

	// Bring up the RHI seam over the just-created device. It adopts the statics
	// above; the raw path stays live until every call site is migrated.
	{
		rhi::DeviceDesc dd = {};
		dd.width = (uint32_t)textureDesc.Width;
		dd.height = (uint32_t)textureDesc.Height;
		dd.framesInFlight = 2;
		ourRhiDevice = rhi::CreateDevice(rhi::Backend::DX11, dd);
	}

	return true;
}

bool DX11::InitDx12(WindowsWindow* aWindowHandler)
{
	ourWindowHandler = aWindowHandler;

	uint32_t width = aWindowHandler->GetWidth();
	uint32_t height = aWindowHandler->GetHeight();

	rhi::DeviceDesc dd = {};
	dd.nativeWindowHandle = aWindowHandler->GetWindowHandle();
	dd.width = width;
	dd.height = height;
	dd.framesInFlight = 2;
#if defined(_DEBUG)
	dd.enableDebugLayer = true;
#endif

	ourRhiDevice = rhi::CreateDevice(rhi::Backend::DX12, dd);
	if (!ourRhiDevice)
	{
		ERROR_PRINT("%s", "Failed to create DX12 device");
		return false;
	}

	// No raw D3D11 device/context/swapchain under DX12 -- every remaining
	// caller of these statics must go through the RHI (DX11::Rhi()) instead.
	Device = nullptr;
	Context = nullptr;
	SwapChain = nullptr;

	myBackBuffer = RenderTarget::CreateFromDeviceBackBuffer(true, { width, height });
	myBackBufferNoSrgbConversion = RenderTarget::CreateFromDeviceBackBuffer(false, { width, height });
	myDepthBuffer = Tga::DepthBuffer::Create({ width, height });

	BackBuffer = &myBackBuffer;
	BackBufferNoSrgbConversion = &myBackBufferNoSrgbConversion;
	DepthBuffer = &myDepthBuffer;

	ourRenderThreadId = std::this_thread::get_id();

	// Unlike the DX11 path, deliberately skip SetAsActiveTarget() here: no
	// command list has started recording yet (that happens on the first
	// BeginFrame(), which binds the root signatures/descriptor heaps via
	// Dx12Device::BeginFrame -> Dx12CommandContext::OnBeginFrame). BeginFrame()
	// below unconditionally re-binds the backbuffer as the active target on
	// every frame anyway, so nothing is lost.

	return true;
}

bool DX11::ResizeToWindowSize()
{
	if (ourRhiDevice && ourRhiDevice->GetBackend() == rhi::Backend::DX12)
		return ResizeToWindowSizeDx12();

	ID3D11RenderTargetView* nullViews[] = { nullptr };
	myContext->OMSetRenderTargets(ARRAYSIZE(nullViews), nullViews, nullptr);
	myContext->OMSetDepthStencilState(0, 0);
	myContext->ClearState();
	
	if (!mySwapChain)
	{
		return false;
	}

	myBackBuffer = RenderTarget();
	myBackBufferNoSrgbConversion = RenderTarget();
	myDepthBuffer = Tga::DepthBuffer();

	// The RHI adopted the old backbuffer views; drop them before ResizeBuffers
	// (which fails while any swapchain buffer reference is outstanding).
	if (ourRhiDevice) ourRhiDevice->Resize(0, 0);

	myContext->Flush();
	if (mySwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0) != S_OK)
	{
		ERROR_PRINT("%s", "Could not resize buffers!");
		return false;
	}

	BackBuffer = nullptr;
	DepthBuffer = nullptr;

	ID3D11Texture2D* backBufferTexture = nullptr;
	if (mySwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBufferTexture) != S_OK)
	{
		ERROR_PRINT("%s", "Could not resize buffers!");
		return false;
	}

	if (!backBufferTexture)
	{
		return false;
	}

	myBackBuffer = RenderTarget::Create(backBufferTexture, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
	myBackBufferNoSrgbConversion = RenderTarget::Create(backBufferTexture);

	D3D11_TEXTURE2D_DESC textureDesc;
	backBufferTexture->GetDesc(&textureDesc);
	backBufferTexture->Release();
	myDepthBuffer = DepthBuffer::Create({ static_cast<unsigned int>(textureDesc.Width), static_cast<unsigned int>(textureDesc.Height) });
	myBackBuffer.SetAsActiveTarget(&myDepthBuffer);

	BackBuffer = &myBackBuffer;
	BackBufferNoSrgbConversion = &myBackBufferNoSrgbConversion;
	DepthBuffer = &myDepthBuffer;

	if (ourRhiDevice) ourRhiDevice->Resize((uint32_t)textureDesc.Width, (uint32_t)textureDesc.Height);

	return true;
}

bool DX11::ResizeToWindowSizeDx12()
{
	if (!ourWindowHandler) return false;

	uint32_t width = ourWindowHandler->GetWidth();
	uint32_t height = ourWindowHandler->GetHeight();

	// The backbuffer wrappers hold no owned DX12 resources -- myIsDx12BackBuffer
	// mode re-resolves the device's *current* backbuffer view every call (see
	// RenderTarget::GetRtv) -- so nothing needs releasing there before the
	// swapchain resizes. The depth buffer DOES own a real DX12 texture sized
	// for the old resolution; drop it first so its MigrationView members
	// release the old handles before we ask the device to resize.
	myDepthBuffer = Tga::DepthBuffer();

	if (!ourRhiDevice || !ourRhiDevice->Resize(width, height))
	{
		ERROR_PRINT("%s", "Could not resize DX12 swapchain!");
		return false;
	}

	myBackBuffer = RenderTarget::CreateFromDeviceBackBuffer(true, { width, height });
	myBackBufferNoSrgbConversion = RenderTarget::CreateFromDeviceBackBuffer(false, { width, height });
	myDepthBuffer = Tga::DepthBuffer::Create({ width, height });

	BackBuffer = &myBackBuffer;
	BackBufferNoSrgbConversion = &myBackBufferNoSrgbConversion;
	DepthBuffer = &myDepthBuffer;

	return true;
}

void DX11::BeginFrame(Color aClearColor)
{
	if (ourRhiDevice) ourRhiDevice->BeginFrame();   // resets the RHI dynamic-constant ring

	myBackBuffer.Clear(aClearColor.AsLinearVec4());
	myDepthBuffer.Clear(1.0f, 0);

	DX11::BackBuffer->SetAsActiveTarget();
}

void DX11::EndFrame(bool aEnableVSync)
{
	// DX12's Dx12Device::EndFrame does the real present (it owns the swapchain);
	// DX11::SwapChain is null under that backend, so presenting through it here
	// would be a null-pointer dereference. The DX11 backend's own EndFrame is a
	// no-op present, since DX11::SwapChain->Present below already did the work.
	if (ourRhiDevice && ourRhiDevice->GetBackend() == rhi::Backend::DX12)
	{
		ourRhiDevice->EndFrame(aEnableVSync);
		return;
	}

	if (aEnableVSync)
	{
		DX11::SwapChain->Present(1, 0);
	}
	else
	{
		DX11::SwapChain->Present(0, 0);
	}

	if (ourRhiDevice) ourRhiDevice->EndFrame(aEnableVSync);   // no-op present on the DX11 backend; advances frame state
}

bool DX11::IsOnSameThreadAsEngine()
{
	return ourRenderThreadId == std::this_thread::get_id();
}

Vector2ui DX11::GetResolution()
{
	return BackBuffer->GetResolution();
}

#ifndef _RETAIL

std::string GetFileTimestamp(std::string_view filePath)
{
	std::filesystem::file_time_type fileTime = std::filesystem::last_write_time(filePath);
	auto systemTimePoint = std::chrono::clock_cast<std::chrono::system_clock>(fileTime);
	auto fileSystemTime = std::chrono::system_clock::to_time_t(systemTimePoint);

	std::ostringstream oss;
	oss << std::put_time(std::gmtime(&fileSystemTime), "%Y-%m-%dT%H:%M:%SZ");
	return oss.str();
};

// Function to check if a file has changed based on its recorded timestamp
bool HasFileChanged(std::string_view filePath, const std::string& recordedTimestamp)
{
	if (!std::filesystem::exists(filePath))
	{
		return true; // Assume changed if the file does not exist
	}

	auto fileTime = std::filesystem::last_write_time(filePath);
	auto systemTimePoint = std::chrono::clock_cast<std::chrono::system_clock>(fileTime);
	auto fileSystemTime = std::chrono::system_clock::to_time_t(systemTimePoint);

	if (recordedTimestamp.empty())
	{
		return true; // Assume changed if no recorded timestamp
	}

	// Parse the recorded timestamp (ISO8601 format)
	std::tm recordedTime = {};
	std::istringstream ss(recordedTimestamp);
	ss >> std::get_time(&recordedTime, "%Y-%m-%dT%H:%M:%SZ");

	return std::difftime(fileSystemTime, _mkgmtime(&recordedTime)) != 0;
}


struct IncludeHandler : public ID3DInclude
{
	std::string sourcePath;
	std::set<std::string> includedFiles;

	HRESULT Open(
		D3D_INCLUDE_TYPE,
		LPCSTR pFileName,
		LPCVOID,
		LPCVOID* ppData,
		UINT* pBytes) override
	{
		FilePathStream relStream;
		relStream << sourcePath << "/" << pFileName;
		FilePathStream resolvedStream;
		if (!Settings::ResolveAssetPath(relStream.GetStringView(), resolvedStream))
		{
			if (!Settings::ResolveAssetPath(pFileName, resolvedStream))
			{
				ERROR_PRINT("Failed to open shader include file: %s", pFileName);
				return E_FAIL;
			}
		}

		std::ifstream file(resolvedStream.GetData(), std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			ERROR_PRINT("Failed to open shader include file: %s", pFileName);
			return E_FAIL;
		}

		includedFiles.insert(pFileName);

		std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);

		char* buffer = new char[static_cast<size_t>(size)];
		if (!file.read(buffer, size))
		{
			delete[] buffer;
			ERROR_PRINT("Failed to read shader include file: %s", pFileName);
			return E_FAIL;
		}

		*ppData = buffer;
		*pBytes = static_cast<UINT>(size);

		return S_OK;
	}

	HRESULT Close(LPCVOID pData) override
	{
		delete[] static_cast<const char*>(pData);
		return S_OK;
	}
};

const char* GetShaderModel(DX11::ShaderType aShaderType)
{
	switch (aShaderType)
	{
	case DX11::ShaderType::Pixel:
		return "ps_5_0";
	case DX11::ShaderType::Vertex:
		return "vs_5_0";
	case DX11::ShaderType::Compute:
		return "cs_5_0";
	default:
		return "";
	}
}


bool CompileShaderIfChanged(std::string_view shaderWithoutExtension, std::string_view aRelShaderFile, std::string_view aAbsShaderFile, std::string_view aBuildDependencyFile, std::string_view aBinaryOutputFile, DX11::ShaderType aShaderType, bool addFileWatcher = true)
{
	std::string shaderRelBasePath = std::filesystem::path(aRelShaderFile).parent_path().string();

	bool needsRecompile = true;
	std::string buildDepStr(aBuildDependencyFile);
	if (std::filesystem::exists(buildDepStr))
	{
		std::ifstream inputFile(buildDepStr);
		if (!inputFile.is_open())
		{
			ERROR_PRINT("Failed to open shader build dependency file.");
			return false;
		}

		nlohmann::json buildDependencyData;
		inputFile >> buildDependencyData;

		// Check if the shader or any dependencies have changed
		needsRecompile = HasFileChanged(aAbsShaderFile, buildDependencyData["timestamp"]);

		for (const auto& dependency : buildDependencyData["dependencies"].items())
		{
			const std::string& depFile = dependency.key();
			const std::string& depTimestamp = dependency.value();

			FilePathStream depRelStream;
			depRelStream << shaderRelBasePath << "/" << depFile;
			FilePathStream resolvedStream;
			if (!Settings::ResolveAssetPath(depRelStream.GetStringView(), resolvedStream))
			{
				Settings::ResolveAssetPath(depFile, resolvedStream);
			}
			if (HasFileChanged(resolvedStream.GetStringView(), depTimestamp))
			{
				needsRecompile = true;
				break;
			}
		}
	}

	bool compilationSucceded = true;
	if (needsRecompile)
	{
		// Recompile the shader
		ID3DBlob* shaderBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;
		IncludeHandler includeHandler;

		includeHandler.sourcePath = shaderRelBasePath;
		
		unsigned int flags = D3DCOMPILE_DEBUG | D3DCOMPILE_ENABLE_STRICTNESS;

		std::wstring wFilename = string_cast<std::wstring>(std::string(aAbsShaderFile));
		HRESULT hr = D3DCompileFromFile(
			wFilename.c_str(),
			nullptr, &includeHandler,
			"main",
			GetShaderModel(aShaderType),
			flags, 0,
			&shaderBlob, &errorBlob);

		if (FAILED(hr))
		{
			if (errorBlob)
			{
				ERROR_PRINT("Shader compilation error: %s", (char*)errorBlob->GetBufferPointer());
				errorBlob->Release();
			}
			if (shaderBlob)
			{
				shaderBlob->Release();
			}
			compilationSucceded = false;
		}
		else
		{
			// Save the compiled shader to a binary file (CSO)
			std::filesystem::path parentPath = std::filesystem::path(aBinaryOutputFile).parent_path();
			std::filesystem::create_directories(parentPath);

			std::string binaryOutputStr(aBinaryOutputFile);
			std::ofstream binaryFile(binaryOutputStr, std::ios::binary);
			if (!binaryFile.is_open())
			{
				ERROR_PRINT("Failed to save compiled shader to binary file.");
				shaderBlob->Release();
				return false;
			}
			binaryFile.write(reinterpret_cast<const char*>(shaderBlob->GetBufferPointer()), shaderBlob->GetBufferSize());
			binaryFile.close();

			// Release the shader blob
			shaderBlob->Release();

			{
				nlohmann::json buildDependencyData;
				buildDependencyData["timestamp"] = GetFileTimestamp(aAbsShaderFile);

				for (auto& dependency : includeHandler.includedFiles)
				{
					FilePathStream depRelStream;
					depRelStream << shaderRelBasePath << "/" << dependency;
					FilePathStream resolvedStream;
					if (!Settings::ResolveAssetPath(depRelStream.GetStringView(), resolvedStream))
					{
						Settings::ResolveAssetPath(dependency, resolvedStream);
					}
					buildDependencyData["dependencies"][dependency] = GetFileTimestamp(resolvedStream.GetStringView());
				}

				// Save the updated JSON file
				std::ofstream outputFile(buildDepStr);
				if (!outputFile.is_open())
				{
					ERROR_PRINT("Failed to save shader build dependency file.");
					return false;
				}
				outputFile << buildDependencyData.dump(4);
				outputFile.close();
			}
		}
	}

	if (addFileWatcher)
	{
		StringId shaderId = StringRegistry::RegisterOrGetString(shaderWithoutExtension);
		Application::GetInstance()->GetFileWatcher()->WatchFileChange(std::string(aAbsShaderFile), [shaderId, aShaderType]() {
			DX11::OnShaderFileChanged(shaderId, aShaderType);
			});
				}

	return compilationSucceded;
	}

void DX11::OnShaderFileChanged(StringId aShaderPath, ShaderType aShaderType)
{
	switch (aShaderType)
	{
	case ShaderType::Pixel:
		ForceLoadPixelShader(aShaderPath.GetString(), false);
		break;
	case ShaderType::Vertex:
		ForceLoadVertexShader(aShaderPath.GetString(), false);
		break;
	case ShaderType::Compute:
		ForceLoadComputeShader(aShaderPath.GetString(), false);
		break;
	default:
		break;
	}
}

#endif // #ifndef _RETAIL

const PixelShader* DX11::LoadPixelShader(const char* aShaderPath)
{
	StringId id = StringRegistry::RegisterOrGetString(aShaderPath);
	auto it = ourLoadedPixelShaders.find(id);
	if (it != ourLoadedPixelShaders.end())
		return &(it->second);

	return ForceLoadPixelShader(aShaderPath);
}

const VertexShader* DX11::LoadVertexShader(const char* aShaderPath)
{
	StringId id = StringRegistry::RegisterOrGetString(aShaderPath);
	auto it = ourLoadedVertexShaders.find(id);
	if (it != ourLoadedVertexShaders.end())
		return &(it->second);
	return ForceLoadVertexShader(aShaderPath);
}

const ComputeShader* DX11::LoadComputeShader(const char* aShaderPath)
{
	StringId id = StringRegistry::RegisterOrGetString(aShaderPath);
	auto it = ourLoadedComputeShaders.find(id);
	if (it != ourLoadedComputeShaders.end())
		return &(it->second);
	return ForceLoadComputeShader(aShaderPath);
}

const PixelShader* DX11::ForceLoadPixelShader(const char* aShaderPath, bool addToFileWatcher)
{
	FilePathStream csoStream;
	csoStream << Settings::CookedAssetRoot() << "/" << aShaderPath << ".cso";
	csoStream.NormalizePath();

	addToFileWatcher; // needed to not cause unreferenced parameter in retail
#ifndef _RETAIL
	FilePathStream relHlslStream;
	relHlslStream << aShaderPath << ".hlsl";

	FilePathStream hlslResolvedStream;
	if (Settings::ResolveAssetPath(relHlslStream.GetStringView(), hlslResolvedStream))
	{
		FilePathStream buildInfoStream;
		buildInfoStream << Settings::CookedAssetRoot() << "/" << aShaderPath << "_buildInfo.json";
		buildInfoStream.NormalizePath();

		CompileShaderIfChanged(aShaderPath, relHlslStream.GetStringView(), hlslResolvedStream.GetStringView(), buildInfoStream.GetStringView(), csoStream.GetStringView(), ShaderType::Pixel, addToFileWatcher);
	}
#endif

	StringId id = StringRegistry::RegisterOrGetString(aShaderPath);
	PixelShader& shader = ourLoadedPixelShaders[id];

	HRESULT result;

	std::ifstream file;
	file.open(csoStream.GetData(), std::ios::binary);
	std::string data = { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	result = DX11::Device->CreatePixelShader(data.data(), data.size(), nullptr, shader.shader.ReleaseAndGetAddressOf());
	if (FAILED(result))
	{
		ourLoadedPixelShaders.erase(id);
		return nullptr;
	}
	file.close();

	if (rhi::IDevice* r = Rhi())
	{
		if (shader.module) r->Destroy(shader.module);
		shader.module = r->CreateShaderModule(rhi::ShaderKind::Pixel, data.data(), data.size());
	}

	return &shader;
}


const VertexShader* DX11::ForceLoadVertexShader(const char* aShaderPath, bool addToFileWatcher)
{
	StringId id = StringRegistry::RegisterOrGetString(aShaderPath);
	VertexShader& shader = ourLoadedVertexShaders[id];

	FilePathStream csoStream;
	csoStream << Settings::CookedAssetRoot() << "/" << aShaderPath << ".cso";
	csoStream.NormalizePath();

	addToFileWatcher; // needed to not cause unreferenced parameter in retail
#ifndef _RETAIL
	FilePathStream relHlslStream;
	relHlslStream << aShaderPath << ".hlsl";

	FilePathStream hlslResolvedStream;
	if (Settings::ResolveAssetPath(relHlslStream.GetStringView(), hlslResolvedStream))
	{
		FilePathStream buildInfoStream;
		buildInfoStream << Settings::CookedAssetRoot() << "/" << aShaderPath << "_buildInfo.json";
		buildInfoStream.NormalizePath();

		CompileShaderIfChanged(aShaderPath, relHlslStream.GetStringView(), hlslResolvedStream.GetStringView(), buildInfoStream.GetStringView(), csoStream.GetStringView(), ShaderType::Vertex, addToFileWatcher);
	}
#endif

	HRESULT result;

	std::ifstream file;
	file.open(csoStream.GetData(), std::ios::binary);
	shader.data = { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	result = DX11::Device->CreateVertexShader(shader.data.data(), shader.data.size(), nullptr, shader.shader.ReleaseAndGetAddressOf());
	if (FAILED(result))
	{
		ourLoadedVertexShaders.erase(id);
		return nullptr;
	}
	file.close();

	if (rhi::IDevice* r = Rhi())
	{
		if (shader.module) r->Destroy(shader.module);
		shader.module = r->CreateShaderModule(rhi::ShaderKind::Vertex, shader.data.data(), shader.data.size());
	}

	return &shader;
}

const ComputeShader* DX11::ForceLoadComputeShader(const char* aShaderPath, bool addToFileWatcher)
{
	StringId id = StringRegistry::RegisterOrGetString(aShaderPath);
	ComputeShader& shader = ourLoadedComputeShaders[id];

	FilePathStream csoStream;
	csoStream << Settings::CookedAssetRoot() << "/" << aShaderPath << ".cso";
	csoStream.NormalizePath();

	addToFileWatcher; // needed to not cause unreferenced parameter in retail
#ifndef _RETAIL
	FilePathStream relHlslStream;
	relHlslStream << aShaderPath << ".hlsl";

	FilePathStream hlslResolvedStream;
	if (Settings::ResolveAssetPath(relHlslStream.GetStringView(), hlslResolvedStream))
	{
		FilePathStream buildInfoStream;
		buildInfoStream << Settings::CookedAssetRoot() << "/" << aShaderPath << "_buildInfo.json";
		buildInfoStream.NormalizePath();

		CompileShaderIfChanged(aShaderPath, relHlslStream.GetStringView(), hlslResolvedStream.GetStringView(), buildInfoStream.GetStringView(), csoStream.GetStringView(), ShaderType::Compute, addToFileWatcher);
	}
#endif

	HRESULT result;

	std::ifstream file;
	file.open(csoStream.GetData(), std::ios::binary);
	std::string data = { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	result = DX11::Device->CreateComputeShader(data.data(), data.size(), nullptr, shader.shader.ReleaseAndGetAddressOf());
	if (FAILED(result))
	{
		ourLoadedComputeShaders.erase(id);
		return nullptr;
	}
	file.close();

	if (rhi::IDevice* r = Rhi())
	{
		if (shader.module) r->Destroy(shader.module);
		shader.module = r->CreateShaderModule(rhi::ShaderKind::Compute, data.data(), data.size());
	}

	return &shader;
}
