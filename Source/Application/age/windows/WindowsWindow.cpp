#include "stdafx.h"
#include <age/windows/WindowsWindow.h>
#include "age/windows/EngineResources.h"
#include <WinUser.h>
#include <age/ImGui/ImGuiInterface.h>

using namespace Ag;

WindowsWindow::WindowsWindow(void)
	:myWndProcCallback(nullptr)
{
}


WindowsWindow::~WindowsWindow(void)
{
}

bool WindowsWindow::Init(const ApplicationConfiguration &aWindowConfig, HINSTANCE &aHInstanceToFill, HWND*& aHwnd)
{
	myWndProcCallback = aWindowConfig.winProcCallback;
	myKeepAspectRatio = aWindowConfig.keepAspectRatio && !aWindowConfig.borderless &&
		!aWindowConfig.startInFullScreen && aWindowConfig.windowSize.y != 0;
	myClientAspectRatio = myKeepAspectRatio
		? static_cast<float>(aWindowConfig.windowSize.x) / static_cast<float>(aWindowConfig.windowSize.y)
		: 1.0f;
	HINSTANCE instance = GetModuleHandle(NULL);
	aHInstanceToFill = instance;

	ZeroMemory(&myWindowClass, sizeof(WNDCLASSEX));
	myWindowClass.cbSize = sizeof(WNDCLASSEX);
	myWindowClass.style = CS_HREDRAW | CS_VREDRAW;
	myWindowClass.lpfnWndProc = WindowProc;
	myWindowClass.hInstance = instance;
	myWindowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	myWindowClass.hbrBackground = ::CreateSolidBrush(RGB(11, 11, 13));   // never the system light colour
	myWindowClass.lpszClassName = L"WindowClass1";
	myWindowClass.hIcon = ::LoadIcon(instance, MAKEINTRESOURCE(IDI_APP_ICON));
	myWindowClass.hIconSm = LoadIcon(instance, MAKEINTRESOURCE(IDI_APP_ICON));
	RegisterClassEx(&myWindowClass);

	const auto& windowSize = aWindowConfig.windowSize;

	RECT wr = {0, 0, static_cast<long>(windowSize.x), static_cast<long>(windowSize.y)};    // set the size, but not the position
	//AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);    // adjust the size

	DWORD windowStyle = 0;
	if (aWindowConfig.borderless || aWindowConfig.startInFullScreen)
	{
		windowStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;   // WS_VISIBLE deferred, see RevealDeferred
	}
	else
	{
		windowStyle = WS_OVERLAPPEDWINDOW;
	}

	if (!aHwnd)
	{
		myWindowHandle = CreateWindowEx(
			WS_EX_APPWINDOW,
			L"WindowClass1",    // name of the window class
			aWindowConfig.applicationName.c_str(),    // title of the window
			windowStyle,    // window style
			0,0,
			//CW_USEDEFAULT,    // x-position of the window
			//CW_USEDEFAULT,    // y-position of the window
			wr.right - wr.left,    // width of the window
			wr.bottom - wr.top,    // height of the window
			NULL,    // we have no parent window, NULL
			NULL,    // we aren't using menus, NULL
			instance,    // application handle
			NULL);    // used with multiple windows, NULL
		
		// Dark title bar and border to match the editor chrome. Without this the
		// window wears the system light frame around a near-black client area,
		// which is the single most obvious way a dark tool still looks unfinished.
		//
		// Resolved dynamically because the attribute is only honoured from
		// Windows 10 20H1 onward, and the constant moved from 19 to 20 between
		// builds -- setting the wrong one is harmless, so both are attempted.
		if (HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll"))
		{
			using SetAttrFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
			if (auto setAttr = reinterpret_cast<SetAttrFn>(::GetProcAddress(dwm, "DwmSetWindowAttribute")))
			{
				const BOOL useDark = TRUE;
				setAttr(myWindowHandle, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &useDark, sizeof(useDark));
				setAttr(myWindowHandle, 19 /* pre-20H1 spelling of the same */, &useDark, sizeof(useDark));
			}
			::FreeLibrary(dwm);
		}

		// Remembered, not applied. The window stays hidden until the first frame
		// is actually on the swapchain, so the splash covers a clean desktop
		// rather than an empty rectangle. See RevealDeferred.
		myDeferredShowCmd = (aWindowConfig.startInFullScreen || aWindowConfig.startMaximized) ? SW_MAXIMIZE : SW_SHOWDEFAULT;
		aHwnd = &myWindowHandle;
	}
	else
	{
		myWindowHandle = *aHwnd;
	}

	SetWindowLongPtr(myWindowHandle, GWLP_USERDATA, (LONG_PTR)this);

	// Fix to set the window to the actual resolution as the borders will mess with the resolution wanted
	myResolution = windowSize;
	myResolutionWithBorderDifference = myResolution;
	if (aWindowConfig.borderless == false)
	{
		RECT r;
		GetClientRect(myWindowHandle, &r); //get window rect of control relative to screen
		int horizontal = r.right - r.left;
		int vertical = r.bottom - r.top;

		int diffX = windowSize.x - horizontal;
		int diffY = windowSize.y - vertical;

		SetResolution(windowSize + Vector2ui(diffX, diffY));
		myResolutionWithBorderDifference = windowSize + Vector2ui(diffX, diffY);
	}



	INFO_PRINT("%s %i %i", "Windows created with size ", windowSize.x, windowSize.y);

	return true;
}


#ifndef _RETAIL
IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
LRESULT WindowsWindow::LocWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#ifndef _RETAIL
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
	{
		return S_OK;
	}
#endif
	if (myWndProcCallback)
	{
		return myWndProcCallback(hWnd, message, wParam, lParam);
	}
	return S_OK;
}

