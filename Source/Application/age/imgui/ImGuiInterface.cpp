#include "stdafx.h"
#include "ImGuiInterface.h"
#include "age/imgui/ConsolePanel.h"
#include <string>
#include <age/application.h>
#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>
#include <age/debugging/CpuProfiler.h>
#include <IconFontHeaders/IconsLucide.h>

#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_impl_win32.h"

//#pragma comment(lib, "..\\Libs\\imgui.lib")
//#pragma comment(lib, "..\\Libs\\imgui_canvas.lib")

using namespace Ag;

namespace Ag
{
	struct ImGuiInterfaceImpl
	{
		ImFontAtlas fontAtlas;
		ImFont* defaultFont;
		ImFont* iconFont;
	};
}

std::unique_ptr<ImGuiInterfaceImpl> ImGuiInterface::ourImpl;

ImFont* ImGuiInterface::GetIconFontLarge()
{
	return ourImpl->iconFont;
}

void ImGuiInterface::Shutdown()
{
#ifndef _RETAIL
	Ag::rhi::IDevice* rhiDevice = Ag::DX11::Rhi();
	if (rhiDevice && rhiDevice->GetBackend() == Ag::rhi::Backend::DX12)
		ImGui_ImplDX12_Shutdown();
	else
		ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	ourImpl = nullptr;
#endif
}

void ImGuiInterface::OnResizeBegin()
{
#ifndef _RETAIL
	if (!ImGui::GetCurrentContext()) return;
	Ag::rhi::IDevice* rhiDevice = Ag::DX11::Rhi();
	if (!rhiDevice || rhiDevice->GetBackend() != Ag::rhi::Backend::DX12)
		ImGui_ImplDX11_InvalidateDeviceObjects();
#endif
}

void ImGuiInterface::OnResizeEnd()
{
#ifndef _RETAIL
	if (!ImGui::GetCurrentContext()) return;
	Ag::rhi::IDevice* rhiDevice = Ag::DX11::Rhi();
	if (!rhiDevice || rhiDevice->GetBackend() != Ag::rhi::Backend::DX12)
		ImGui_ImplDX11_CreateDeviceObjects();
#endif
}

#ifndef _RETAIL
static ImFont* ImGuiLoadSystemFont(ImFontAtlas& atlas, const char* name, float size)
{
	char* windir = nullptr;
	if (_dupenv_s(&windir, nullptr, "WINDIR") || windir == nullptr)
		return nullptr;

	static const ImWchar ranges[] =
	{
		0x0020, 0x00FF, // Basic Latin + Latin Supplement
		0x0104, 0x017C, // Polish characters and more
		0,
	};

	ImFontConfig config;
	// 2x horizontal, 1x vertical -- not 4x4.
	//
	// Oversampling rasterises each glyph at N times the pixels per axis, so 4x4
	// is sixteen times the work for the whole atlas. Vertical oversampling buys
	// almost nothing, because text baselines land on pixel boundaries anyway;
	// ImGui's own default is 1 vertically for exactly that reason. Horizontal
	// oversampling does help, since glyph advances are fractional, but the
	// return past 2 is very small.
	config.OversampleH = 2;
	config.OversampleV = 1;
	config.PixelSnapH = false;

	auto path = std::string(windir) + "\\Fonts\\" + name;
	auto font = atlas.AddFontFromFileTTF(path.c_str(), size, &config, ranges);

	free(windir);

	return font;
}
#endif

