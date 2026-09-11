#include "stdafx.h"
#include <tge/windows/BackendChooser.h>
#include <cstdlib>

namespace
{
	constexpr int kLegacyButtonId = 1001;
	constexpr int kDx12ButtonId = 1002;

	// One-shot dialog state. Plain globals are fine here (matches this
	// codebase's own style for a single blocking pre-main-loop window, e.g.
	// GoEditor.cpp's SInputManager) -- this function is never called
	// concurrently or re-entrantly.
	Tga::RhiBackendChoice gChoice = Tga::RhiBackendChoice::Cancelled;
	bool gDone = false;

	LRESULT CALLBACK ChooserWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_COMMAND:
			if (LOWORD(wParam) == kLegacyButtonId)
			{
				gChoice = Tga::RhiBackendChoice::Legacy;
				gDone = true;
				DestroyWindow(hWnd);
			}
			else if (LOWORD(wParam) == kDx12ButtonId)
			{
				gChoice = Tga::RhiBackendChoice::Dx12;
				gDone = true;
				DestroyWindow(hWnd);
			}
			return 0;

		case WM_CLOSE:
			DestroyWindow(hWnd);   // triggers WM_DESTROY below
			return 0;

		case WM_DESTROY:
			// NOT PostQuitMessage(0): that posts WM_QUIT to the whole
			// THREAD's message queue, not just "close this one dialog" --
			// found 2026-09-12: the main application's own game-loop message
			// pump, started moments later, picked up this stale WM_QUIT on
			// its very first check and exited immediately, with everything
			// otherwise having initialized correctly (device, scene, GI --
			// no error anywhere), making a successful backend pick look
			// exactly like a random, silent crash. Setting `gDone` is all
			// this function's own local loop below needs to stop.
			gDone = true;
			return 0;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
}

namespace Tga
{
	RhiBackendChoice ShowBackendChooser(const wchar_t* aWindowTitle)
	{
		gChoice = RhiBackendChoice::Cancelled;
		gDone = false;

		HINSTANCE instance = GetModuleHandle(nullptr);

		WNDCLASSEX wc = {};
		wc.cbSize = sizeof(wc);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = ChooserWndProc;
		wc.hInstance = instance;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wc.lpszClassName = L"TgeBackendChooserWindowClass";
		RegisterClassEx(&wc);

		const int windowW = 360, windowH = 170;
		HWND hwnd = CreateWindowEx(
			WS_EX_APPWINDOW, L"TgeBackendChooserWindowClass", aWindowTitle,
			(WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU) & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX,
			CW_USEDEFAULT, CW_USEDEFAULT, windowW, windowH,
			nullptr, nullptr, instance, nullptr);
		if (!hwnd) return RhiBackendChoice::Cancelled;

		// Center on the primary monitor.
		{
			RECT rc = {};
			GetWindowRect(hwnd, &rc);
			const int actualW = rc.right - rc.left, actualH = rc.bottom - rc.top;
			const int screenW = GetSystemMetrics(SM_CXSCREEN), screenH = GetSystemMetrics(SM_CYSCREEN);
			SetWindowPos(hwnd, nullptr, (screenW - actualW) / 2, (screenH - actualH) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
		}

		HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		HWND label = CreateWindowEx(0, L"STATIC", L"Choose a rendering backend:",
			WS_CHILD | WS_VISIBLE | SS_CENTER,
			20, 20, windowW - 60, 20, hwnd, nullptr, instance, nullptr);
		HWND legacyBtn = CreateWindowEx(0, L"BUTTON", L"Legacy",
			WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
			35, 70, 130, 45, hwnd, (HMENU)(INT_PTR)kLegacyButtonId, instance, nullptr);
		HWND dx12Btn = CreateWindowEx(0, L"BUTTON", L"DX12",
			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			185, 70, 130, 45, hwnd, (HMENU)(INT_PTR)kDx12ButtonId, instance, nullptr);
		SendMessage(label, WM_SETFONT, (WPARAM)font, TRUE);
		SendMessage(legacyBtn, WM_SETFONT, (WPARAM)font, TRUE);
		SendMessage(dx12Btn, WM_SETFONT, (WPARAM)font, TRUE);

		ShowWindow(hwnd, SW_SHOWDEFAULT);
		UpdateWindow(hwnd);
		SetForegroundWindow(hwnd);

		MSG msg;
		while (!gDone && GetMessage(&msg, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (gChoice == RhiBackendChoice::Dx12)
			_putenv_s("TGE_RHI", "dx12");
		else if (gChoice == RhiBackendChoice::Legacy)
			_putenv_s("TGE_RHI", "dx11");   // explicit; DX11::Init() already treats anything but "dx12" as DX11

		UnregisterClass(L"TgeBackendChooserWindowClass", instance);
		return gChoice;
	}
}
