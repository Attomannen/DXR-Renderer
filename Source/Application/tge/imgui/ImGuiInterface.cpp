#include "stdafx.h"
#include "ImGuiInterface.h"
#include <string>
#include <tge/application.h>
#include <tge/graphics/DX11.h>
#include <imnodes/imnodes.h>
#include <IconFontHeaders/IconsLucide.h>

#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

//#pragma comment(lib, "..\\Libs\\imgui.lib")
//#pragma comment(lib, "..\\Libs\\imgui_canvas.lib")

using namespace Tga;

namespace Tga
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
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImNodes::DestroyContext();
	ImGui::DestroyContext();

	ourImpl = nullptr;
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
	config.OversampleH = 4;
	config.OversampleV = 4;
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
		icons_config.OversampleH = 4;
		icons_config.OversampleV = 4;
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
		icons_config.OversampleH = 4;
		icons_config.OversampleV = 4;
		icons_config.PixelSnapH = true;
		icons_config.GlyphMinAdvanceX = iconFontSize;
		FilePathStream fontPath;
		if (Settings::ResolveEngineAssetPath("text/lucide.ttf", fontPath))
		{
			ourImpl->iconFont = ourImpl->fontAtlas.AddFontFromFileTTF(fontPath.GetData(), iconFontSize, &icons_config, icons_ranges);
		}
	}

	ourImpl->fontAtlas.Build();


	ImGui::CreateContext(&ourImpl->fontAtlas);
	ImGuiIO& io = ImGui::GetIO();

	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
	//io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	ImNodes::CreateContext();


	// Setup ImGui binding
	ImGui_ImplWin32_Init(*Tga::Application::GetInstance()->GetHWND());
	ImGui_ImplDX11_Init(Tga::DX11::Device, Tga::DX11::Context);
	ImGui::StyleColorsDark();

	ImGui::GetStyle().TabBarOverlineSize = 0;

	auto& colors = ImGui::GetStyle().Colors;

	ImVec4 darkest = ImVec4{ 0.1f, 0.1f, 0.1f, 1.0f };
	ImVec4 darker = ImVec4{ 0.13f, 0.13f, 0.13f, 1.0f };
	ImVec4 dark = ImVec4{ 0.16f, 0.16f, 0.16f, 1.0f };
	ImVec4 darkish = ImVec4{ 0.24f, 0.24f, 0.24f, 1.0f };

	ImVec4 light = ImVec4{ 0.37f, 0.13f, 0.33f, 1.0f };// ImVec4{ 0.28f, 0.13f, 0.24f, 1.0f };
	ImVec4 lighter = ImVec4{ 0.50f, 0.18f, 0.45f, 1.0f };//ImVec4{ 0.44f, 0.18f, 0.38f, 1.0f };
	
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
	colors[ImGuiCol_CheckMark] = lighter;

	// Popups
	colors[ImGuiCol_PopupBg] = ImVec4{ darkest.x, darkest.y, darkest.z, 0.92f };

	// Slider
	colors[ImGuiCol_SliderGrab] = lighter;
	colors[ImGuiCol_SliderGrabActive] = lighter;

	// Frame BG
	colors[ImGuiCol_FrameBg] = darker;
	colors[ImGuiCol_FrameBgHovered] = light;
	colors[ImGuiCol_FrameBgActive] = dark;

	// Tabs
	colors[ImGuiCol_Tab] = dark;
	colors[ImGuiCol_TabHovered] = lighter;
	colors[ImGuiCol_TabSelected] = darkish;
	colors[ImGuiCol_TabSelectedOverline] = lighter;
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
	ImGui_ImplDX11_NewFrame();
	ImGui::NewFrame();
#endif
}

void ImGuiInterface::Render()
{
#ifndef _RETAIL
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();
#endif
}