void ImGuiInterface::Init()
{
#ifndef _RETAIL
	ourImpl = std::make_unique<ImGuiInterfaceImpl>();

	ourImpl->defaultFont = ImGuiLoadSystemFont(ourImpl->fontAtlas, "segoeui.ttf", 18.0f);//16.0f * 96.0f / 72.0f);

	{
		float iconFontSize = 16.0f;

		static const ImWchar icons_ranges[] = { ICON_MIN_LC, ICON_MAX_16_LC, 0 };
		ImFontConfig icons_config;
		icons_config.MergeMode = true;
		// Icons are pixel-snapped and drawn at a fixed size, so there are no
		// fractional positions for oversampling to resolve. It was pure cost --
		// and this font's range is well over a thousand glyphs, rasterised twice
		// because the atlas holds it at two sizes.
		icons_config.OversampleH = 1;
		icons_config.OversampleV = 1;
		icons_config.PixelSnapH = true;
		icons_config.GlyphMinAdvanceX = iconFontSize;
		icons_config.GlyphOffset = { 0.f, 3.f };
		FilePathStream fontPath;
		if (Settings::ResolveEngineAssetPath("text/lucide.ttf", fontPath))
		{
			ourImpl->fontAtlas.AddFontFromFileTTF(fontPath.GetData(), iconFontSize, &icons_config, icons_ranges);
	}
	}

	// todo: load variations like bold and at different sizes, expose through ImGuiInterface;

	{
		float iconFontSize = 24.0f;

		static const ImWchar icons_ranges[] = { ICON_MIN_LC, ICON_MAX_16_LC, 0 };
		ImFontConfig icons_config;
		icons_config.OversampleH = 1;
		icons_config.OversampleV = 1;
		icons_config.PixelSnapH = true;
		icons_config.GlyphMinAdvanceX = iconFontSize;
		FilePathStream fontPath;
		if (Settings::ResolveEngineAssetPath("text/lucide.ttf", fontPath))
		{
			ourImpl->iconFont = ourImpl->fontAtlas.AddFontFromFileTTF(fontPath.GetData(), iconFontSize, &icons_config, icons_ranges);
		}
	}

	{ AG_CPU_SCOPE("Font atlas build"); ourImpl->fontAtlas.Build(); }


	{ AG_CPU_SCOPE("ImGui CreateContext"); ImGui::CreateContext(&ourImpl->fontAtlas); }
	ImGuiIO& io = ImGui::GetIO();

	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	// Multi-viewport (dragging a panel out into its own OS window) needs BOTH
	// the platform backend (Win32, always supports it) and the RENDERER
	// backend to implement Renderer_CreateWindow/RenderWindow/DestroyWindow.
	// imgui_impl_dx12 (as vendored, 1.91.6) implements none of these -- if
	// ViewportsEnable were set under DX12, ImGui::RenderPlatformWindowsDefault
	// would call a null Renderer_RenderWindow the first time a panel is torn
	// out, and crash. DX11 keeps the existing behavior unchanged.
	if (!Ag::DX11::Rhi() || Ag::DX11::Rhi()->GetBackend() != Ag::rhi::Backend::DX12)
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
	//io.IniFilename = nullptr;
	io.LogFilename = nullptr;


	// Setup ImGui binding
	{ AG_CPU_SCOPE("ImGui Win32 init"); ImGui_ImplWin32_Init(*Ag::Application::GetInstance()->GetHWND()); }
	// imgui_impl_dx11/dx12 are dedicated per-backend renderer backends (stay
	// outside the RHI seam per the plan); GetNative*/GetImGuiSrv* are the
	// sanctioned escape hatches rather than reaching for backend-private
	// statics/types directly.
	Ag::rhi::IDevice* rhiDevice = Ag::DX11::Rhi();
	if (rhiDevice->GetBackend() == Ag::rhi::Backend::DX12)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE fontCpu{ reinterpret_cast<SIZE_T>(rhiDevice->ImGuiFontSrvCpuHandle()) };
		D3D12_GPU_DESCRIPTOR_HANDLE fontGpu{ reinterpret_cast<UINT64>(rhiDevice->ImGuiFontSrvGpuHandle()) };
		// The legacy 6-argument Init() (still supported, and what this used
		// to call) has no way to pass the app's own command queue, so this
		// vendored backend's font-texture upload always spun up a brand-new,
		// independent ID3D12CommandQueue purely for that one-off copy. Found
		// 2026-09-11: on this GPU/driver, that second queue submitting work
		// concurrently with the engine's own busy queue (loading many other
		// textures at startup) deadlocks -- silently, until Windows' TDR
		// watchdog resets the device ~2 seconds later. Using the InitInfo
		// struct form instead lets ImGui_ImplDX12_CreateFontsTexture reuse
		// our real queue (see the matching comment in imgui_impl_dx12.cpp),
		// sidestepping the second-queue interaction entirely.
		ImGui_ImplDX12_InitInfo initInfo = {};
		initInfo.Device = static_cast<ID3D12Device*>(rhiDevice->GetNativeDevice());
		initInfo.CommandQueue = static_cast<ID3D12CommandQueue*>(rhiDevice->GetNativeCommandQueue());
		initInfo.NumFramesInFlight = 3;   // matches Dx12Device::kFramesInFlight
		initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;   // BackBufferNoSrgbConversion's format -- ImGui renders here, not the sRGB view (see Application::EndFrame)
		initInfo.SrvDescriptorHeap = static_cast<ID3D12DescriptorHeap*>(rhiDevice->GetImGuiSrvDescriptorHeap());
		initInfo.LegacySingleSrvCpuDescriptor = fontCpu;
		initInfo.LegacySingleSrvGpuDescriptor = fontGpu;
		{ AG_CPU_SCOPE("ImGui DX12 backend init");
		ImGui_ImplDX12_Init(&initInfo); }
	}
	else
	{
		ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(rhiDevice->GetNativeDevice()),
		                    static_cast<ID3D11DeviceContext*>(rhiDevice->GetNativeContext()));
	}
	ImGui::StyleColorsDark();

	ImGui::GetStyle().TabBarOverlineSize = 0;

	auto& colors = ImGui::GetStyle().Colors;

	// AttoEngine dark. The surface ramp is anchored on #0b0b0d, the same
	// near-black the logo mark sits on, so the editor chrome and the brand are
	// the same colour rather than two different dark greys.
	//
	// The accent is neutral by design. The mark is monochrome, and a saturated
	// hue (this was The Game Assembly's purple) fights it wherever the two
	// appear together. Selection reads through value rather than hue, which is
	// also what keeps a viewport-dominated tool from tinting the work inside it.
	ImVec4 darkest = ImVec4{ 0.043f, 0.043f, 0.051f, 1.0f };   // #0b0b0d
	ImVec4 darker  = ImVec4{ 0.078f, 0.078f, 0.086f, 1.0f };   // #141416
	ImVec4 dark    = ImVec4{ 0.110f, 0.110f, 0.125f, 1.0f };   // #1c1c20
	ImVec4 darkish = ImVec4{ 0.165f, 0.165f, 0.188f, 1.0f };   // #2a2a30

	ImVec4 light   = ImVec4{ 0.229f, 0.229f, 0.267f, 1.0f };   // #3a3a44  selected
	ImVec4 lighter = ImVec4{ 0.322f, 0.322f, 0.373f, 1.0f };   // #52525f  hovered
	
	colors[ImGuiCol_TextDisabled] = dark;


	colors[ImGuiCol_WindowBg] = darkest;
	colors[ImGuiCol_MenuBarBg] = dark;

	// Border
	colors[ImGuiCol_Border] = dark;
	colors[ImGuiCol_BorderShadow] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.24f };

	// Text
	colors[ImGuiCol_Text] = ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f };
	colors[ImGuiCol_TextDisabled] = ImVec4{ 0.5f, 0.5f, 0.5f, 1.0f };

	// Headers
	colors[ImGuiCol_Header] = light;
	colors[ImGuiCol_HeaderHovered] = lighter;
	colors[ImGuiCol_HeaderActive] = lighter;

	// Buttons
	colors[ImGuiCol_Button] = darker;
	colors[ImGuiCol_ButtonHovered] = lighter;
	colors[ImGuiCol_ButtonActive] = dark;
	colors[ImGuiCol_CheckMark] = ImVec4{ 0.85f, 0.85f, 0.88f, 1.0f };

	// Popups
	colors[ImGuiCol_PopupBg] = ImVec4{ darkest.x, darkest.y, darkest.z, 0.92f };

	// Slider
	colors[ImGuiCol_SliderGrab] = ImVec4{ 0.68f, 0.68f, 0.72f, 1.0f };
	colors[ImGuiCol_SliderGrabActive] = ImVec4{ 0.85f, 0.85f, 0.88f, 1.0f };

	// Frame BG
	colors[ImGuiCol_FrameBg] = darker;
	colors[ImGuiCol_FrameBgHovered] = light;
	colors[ImGuiCol_FrameBgActive] = dark;

	// Tabs
	colors[ImGuiCol_Tab] = dark;
	colors[ImGuiCol_TabHovered] = lighter;
	colors[ImGuiCol_TabSelected] = darkish;
	colors[ImGuiCol_TabSelectedOverline] = ImVec4{ 0.85f, 0.85f, 0.88f, 1.0f };
	colors[ImGuiCol_TabUnfocused] = dark;
	colors[ImGuiCol_TabUnfocusedActive] = darkish;

	// Title
	colors[ImGuiCol_TitleBg] = darker;
	colors[ImGuiCol_TitleBgActive] = darker;
	colors[ImGuiCol_TitleBgCollapsed] = darker;

	// Scrollbar
	colors[ImGuiCol_ScrollbarBg] = darkest;
	colors[ImGuiCol_ScrollbarGrab] = dark;
	colors[ImGuiCol_ScrollbarGrabHovered] = lighter;
	colors[ImGuiCol_ScrollbarGrabActive] = darkish;

	// Separator
	colors[ImGuiCol_Separator] = dark;
	colors[ImGuiCol_SeparatorHovered] = lighter;
	colors[ImGuiCol_SeparatorActive] = lighter;

	// Separator
	colors[ImGuiCol_TableBorderLight] = dark;
	colors[ImGuiCol_TableBorderStrong] = dark;

	// Resize Grip
	colors[ImGuiCol_ResizeGrip] = dark;
	colors[ImGuiCol_ResizeGripHovered] = lighter;
	colors[ImGuiCol_ResizeGripActive] = lighter;
	 
	// Docking
	colors[ImGuiCol_DockingPreview] = dark;
	
	// Text
	colors[ImGuiCol_TextSelectedBg] = lighter;

