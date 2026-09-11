#pragma once
#include <array>
#include <memory>
#include <thread>
#include <wrl/client.h>

#include "FullscreenPixelateEffect.h"
using Microsoft::WRL::ComPtr;

namespace Tga
{
	class TextService;
	class DebugDrawer;
	class TextureManager;
	class Engine;
	class RenderObject;
	class SpriteDrawer;
	class LineDrawer;
	class CustomShapeDrawer;
	class ModelDrawer;
	class RenderObjectCustom;
	class Camera;
	class FullscreenEffect;
	class GraphicsStateStack;
	class DeferredRenderer;
	class RenderResourcePool;

	class GraphicsEngine
	{
	public:
		GraphicsEngine(void);
		~GraphicsEngine(void);
		GraphicsEngine& operator =(const Engine& anOther) = delete;
		static bool Start();
		static void Shutdown();

		static GraphicsEngine* GetInstance() { return ourInstance; }

		bool IsInitiated();
		bool BeginFrame();
		void EndFrame();

		SpriteDrawer& GetSpriteDrawer() const { return *mySpriteDrawer; };
		ModelDrawer& GetModelDrawer() const { return *myModelDrawer; };
		CustomShapeDrawer& GetCustomShapeDrawer() const { return *myCustomShapeDrawer; };
		LineDrawer& GetLineDrawer() const { return *myLineDrawer; };

		TextureManager& GetTextureManager() const { return *myTextureManager; }
		DebugDrawer& GetDebugDrawer() const { return *myDebugDrawer; }
		TextService& GetTextService() const { return *myTextService; }

		GraphicsStateStack& GetGraphicsStateStack() const { return *myGraphicsStateStack; };

		// Engine-owned deferred renderer (G-buffer + PBR resolve + tonemap composite).
		// Always constructed; check IsReady() before use. See DeferredRenderer.h.
		DeferredRenderer& GetDeferredRenderer() const { return *myDeferredRenderer; };

		// Frame-transient render-target pool shared by render-graph passes.
		RenderResourcePool& GetRenderResourcePool() const { return *myRenderResourcePool; };

		FullscreenEffect& GetFullscreenEffectCopy() const { return *myFullscreenCopy; };
		FullscreenEffect& GetFullscreenEffectTonemap() const { return *myFullscreenTonemap; };
		FullscreenEffect& GetFullscreenEffectVerticalGaussianBlur() const { return *myFullscreenVerticalGaussianBlur; };
		FullscreenEffect& GetFullscreenEffectHorizontalGaussianBlur() const { return *myFullscreenHorizontalGaussianBlur; };
		FullscreenPixelateEffect& GetFullscreenEffectPixelation() const { return *myFullscreenPixelateEffect; };
		FullscreenEffect& GetFullscreenEffectBanding() const { return *myFullscreenBandingEffect; };

		void SetFullScreen(bool aFullScreen);

	private:
		bool Init();

		static GraphicsEngine* ourInstance;

		std::unique_ptr<SpriteDrawer> mySpriteDrawer;
		std::unique_ptr<ModelDrawer> myModelDrawer;
		std::unique_ptr<CustomShapeDrawer> myCustomShapeDrawer;
		std::unique_ptr<LineDrawer> myLineDrawer;

		std::unique_ptr<GraphicsStateStack> myGraphicsStateStack;
		std::unique_ptr<DeferredRenderer> myDeferredRenderer;
		std::unique_ptr<RenderResourcePool> myRenderResourcePool;

		std::unique_ptr<FullscreenEffect> myFullscreenCopy;
		std::unique_ptr<FullscreenEffect> myFullscreenTonemap;
		std::unique_ptr<FullscreenEffect> myFullscreenVerticalGaussianBlur;
		std::unique_ptr<FullscreenEffect> myFullscreenHorizontalGaussianBlur;
		std::unique_ptr<FullscreenPixelateEffect> myFullscreenPixelateEffect;
		std::unique_ptr<FullscreenEffect> myFullscreenBandingEffect;

		std::unique_ptr<TextureManager> myTextureManager;
		std::unique_ptr<DebugDrawer> myDebugDrawer;
		std::unique_ptr<TextService> myTextService;

		bool myIsInitiated;
	};
}