LRESULT CALLBACK WindowsWindow::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	WindowsWindow* windowsClass = (WindowsWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	if (windowsClass)
	{
		LRESULT result = windowsClass->LocWindowProc(hWnd, message, wParam, lParam);
		if (result)
		{
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}

	switch(message)
	{
		case WM_SIZING:
		{
			if (windowsClass && windowsClass->myKeepAspectRatio)
			{
				RECT* const rect = reinterpret_cast<RECT*>(lParam);
				const long borderWidth = static_cast<long>(windowsClass->myResolutionWithBorderDifference.x - windowsClass->myResolution.x);
				const long borderHeight = static_cast<long>(windowsClass->myResolutionWithBorderDifference.y - windowsClass->myResolution.y);
				const long outerWidth = rect->right - rect->left;
				const long outerHeight = rect->bottom - rect->top;
				const long clientWidth = std::max(1L, outerWidth - borderWidth);
				const long clientHeight = std::max(1L, outerHeight - borderHeight);

				// Horizontal edges (and corners) drive width; vertical edges drive
				// height.  The opposite edge remains anchored under the cursor.
				const bool verticalEdge = wParam == WMSZ_TOP || wParam == WMSZ_BOTTOM;
				const long constrainedClientWidth = verticalEdge
					? static_cast<long>(std::lround(clientHeight * windowsClass->myClientAspectRatio))
					: clientWidth;
				const long constrainedClientHeight = verticalEdge
					? clientHeight
					: static_cast<long>(std::lround(clientWidth / windowsClass->myClientAspectRatio));
				const long constrainedOuterWidth = constrainedClientWidth + borderWidth;
				const long constrainedOuterHeight = constrainedClientHeight + borderHeight;

				if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT)
					rect->left = rect->right - constrainedOuterWidth;
				else
					rect->right = rect->left + constrainedOuterWidth;

				if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT)
					rect->top = rect->bottom - constrainedOuterHeight;
				else
					rect->bottom = rect->top + constrainedOuterHeight;
				return TRUE;
			}
			break;
		}

		case WM_DESTROY:
			{
				PostQuitMessage(0);
				return 0;
			} break;

		case WM_SIZE:
		{
			if (windowsClass)
			{
				const unsigned int width = LOWORD(lParam), height = HIWORD(lParam);
				// Preserve the last valid size while minimized; DX11/DX12 both reject
				// zero-sized backbuffers and resize again on the restore WM_SIZE.
				if (width != 0 && height != 0)
					windowsClass->myResolution = { width, height };
			}
			if (Application::GetInstance())
				Application::GetInstance()->SetWantToUpdateSize();
			break;
		}
		

	}
	return DefWindowProc (hWnd, message, wParam, lParam);
}

void Ag::WindowsWindow::SetResolution(Vector2ui aResolution)
{
	::SetWindowPos(myWindowHandle, 0, 0, 0, aResolution.x, aResolution.y, SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
	RECT client{};
	if (::GetClientRect(myWindowHandle, &client))
	{
		const unsigned int width = std::max(0L, client.right - client.left);
		const unsigned int height = std::max(0L, client.bottom - client.top);
		if (width != 0 && height != 0) myResolution = { width, height };
	}
}

unsigned int Ag::WindowsWindow::GetWidth() const
{
	RECT client{};
	if (myWindowHandle && ::GetClientRect(myWindowHandle, &client))
	{
		const long width = client.right - client.left;
		if (width > 0) return static_cast<unsigned int>(width);
	}
	return myResolution.x;
}

unsigned int Ag::WindowsWindow::GetHeight() const
{
	RECT client{};
	if (myWindowHandle && ::GetClientRect(myWindowHandle, &client))
	{
		const long height = client.bottom - client.top;
		if (height > 0) return static_cast<unsigned int>(height);
	}
	return myResolution.y;
}

void Ag::WindowsWindow::RevealDeferred()
{
	if (myRevealed || !myWindowHandle) return;
	myRevealed = true;
	// An externally supplied HWND (the editor embedding case) was never ours to
	// hide, so there is nothing to reveal.
	if (myDeferredShowCmd == 0) return;
	::ShowWindow(myWindowHandle, myDeferredShowCmd);
	::SetForegroundWindow(myWindowHandle);
}

void Ag::WindowsWindow::Close()
{
	DestroyWindow(myWindowHandle);
}