#endif
}

void ImGuiInterface::PreFrame()
{
#ifndef _RETAIL
	ImGui_ImplWin32_NewFrame();
	Ag::rhi::IDevice* rhiDevice = Ag::DX11::Rhi();
	if (rhiDevice && rhiDevice->GetBackend() == Ag::rhi::Backend::DX12)
		ImGui_ImplDX12_NewFrame();
	else
		ImGui_ImplDX11_NewFrame();
	ImGui::NewFrame();
#endif
}

void ImGuiInterface::Render()
{
	// Drawn here so both executables get it: this is the one ImGui hook they
	// share. See ConsolePanel -- it replaces the Win32 console window.
	ConsolePanel::Draw();
#ifndef _RETAIL
	ImGui::Render();

	Ag::rhi::IDevice* rhiDevice = Ag::DX11::Rhi();
	if (rhiDevice && rhiDevice->GetBackend() == Ag::rhi::Backend::DX12)
	{
		auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(rhiDevice->GetNativeCommandList());
		auto* srvHeap = static_cast<ID3D12DescriptorHeap*>(rhiDevice->GetImGuiSrvDescriptorHeap());
		// imgui_impl_dx12 expects the CALLER to have the right descriptor heap
		// bound before RenderDrawData -- unlike imgui_impl_dx11, it never calls
		// SetDescriptorHeaps itself. This heap is exclusively ImGui's own (see
		// Dx12Device::myImGuiSrvHeap); the engine's own scratch heaps get
		// rebound fresh next frame in Dx12Device::BeginFrame, and nothing else
		// draws in this command list after ImGui (see Application::EndFrame).
		cmdList->SetDescriptorHeaps(1, &srvHeap);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
	}
	else
	{
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}

	// Multi-viewport is DX11-only (see Init's ConfigFlags comment) -- both
	// calls below are no-ops when ImGuiConfigFlags_ViewportsEnable isn't set,
	// so leaving them unconditional here is safe under DX12 too.
	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();
#endif
}
