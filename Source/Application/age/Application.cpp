#include "stdafx.h"
#include <age/debugging/CpuProfiler.h>

#include <age/Application.h>
#include <age/debugging/MemoryTracker.h>
#include <age/log/Log.h>
#include <age/filewatcher/FileWatcher.h>
#include <age/graphics/dx11.h>
#include <age/graphics/StreamlineDLSS.h>
#include <age/rhi/Device.h>

#include <age/windows/WindowsWindow.h>
#include <age/settings/settings.h>

#define WIN32_LEAN_AND_MEAN 
#define NOMINMAX 
#include <windows.h>
#include <timeapi.h>
#include <age/ImGui/ImGuiInterface.h>

#pragma comment( lib, "user32.lib" )

#ifndef _RETAIL
// Uncomment this define to use Live++:
// You also need to download Live++ and place the folder LivePP in Source/External
//#define USE_LIVE_PP
#endif

#ifdef USE_LIVE_PP
#include <LivePP/API/x64/LPP_API_x64_CPP.h>
static lpp::LppSynchronizedAgent locLppAgent;
static bool locIsLppValid;
#endif

using namespace Ag; 
std::chrono::steady_clock::time_point Ag::Application::ourWindowCreatedAt{};
bool Ag::Application::ourFirstPresentReported = false;

Application* Ag::Application::ourInstance = nullptr;
Application::Application()
: myWindow(nullptr)
, myRunApplication(true)
, myTotalTime(0.0f)
, myDeltaTime(0.0f)
, myShouldExit(false)
, myWantToUpdateSize(false)
{

	{ // if specified for incomming parameters, prioritize
		myWindowConfiguration = Settings::GetApplicationConfiguration();
		ApplicationConfiguration &cfg = myWindowConfiguration;
		{
			// if fullscreen we should not care what the settings say for the window size..
			if (cfg.startInFullScreen || cfg.startMaximized) {
				int screenWidth = GetSystemMetrics(SM_CXSCREEN);
				int screenHeight = GetSystemMetrics(SM_CYSCREEN);
				cfg.windowSize = cfg.renderSize = { (uint32_t)screenWidth, (uint32_t)screenHeight };
			}
		}
		
		myWindowConfiguration.hwnd = cfg.hwnd;
		myWindowConfiguration.hInstance = cfg.hInstance;
	}
	Log::Create();
}


Application::~Application()
{
	// Streamline owns GPU-side state, so it must shut down before the DX12
	// device held by myDx11 is released during member destruction.
	StreamlineDLSS::Get().Shutdown();
	Log::Destroy();
}


void Ag::Application::DestroyInstance()
{
	if (ourInstance)
	{
#ifdef USE_LIVE_PP
		if (locIsLppValid)
		{
			lpp::LppDestroySynchronizedAgent(&locLppAgent);
		}
#endif
		delete ourInstance;
		ourInstance = nullptr;
		StopMemoryTrackingAndPrint();
	}
}

bool Application::Start()
{
	if (!ourInstance)
	{
		ApplicationConfiguration cfg = Settings::GetApplicationConfiguration();
		MemoryTrackingSettings trackingSettings;
		trackingSettings.shouldTrackAllAllocations = ((cfg.activateDebugSystems & DebugFeature::MemoryTrackingAllAllocations) != DebugFeature::None);
		trackingSettings.shouldStoreStackTraces = ((cfg.activateDebugSystems & DebugFeature::MemoryTrackingStackTraces) != DebugFeature::None);
		StartMemoryTracking(trackingSettings);

		ourInstance = new Application();
		return ourInstance->InternalStart();
	}
	else
	{
		ERROR_PRINT("%s", "DX2D::Application::CreateInstance called twice, thats bad.");
	}
	return false;
}

bool Application::InternalStart()
{
	INFO_PRINT("%s", "#########################################");
	INFO_PRINT("%s", "---AttoEngine Starting, dream big and dare to fail---");

#ifdef USE_LIVE_PP
	locLppAgent = lpp::LppCreateSynchronizedAgent(nullptr, L"../Source/External/LivePP");
	locIsLppValid = lpp::LppIsValidSynchronizedAgent(&locLppAgent);
	locLppAgent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_NONE, nullptr, nullptr);
