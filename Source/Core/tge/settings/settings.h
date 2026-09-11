#pragma once

#include <string_view>
#include <filesystem>
#include <tge/math/vector2.h>
#include <tge/math/color.h>
#include <functional>
#include <tge/util/FixedStream.h>
#include <tge/stringRegistry/StringRegistry.h>
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>
namespace fs = std::filesystem;
#define TGA_DEFAULT_CRYSTAL_BLUE { 3.0f / 255.0f, 153.0f / 255.0f, 176.0f / 255.0f, 1.0f }

namespace Tga
{
    using callback_function_wndProc = std::function<LRESULT(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)>;
    using callback_function_log = std::function<void(std::string_view)>;
    using callback_function_error = std::function<void(std::string_view)>;

    enum class DebugFeature
    {
        None = 0,
        Fps = 1 << 0,
        Mem = 1 << 1,
        Drawcalls = 1 << 2,
        Cpu = 1 << 3,
        FpsGraph = 1 << 4,
        Filewatcher = 1 << 5,
        OptimizeWarnings = 1 << 6,
        MemoryTrackingStackTraces = 1 << 7,
        MemoryTrackingAllAllocations = 1 << 8,
        Log = 1 << 9,
        All = (1 << 10) - 1,
    };
    inline DebugFeature operator|(DebugFeature lhs, DebugFeature rhs)
    {
        return static_cast<DebugFeature>(static_cast<std::underlying_type<DebugFeature>::type>(lhs) | static_cast<std::underlying_type<DebugFeature>::type>(rhs));
    }
    inline DebugFeature operator&(DebugFeature lhs, DebugFeature rhs)
    {
        return static_cast<DebugFeature>(static_cast<std::underlying_type<DebugFeature>::type>(lhs) & static_cast<std::underlying_type<DebugFeature>::type>(rhs));
    }
    enum class MultiSamplingQuality
    {
        Off,
        Low = 1,
        Medium = 2,
        High = 3,
    };

    struct ApplicationConfiguration
    {
        ApplicationConfiguration()
        {
            hwnd = nullptr;
            hInstance = nullptr;
            winProcCallback = nullptr;
            windowSize = { 1280, 720 };
            enableVSync = false;
            renderSize = windowSize;
            startInFullScreen = false;
            startMaximized = false;
            clearColor = TGA_DEFAULT_CRYSTAL_BLUE;
            applicationName = L"TGA - Engagemang, Respect och Nyfikenhet!";
            borderless = false;
            activateDebugSystems = DebugFeature::Fps | DebugFeature::Mem | DebugFeature::Log;
            preferedMultiSamplingQuality = MultiSamplingQuality::Off;
        }

        callback_function_wndProc winProcCallback;

        /* How big should the window be? */
        Vector2ui windowSize;

        /* What resolution should we render everything in?*/
        Vector2ui renderSize;

        /* Will show the FPS and memory text*/
        DebugFeature activateDebugSystems;
        Color clearColor;
        HWND* hwnd;
        HINSTANCE hInstance;
        std::wstring applicationName;
        bool enableVSync;
        bool startInFullScreen;
        bool startMaximized;
        bool borderless;

        MultiSamplingQuality preferedMultiSamplingQuality;
    };

	extern bool LoadSettings(const std::string& aProjectName);
	
	namespace Settings
	{
		extern const std::string& EngineAssetRoot();
		extern const std::string& GameAssetRoot();
		extern const std::string& CookedAssetRoot();

		extern ApplicationConfiguration& GetApplicationConfiguration();

		extern std::string ResolveAssetPath(std::string_view anAsset);
		extern bool ResolveAssetPath(std::string_view anAsset, FilePathStream& outResolvedPath);

		extern std::string ResolveEngineAssetPath(std::string_view anAsset);
        extern bool ResolveEngineAssetPath(std::string_view anAsset, FilePathStream& outResolvedPath);

		extern std::string ResolveGameAssetPath(std::string_view anAsset);
        extern bool ResolveGameAssetPath(std::string_view anAsset, FilePathStream& outResolvedPath);

		extern std::string ResolveCookedAssetPath(std::string_view anAsset);
        extern bool ResolveCookedAssetPath(std::string_view anAsset, FilePathStream& outResolvedPath);

	}
}
