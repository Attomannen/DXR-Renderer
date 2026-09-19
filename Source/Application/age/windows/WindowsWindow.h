/*
This class handles the creation of the actual window
*/

#pragma once
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN 
#endif
#if !defined(NOMINMAX)
#define NOMINMAX 
#endif
#include <windows.h>
#include <age/application.h>

namespace Ag
{
	struct EngineCreateParameters;
	class WindowsWindow
	{
	public:
		WindowsWindow(void);
		~WindowsWindow(void);
		bool Init(const ApplicationConfiguration& aWndCfg, HINSTANCE& aHInstanceToFill, HWND*& aHwnd);
		HWND GetWindowHandle() const {return myWindowHandle;}
		void SetResolution(Vector2ui aResolution);
		void Close();
		// The swapchain must follow the physical client area, not the startup
		// configuration. In particular, DXGI exclusive fullscreen can change the
		// client dimensions without synchronously updating our cached resolution.
		unsigned int GetWidth() const;
		unsigned int GetHeight() const;
	private:
		LRESULT LocWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
		static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
		HWND myWindowHandle;
		WNDCLASSEX myWindowClass;
		callback_function_wndProc myWndProcCallback;
		Vector2ui myResolution;
		Vector2ui myResolutionWithBorderDifference;
		bool myKeepAspectRatio = false;
		float myClientAspectRatio = 1.0f;
	};
}