#endif

	myFileWatcher = std::make_unique<FileWatcher>();
	// Startup cost that matters is measured from HERE, not from process start.
	// The window is created before the device, Streamline, the graphics engine
	// and the scene load, so it sits on screen blank for all of it -- and a
	// blank window is what the user is actually waiting through. Profiling
	// scopes above this line are invisible to them; everything after is not.
	Application::ourWindowCreatedAt = std::chrono::steady_clock::now();
	myWindow = std::make_unique<WindowsWindow>();
	if (!myWindow->Init(myWindowConfiguration, myWindowConfiguration.hInstance, myWindowConfiguration.hwnd)) 
	{
		ERROR_PRINT("%s", "Window failed to be created!");
		return false;
	}

	myDx11 = std::make_unique<DX11>();
	// Streamline's interposer has to be initialized before the DXGI/D3D12
	// bootstrap.  It is optional: missing DLLs simply retain native TAA.
	// Gated on a setting, because it cannot be made lazy. Streamline interposes
	// on DXGI/D3D12, so it has to load before the device exists -- there is no
	// point later at which it could be brought up on demand. That makes it a
	// flat 4.9 s cost on every launch, for a feature that is off by default.
	const bool wantUpscaling = Settings::GetApplicationConfiguration().enableUpscaling;
	if (wantUpscaling)
	{
		AG_CPU_SCOPE("Streamline init");
		StreamlineDLSS::Get().Initialize();
	}
	AG_CPU_SCOPE("Device + ImGui init");
	if (!myDx11->Init(myWindow.get()))
	{
		ERROR_PRINT("%s", "D3D failed to be created!");
		myWindow->Close();
		return false;
	}
	if (rhi::IDevice* rhiDevice = DX11::Rhi(); wantUpscaling && rhiDevice && rhiDevice->GetBackend() == rhi::Backend::DX12)
	{
		AG_CPU_SCOPE("Streamline attach device");
		StreamlineDLSS::Get().AttachD3D12Device(rhiDevice->GetNativeDevice());
	}

	CalculateRatios();
#ifndef _RETAIL
	{
		AG_CPU_SCOPE("ImGui init");
		ImGuiInterface::Init();
	}
#endif // !_RETAIL

	myStartOfTime = std::chrono::steady_clock::now();

#pragma comment(lib, "winmm.lib")
	timeBeginPeriod(1);

	return true;
}

void Ag::Application::Shutdown()
{
	ImGuiInterface::Shutdown();
	timeEndPeriod(1);

	if (ourInstance)
	{
		DestroyInstance();
	}
}

void Ag::Application::UpdateWindowSizeChanges()
{	
	// A minimized window reports a 0x0 client area.  Keep the last valid
	// backbuffer and defer all dependent resolution updates until restoration.
	#ifndef _RETAIL
	ImGuiInterface::OnResizeBegin();
	#endif
	if (!myDx11->ResizeToWindowSize())
	{
		#ifndef _RETAIL
		ImGuiInterface::OnResizeEnd();
		#endif
		return;
	}
	#ifndef _RETAIL
	ImGuiInterface::OnResizeEnd();
	#endif
	// Only if a command list is actually open. This runs after EndFrame has
	// closed and submitted DX12's list, so binding here recorded viewport and
	// render-target commands into a closed list. Older D3D12 runtimes ignored
	// that; the Agility runtime NRD requires faults on it inside the driver.
	// The next BeginFrame binds its own targets anyway.
	if (DX11::Rhi() && DX11::Rhi()->IsRecording())
		DX11::BackBuffer->SetAsActiveTarget();

	myWindowConfiguration.renderSize = DX11::GetResolution();

	RECT r;
	GetWindowRect(*myWindowConfiguration.hwnd, &r); //get window rect of control relative to screen
	myWindowConfiguration.windowSize = Vector2ui(r.right - r.left, r.bottom - r.top);

	CalculateRatios();
}

float Ag::Application::GetRenderSizeRatio() const
{
	return myRenderSizeRatio;
}

float Ag::Application::GetRenderSizeRatioInversed() const
{
	return myRenderSizeRatioInversed;
}

Vector2f Ag::Application::GetRenderSizeRatioVec() const
{
	return myRenderSizeRatioVec;
}

Vector2f Ag::Application::GetRenderSizeRatioInversedVec() const
{
	return myRenderSizeRatioInversedVec;
}

void Ag::Application::SetResolution(const Vector2ui &aResolution)
{
	myWindow->SetResolution(aResolution);

	UpdateWindowSizeChanges();
}

