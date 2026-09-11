#pragma once
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN 
#endif
#if !defined(NOMINMAX)
#define NOMINMAX 
#endif
#include <Windows.h>

#include <functional>
#include <tge/math/color.h>
#include <tge/math/vector2.h>
#include <chrono>
#include "StepTimer.h"
#include <tge/EngineDefines.h>
#include <tge/settings/settings.h>

namespace Tga
{
    class FileWatcher;
    class DX11;

#ifndef _RETAIL
    class ImGuiInterface;
#endif // !_RETAIL
}

namespace Tga
{
    class Application
    {
		friend class WindowsWindow;

    public:
        Application &operator =( const Application& anOther ) = delete;
		static bool Start();
		static void Shutdown();

        static Application* GetInstance() {return ourInstance;}

		Vector2ui GetWindowSize() const { return myWindowConfiguration.windowSize; }
		Vector2ui GetRenderSize() const { return myWindowConfiguration.renderSize; }

        float GetRenderSizeRatio() const;
        float GetRenderSizeRatioInversed() const;

		float GetDeltaTime() const { return myDeltaTime; }
        float GetTotalTime() const { return myTotalTime; }
        HWND* GetHWND() const;
        HINSTANCE GetHInstance() const;
      
		bool IsDebugFeatureOn(DebugFeature aFeature) const;

		void SetResolution(const Vector2ui &aResolution);

		void SetClearColor(const Color& aClearColor);
        const Color& GetClearColor() const { return myWindowConfiguration.clearColor; }
		// If you want to manually tell the engine to render instead of the callback function with the (myUpdateFunctionToCall)
		bool BeginFrame();
		void EndFrame(void);

        FileWatcher* GetFileWatcher() { return myFileWatcher.get(); }

    private:    // Private interface
        Application();
        ~Application();

        static void DestroyInstance();

		WindowsWindow& GetWindow() const { return *myWindow; }
		
		bool InternalStart();

        void CalculateRatios();
		void SetWantToUpdateSize() { myWantToUpdateSize = true; }

		void UpdateWindowSizeChanges();

		Vector2f GetRenderSizeRatioVec() const;
		Vector2f GetRenderSizeRatioInversedVec() const;

    private:    // Private data
        static Application* ourInstance;

        std::unique_ptr<WindowsWindow> myWindow;
        std::unique_ptr<DX11> myDx11;
        ApplicationConfiguration myWindowConfiguration;

        std::unique_ptr<FileWatcher> myFileWatcher;
		bool myWantToUpdateSize;

        bool myRunApplication;
        float myRenderSizeRatio;
        float myRenderSizeRatioInversed;
		Vector2f myRenderSizeRatioVec;
		Vector2f myRenderSizeRatioInversedVec;

		std::chrono::steady_clock::time_point myStartOfTime;
        float myTotalTime;
        float myDeltaTime;

		DX::StepTimer myTimer;

		bool myShouldExit; // Only used when using beginframe and endframe
    };
}
