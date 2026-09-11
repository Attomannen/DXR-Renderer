#include "stdafx.h"

#include <tge/Application.h>
#include <tge/debugging/MemoryTracker.h>
#include <tge/log/Log.h>
#include <tge/filewatcher/FileWatcher.h>
#include <tge/graphics/dx11.h>

#include <tge/windows/WindowsWindow.h>
#include <tge/settings/settings.h>

#define WIN32_LEAN_AND_MEAN 
#define NOMINMAX 
#include <windows.h>
#include <tge/ImGui/ImGuiInterface.h>

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

using namespace Tga; 
Application* Tga::Application::ourInstance = nullptr;
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
	Log::Destroy();
}


void Tga::Application::DestroyInstance()
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
	INFO_PRINT("%s", "---TGE Starting, dream big and dare to fail---");

#ifdef USE_LIVE_PP
	locLppAgent = lpp::LppCreateSynchronizedAgent(nullptr, L"../Source/External/LivePP");
	locIsLppValid = lpp::LppIsValidSynchronizedAgent(&locLppAgent);
	locLppAgent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_NONE, nullptr, nullptr);
#endif

	myFileWatcher = std::make_unique<FileWatcher>();
	myWindow = std::make_unique<WindowsWindow>();
	if (!myWindow->Init(myWindowConfiguration, myWindowConfiguration.hInstance, myWindowConfiguration.hwnd)) 
	{
		ERROR_PRINT("%s", "Window failed to be created!");
		return false;
	}

	myDx11 = std::make_unique<DX11>();
	if (!myDx11->Init(myWindow.get()))
	{
		ERROR_PRINT("%s", "D3D failed to be created!");
		myWindow->Close();
		return false;
	}

	CalculateRatios();
#ifndef _RETAIL
	ImGuiInterface::Init();
#endif // !_RETAIL

	myStartOfTime = std::chrono::steady_clock::now();


	return true;
}

void Tga::Application::Shutdown()
{
	ImGuiInterface::Shutdown();

	if (ourInstance)
	{
		DestroyInstance();
	}
}

void Tga::Application::UpdateWindowSizeChanges()
{	
	myDx11->ResizeToWindowSize();
	DX11::BackBuffer->SetAsActiveTarget();

	myWindowConfiguration.renderSize = DX11::GetResolution();

	RECT r;
	GetWindowRect(*myWindowConfiguration.hwnd, &r); //get window rect of control relative to screen
	myWindowConfiguration.windowSize = Vector2ui(r.right - r.left, r.bottom - r.top);

	CalculateRatios();
}

float Tga::Application::GetRenderSizeRatio() const
{
	return myRenderSizeRatio;
}

float Tga::Application::GetRenderSizeRatioInversed() const
{
	return myRenderSizeRatioInversed;
}

Vector2f Tga::Application::GetRenderSizeRatioVec() const
{
	return myRenderSizeRatioVec;
}

Vector2f Tga::Application::GetRenderSizeRatioInversedVec() const
{
	return myRenderSizeRatioInversedVec;
}

void Tga::Application::SetResolution(const Vector2ui &aResolution)
{
	myWindow->SetResolution(aResolution);

	UpdateWindowSizeChanges();
}

void Tga::Application::CalculateRatios()
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

HWND* Tga::Application::GetHWND() const
{
	return myWindowConfiguration.hwnd;
}


HINSTANCE Tga::Application::GetHInstance() const
{
	return myWindowConfiguration.hInstance;
}

void Tga::Application::SetClearColor(const Color& aClearColor)
{
	myWindowConfiguration.clearColor = aClearColor;
}

bool Tga::Application::IsDebugFeatureOn(DebugFeature aFeature) const
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

	MSG msg = { 0 };

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
#ifndef _RETAIL
	ImGuiInterface::PreFrame();
#endif // !_RETAIL
    myFileWatcher->FlushChanges();
	
	myDx11->BeginFrame(myWindowConfiguration.clearColor);
	DX11::ResetDrawCallCounter();

	return true;
}


void Application::EndFrame( void )
{
#ifndef _RETAIL
	DX11::BackBufferNoSrgbConversion->SetAsActiveTarget();

	ImGuiInterface::Render();

	DX11::BackBuffer->SetAsActiveTarget();

#endif // !_RETAIL

	myTimer.Tick([&]()
	{
		myDeltaTime = static_cast<float>(myTimer.GetElapsedSeconds());
		myTotalTime += static_cast<float>(myTimer.GetElapsedSeconds());
	});

	myDx11->EndFrame(myWindowConfiguration.enableVSync);

	if (myWantToUpdateSize)
	{
		UpdateWindowSizeChanges();
		myWantToUpdateSize = false;
	}
}
