#pragma once
#include <array>
#include <memory>
#include <unordered_map>
#include <age/Graphics/DepthBuffer.h>
#include <age/Graphics/RenderTarget.h>
#include <age/Math/Color.h>
#include <age/Math/Vector.h>
#include <thread>
#include <wrl/client.h>

#include <age/stringRegistry/StringRegistry.h>
#include <age/rhi/ShaderCache.h>

using Microsoft::WRL::ComPtr;

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11Debug;
struct ID3D11PixelShader;
struct ID3D11VertexShader;
struct ID3D11GeometryShader;

namespace Ag
{
class WindowsWindow;
namespace rhi { class IDevice; }
// PixelShader / VertexShader / ComputeShader now live in <age/rhi/ShaderCache.h>

// DirectX 11 Framework. Shorthand to make it easier to deal with.
class DX11
{
	static WindowsWindow* ourWindowHandler;
	
public:
	enum class ShaderType
	{
		Pixel,
		Vertex,
		Compute
	};

	DX11();
	~DX11();

	bool Init(WindowsWindow* aWindowHandler);
	bool ResizeToWindowSize();
	void BeginFrame(Color aClearColor);
	void EndFrame(bool aEnableVsync = true);

	static bool IsOnSameThreadAsEngine();

	static Vector2ui GetResolution();

	static ID3D11Device* Device;
	static ID3D11DeviceContext* Context;
	static IDXGISwapChain* SwapChain;
	static RenderTarget* BackBuffer;
	static RenderTarget* BackBufferNoSrgbConversion; // ImGui works in sRGB, so requires sRGB access to the backbuffer. Do not use for anything else
	static DepthBuffer* DepthBuffer;

	static const PixelShader* LoadPixelShader(const char* aShaderPath);
	static const VertexShader* LoadVertexShader(const char* aShaderPath);
	static const ComputeShader* LoadComputeShader(const char* aShaderPath);
	// DX12-only SM6/DXIL path. Kept separate from LoadComputeShader so DX11
	// never attempts to create an ID3D11ComputeShader from DXIL bytes.
	static const ComputeShader* LoadComputeShaderDxil(const char* aShaderPath);

	// The RHI device wrapping this DX11 context (created at the end of Init).
	// During the migration the raw statics above stay valid alongside it.
	static rhi::IDevice* Rhi() { return ourRhiDevice.get(); }

	static void ResetDrawCallCounter() { ourPreviousDrawCallCount = ourDrawCallCount; ourDrawCallCount = 0; };
	static void LogDrawCall() { ourDrawCallCount++; }
	static int GetPreviousDrawCallCount() { return ourPreviousDrawCallCount; }

	static void OnShaderFileChanged(StringId aShaderPath, ShaderType aShaderType);

private:
	// DX12 counterparts of Init/ResizeToWindowSize, selected via the AGE_RHI=dx12
	// env var (see DX11::Init). Skip the raw D3D11 device/swapchain entirely and
	// construct ourRhiDevice directly against the real window handle.
	bool InitDx12(WindowsWindow* aWindowHandler);
	bool ResizeToWindowSizeDx12();

	static const PixelShader* ForceLoadPixelShader(const char* aShaderPath, bool addToFileWatcher = true);
	static const VertexShader* ForceLoadVertexShader(const char* aShaderPath, bool addToFileWatcher = true);
	static const ComputeShader* ForceLoadComputeShader(const char* aShaderPath, bool addToFileWatcher = true);
	static const ComputeShader* ForceLoadComputeShaderDxil(const char* aShaderPath, bool addToFileWatcher = true);

	static int ourDrawCallCount;
	static int ourPreviousDrawCallCount;
	static bool ourIsCreated;
	static std::thread::id ourRenderThreadId;

	ComPtr<ID3D11Device> myDevice;
	ComPtr<ID3D11DeviceContext> myContext;
	ComPtr<IDXGISwapChain> mySwapChain;
	RenderTarget myBackBuffer;
	RenderTarget myBackBufferNoSrgbConversion;
	Ag::DepthBuffer myDepthBuffer;

	ComPtr<ID3D11Debug> myD3dDebug;

	static std::unordered_map<StringId, PixelShader> ourLoadedPixelShaders;
	static std::unordered_map<StringId, VertexShader> ourLoadedVertexShaders;
	static std::unordered_map<StringId, ComputeShader> ourLoadedComputeShaders;
	static std::unordered_map<StringId, ComputeShader> ourLoadedDxilComputeShaders;

	static std::unique_ptr<rhi::IDevice> ourRhiDevice;
};

} // namespace Ag
