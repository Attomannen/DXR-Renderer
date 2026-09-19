#include "stdafx.h"
#include "age/windows/SplashScreen.h"
#include "age/windows/EngineResources.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace Ag
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		constexpr wchar_t kClassName[]  = L"AttoEngineSplash";
		constexpr int     kFadeInMs     = 320;
		constexpr int     kFadeOutMs    = 380;
		// A splash that vanishes the instant loading finishes reads as a glitch
		// rather than as a brand, so guarantee it is fully visible for a moment.
		constexpr int     kMinVisibleMs = 550;

		struct SplashState
		{
			std::thread       thread;
			std::atomic<bool> dismiss{ false };
			std::atomic<bool> visible{ false };
			HANDLE            ready = nullptr;
		};
		SplashState& State() { static SplashState s; return s; }

		// Decode the PNG that the .rc embedded into this executable.
		//
		// From resources rather than from disk on purpose: the splash has to be up
		// before the engine has resolved any asset path, and an executable that
		// carries its own splash cannot fail to find it.
		bool DecodeSplashPng(std::vector<uint8_t>& outPixels, UINT& outWidth, UINT& outHeight)
		{
			HRSRC res = ::FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_SPLASH_PNG), RT_RCDATA);
			if (!res) return false;
			HGLOBAL handle = ::LoadResource(nullptr, res);
			if (!handle) return false;
			const void* data = ::LockResource(handle);
			const DWORD size = ::SizeofResource(nullptr, res);
			if (!data || size == 0) return false;

			ComPtr<IWICImagingFactory> factory;
			if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(factory.GetAddressOf())))) return false;

			ComPtr<IWICStream> stream;
			if (FAILED(factory->CreateStream(stream.GetAddressOf()))) return false;
			if (FAILED(stream->InitializeFromMemory(
				const_cast<BYTE*>(static_cast<const BYTE*>(data)), size))) return false;

			ComPtr<IWICBitmapDecoder> decoder;
			if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
				WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) return false;

			ComPtr<IWICBitmapFrameDecode> frame;
			if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return false;

			// PBGRA, not BGRA: UpdateLayeredWindow blends with AC_SRC_ALPHA, which
			// expects the colour channels already multiplied by alpha. Handing it
			// straight (unpremultiplied) pixels puts a bright halo around every
			// antialiased edge of the logo.
			ComPtr<IWICFormatConverter> converter;
			if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf()))) return false;
			if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
				WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;

			if (FAILED(converter->GetSize(&outWidth, &outHeight))) return false;
			if (outWidth == 0 || outHeight == 0) return false;

			const UINT stride = outWidth * 4;
			outPixels.resize(static_cast<size_t>(stride) * outHeight);
			return SUCCEEDED(converter->CopyPixels(nullptr, stride,
				static_cast<UINT>(outPixels.size()), outPixels.data()));
		}

		void ApplyAlpha(HWND window, HDC sourceDc, SIZE size, POINT position, uint8_t alpha)
		{
			POINT src{ 0, 0 };
			BLENDFUNCTION blend = {};
			blend.BlendOp = AC_SRC_OVER;
			blend.SourceConstantAlpha = alpha;
			blend.AlphaFormat = AC_SRC_ALPHA;
			::UpdateLayeredWindow(window, nullptr, &position, &size, sourceDc, &src, 0, &blend, ULW_ALPHA);
		}

		// Ease-out cubic. A linear fade looks mechanical because the eye is far
		// more sensitive to the low end of the ramp than the high end.
		uint8_t FadeAlpha(float t)
		{
			t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
			const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
			return static_cast<uint8_t>(eased * 255.0f + 0.5f);
		}

		void SplashThread()
		{
			// The splash owns its own apartment: Show() can run before the engine
			// has initialised COM, and this thread must not depend on it having.
			const HRESULT coInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			const bool ownsCom = SUCCEEDED(coInit);

			std::vector<uint8_t> pixels;
			UINT width = 0, height = 0;
			bool ok = DecodeSplashPng(pixels, width, height);

			HWND    window     = nullptr;
			HDC     screenDc   = nullptr;
			HDC     memDc      = nullptr;
			HBITMAP bitmap     = nullptr;
			HBITMAP oldBitmap  = nullptr;
			POINT   position{ 0, 0 };
			SIZE    size{ static_cast<LONG>(width), static_cast<LONG>(height) };

			if (ok)
			{
				WNDCLASSEXW wc = { sizeof(wc) };
				wc.lpfnWndProc   = ::DefWindowProcW;
				wc.hInstance     = ::GetModuleHandleW(nullptr);
				wc.lpszClassName = kClassName;
				::RegisterClassExW(&wc);

				// Centre on the monitor holding the cursor, which is the one the
				// user is looking at on a multi-monitor desk.
				POINT cursor{ 0, 0 };
				::GetCursorPos(&cursor);
				HMONITOR monitor = ::MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
				MONITORINFO mi = { sizeof(mi) };
				::GetMonitorInfoW(monitor, &mi);
				position.x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - size.cx) / 2;
				position.y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - size.cy) / 2;

				// WS_EX_TOOLWINDOW keeps it out of the taskbar and out of alt-tab;
				// a splash that can be tabbed to is a splash that can be left
				// behind the main window.
				window = ::CreateWindowExW(
					WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
					kClassName, L"AttoEngine", WS_POPUP,
					position.x, position.y, size.cx, size.cy,
					nullptr, nullptr, wc.hInstance, nullptr);
				ok = window != nullptr;
			}

			if (ok)
			{
				screenDc = ::GetDC(nullptr);
				memDc    = ::CreateCompatibleDC(screenDc);

				BITMAPINFO bi = {};
				bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
				bi.bmiHeader.biWidth       = static_cast<LONG>(width);
				bi.bmiHeader.biHeight      = -static_cast<LONG>(height);   // top-down
				bi.bmiHeader.biPlanes      = 1;
				bi.bmiHeader.biBitCount    = 32;
				bi.bmiHeader.biCompression = BI_RGB;

				void* bits = nullptr;
				bitmap = ::CreateDIBSection(memDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
				if (bitmap && bits)
				{
					memcpy(bits, pixels.data(), pixels.size());
					oldBitmap = static_cast<HBITMAP>(::SelectObject(memDc, bitmap));
					::ShowWindow(window, SW_SHOWNOACTIVATE);
				}
				else ok = false;
			}

			State().visible.store(ok, std::memory_order_release);
			if (State().ready) ::SetEvent(State().ready);

			if (ok)
			{
				using clock = std::chrono::steady_clock;
				const auto shown = clock::now();
				auto elapsedMs = [](clock::time_point from) {
					return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - from).count();
				};

				// Fade in, then hold until both the minimum visible time has passed
				// and the engine has asked to be let through.
				while (!State().dismiss.load(std::memory_order_acquire) || elapsedMs(shown) < kMinVisibleMs)
				{
					const auto t = elapsedMs(shown);
					ApplyAlpha(window, memDc, size, position,
						t >= kFadeInMs ? uint8_t(255) : FadeAlpha(static_cast<float>(t) / kFadeInMs));
					MSG msg;
					while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
					{
						::TranslateMessage(&msg);
						::DispatchMessageW(&msg);
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(8));
				}

				const auto fadeStart = clock::now();
				for (;;)
				{
					const auto t = elapsedMs(fadeStart);
					if (t >= kFadeOutMs) break;
					ApplyAlpha(window, memDc, size, position,
						static_cast<uint8_t>(255 - FadeAlpha(static_cast<float>(t) / kFadeOutMs)));
					std::this_thread::sleep_for(std::chrono::milliseconds(8));
				}
			}

			State().visible.store(false, std::memory_order_release);
			if (memDc)
			{
				if (oldBitmap) ::SelectObject(memDc, oldBitmap);
				::DeleteDC(memDc);
			}
			if (bitmap)   ::DeleteObject(bitmap);
			if (screenDc) ::ReleaseDC(nullptr, screenDc);
			if (window)   ::DestroyWindow(window);
			::UnregisterClassW(kClassName, ::GetModuleHandleW(nullptr));
			if (ownsCom) ::CoUninitialize();
		}
	}

	void SplashScreen::Show()
	{
		SplashState& state = State();
		if (state.thread.joinable()) return;
		state.dismiss.store(false, std::memory_order_release);
		state.ready = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		state.thread = std::thread(SplashThread);
		// Wait for the window to exist so it cannot appear behind the main window
		// that the caller is about to create. Bounded, because a splash must never
		// be the reason the engine fails to start.
		if (state.ready) ::WaitForSingleObject(state.ready, 2000);
	}

	void SplashScreen::Hide()
	{
		SplashState& state = State();
		if (!state.thread.joinable()) return;
		state.dismiss.store(true, std::memory_order_release);
		state.thread.join();
		if (state.ready) { ::CloseHandle(state.ready); state.ready = nullptr; }
	}

	bool SplashScreen::IsVisible() { return State().visible.load(std::memory_order_acquire); }
}