void Ag::Application::CalculateRatios()
{
	float sizeX = static_cast<float>(myWindowConfiguration.renderSize.x);
	float sizeY = static_cast<float>(myWindowConfiguration.renderSize.y);
	if (sizeY > sizeX)
	{
		float temp = sizeX;
		sizeX = sizeY;
		sizeY = temp;
	}

	myRenderSizeRatio = static_cast<float>(sizeX) / static_cast<float>(sizeY);
	myRenderSizeRatioInversed = static_cast<float>(sizeY) / static_cast<float>(sizeX);
	
	myRenderSizeRatioVec.x = 1.0f;
	myRenderSizeRatioVec.y = 1.0f;
	myRenderSizeRatioInversedVec.x = 1.0f;
	myRenderSizeRatioInversedVec.y = 1.0f;
	if (sizeX >= sizeY)
	{
		myRenderSizeRatioVec.y = myRenderSizeRatio;
		myRenderSizeRatioInversedVec.y = myRenderSizeRatioInversed;
	}
	else
	{
		myRenderSizeRatioVec.x = myRenderSizeRatio;
		myRenderSizeRatioInversedVec.x = myRenderSizeRatioInversed;
	}
}

HWND* Ag::Application::GetHWND() const
{
	return myWindowConfiguration.hwnd;
}


HINSTANCE Ag::Application::GetHInstance() const
{
	return myWindowConfiguration.hInstance;
}

void Ag::Application::SetClearColor(const Color& aClearColor)
{
	myWindowConfiguration.clearColor = aClearColor;
}

bool Ag::Application::IsDebugFeatureOn(DebugFeature aFeature) const
{
	const bool all = ((myWindowConfiguration.activateDebugSystems & DebugFeature::All) == DebugFeature::All);
	if (all)
	{
		return true;
	}

	const bool specific = ((myWindowConfiguration.activateDebugSystems & aFeature) != DebugFeature::None);
	return specific;
}

bool Application::BeginFrame()
{
	if (myShouldExit)
	{
		return false;
	}

#ifdef USE_LIVE_PP

	if (locIsLppValid && locLppAgent.WantsReload(lpp::LPP_RELOAD_OPTION_SYNCHRONIZE_WITH_RELOAD))
	{
		locLppAgent.Reload(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);
	}

#endif

	CpuProfiler::Get().BeginFrame();

	MSG msg = { 0 };

	{
		AG_CPU_SCOPE("Message pump");
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
			{
				INFO_PRINT("%s", "Exiting...");
				myShouldExit = true;
				return false;
			}
		}
	}
#ifndef _RETAIL
	{
		AG_CPU_SCOPE("ImGui new frame");
		ImGuiInterface::PreFrame();
	}
#endif // !_RETAIL
	{
		AG_CPU_SCOPE("File watcher");
		myFileWatcher->FlushChanges();
	}

	// Includes waiting on the swap chain's frame-latency object, i.e. time the
	// CPU spends blocked on the GPU.
	{
		AG_CPU_SCOPE("Device begin frame (GPU wait)");
		myDx11->BeginFrame(myWindowConfiguration.clearColor);
	}
	DX11::ResetDrawCallCounter();

	return true;
}


void Application::EndFrame( void )
{
#ifndef _RETAIL
	DX11::BackBufferNoSrgbConversion->SetAsActiveTarget();

	{
		AG_CPU_SCOPE("ImGui render");
		ImGuiInterface::Render();
	}

	DX11::BackBuffer->SetAsActiveTarget();

#endif // !_RETAIL

	myTimer.Tick([&]()
	{
		myDeltaTime = static_cast<float>(myTimer.GetElapsedSeconds());
		myTotalTime += static_cast<float>(myTimer.GetElapsedSeconds());
	});

	{
		AG_CPU_SCOPE("Submit + present");
		myDx11->EndFrame(myWindowConfiguration.enableVSync);
	}
	CpuProfiler::Get().EndFrame();

	// Keep resizing at the established post-present boundary. DX12 owns the
	// command-list lifetime between BeginFrame/EndFrame; resizing before its
	// next BeginFrame can dereference a closed transient command context.
	if (myWantToUpdateSize)
	{
		UpdateWindowSizeChanges();
		myWantToUpdateSize = false;
	}
}
