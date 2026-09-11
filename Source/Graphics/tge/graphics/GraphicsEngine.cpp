#include "stdafx.h"

#include <tge/graphics/GraphicsEngine.h>
#include <tge/application.h>
#include <tge/log/Log.h>
#include <tge/drawers/CustomShapeDrawer.h>
#include <tge/drawers/LineDrawer.h>
#include <tge/drawers/ModelDrawer.h>
#include <tge/drawers/SpriteDrawer.h>
#include <tge/drawers/DebugDrawer.h>
#include <tge/text/TextService.h>
#include <tge/graphics/DX11.h>
#include <tge/graphics/FullscreenEffect.h>
#include <tge/graphics/GraphicsStateStack.h>
#include <tge/render/DeferredRenderer.h>
#include <tge/render/RenderResourcePool.h>
#include <tge/math/CommonMath.h>
#include <tge/render/RenderCommon.h>
#include <tge/render/RenderObject.h>
#include <tge/texture/texture.h>
#include <tge/texture/TextureManager.h>
#include <tge/windows/WindowsWindow.h>
#include <DDSTextureLoader/DDSTextureLoader11.h>
#include <tge/primitives/LinePrimitive.h>
#include <tge/EngineDefines.h>


#include <fstream>
#include <d3dcompiler.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <thread>

#include "FullscreenPixelateEffect.h"
#include "tge/model/ModelFactory.h"

#pragma comment (lib, "d3d11.lib")
#pragma comment (lib, "D3DCompiler.lib")

using namespace Tga;
GraphicsEngine* Tga::GraphicsEngine::ourInstance = nullptr;

bool GraphicsEngine::Start()
{
	if (!ourInstance)
	{
		ApplicationConfiguration cfg = Settings::GetApplicationConfiguration();

		ourInstance = new GraphicsEngine();
		return ourInstance->Init();
	}
	else
	{
		ERROR_PRINT("%s", "DX2D::Engine::CreateInstance called twice, that's bad.");
	}
	return false;
}

void Tga::GraphicsEngine::Shutdown()
{
	ModelFactory::DestroyInstance();

	if (ourInstance)
	{
		delete ourInstance;
		ourInstance = nullptr;
	}
}

GraphicsEngine::GraphicsEngine()
	: myIsInitiated(false)
{}

GraphicsEngine::~GraphicsEngine(void)
{}

bool GraphicsEngine::Init()
{
	INFO_PRINT("%s", "Starting graphics engine");

	DX11::BackBuffer->SetAsActiveTarget();

	myTextureManager = std::make_unique<TextureManager>();
	myTextureManager->Init();

	mySpriteDrawer = std::make_unique<SpriteDrawer>();
	mySpriteDrawer->Init();

	myModelDrawer = std::make_unique<ModelDrawer>();
	myModelDrawer->Init();

	myCustomShapeDrawer = std::make_unique<CustomShapeDrawer>();
	myCustomShapeDrawer->Init();

	myLineDrawer = std::make_unique<LineDrawer>();
	myLineDrawer->Init();

	// Force a model factory instance, since it register callbacks in core
	ModelFactory::GetInstance();

	myFullscreenCopy = std::make_unique<FullscreenEffect>();
	if (!myFullscreenCopy->Init("Shaders/PostprocessCopyPS"))
		return false;
	myFullscreenTonemap = std::make_unique<FullscreenEffect>();
	if (!myFullscreenTonemap->Init("Shaders/PostprocessTonemapPS"))
		return false;
	myFullscreenVerticalGaussianBlur = std::make_unique<FullscreenEffect>();
	if (!myFullscreenVerticalGaussianBlur->Init("Shaders/PostprocessGaussianV_PS"))
		return false;
	myFullscreenHorizontalGaussianBlur = std::make_unique<FullscreenEffect>();
	if (!myFullscreenHorizontalGaussianBlur->Init("Shaders/PostprocessGaussianH_PS"))
		return false;
	myFullscreenPixelateEffect = std::make_unique<FullscreenPixelateEffect>();
	if(!myFullscreenPixelateEffect->Init("Shaders/PostprocessPixelate_PS"))
		return false;

	myGraphicsStateStack = std::make_unique<GraphicsStateStack>();
	if (!myGraphicsStateStack->Init())
		return false;

	myRenderResourcePool = std::make_unique<RenderResourcePool>();

	// Engine-owned deferred renderer. Optional: if it fails to build, the forward
	// path still works, so log and carry on rather than aborting engine startup.
	myDeferredRenderer = std::make_unique<DeferredRenderer>();
	{
		const Vector2ui renderSize = Application::GetInstance()->GetRenderSize();
		if (!myDeferredRenderer->Init(renderSize))
			ERROR_PRINT("%s", "GraphicsEngine: deferred renderer unavailable; forward path only");
	}

	myDebugDrawer = std::make_unique<DebugDrawer>(
		(Settings::GetApplicationConfiguration().activateDebugSystems & (
			DebugFeature::Fps |
			DebugFeature::Mem |
			DebugFeature::Drawcalls |
			DebugFeature::Cpu |
			DebugFeature::FpsGraph |
			DebugFeature::Log))
		!= DebugFeature::None);

	myTextService = std::make_unique<TextService>();
	myTextService->Init();

	if (myDebugDrawer)
	{
		myDebugDrawer->Init();
	}

	INFO_PRINT("%s", "All done, starting...");
	INFO_PRINT("%s", "#########################################");
	myIsInitiated = true;
	return true;
}

bool GraphicsEngine::IsInitiated()
{
	return myIsInitiated;
}

void Tga::GraphicsEngine::SetFullScreen(bool aFullScreen)
{
	DX11::SwapChain->SetFullscreenState(aFullScreen, nullptr);
}
bool GraphicsEngine::BeginFrame()
{
	myTextureManager->Update();

	myGraphicsStateStack->BeginFrame();

	return true;
}

void GraphicsEngine::EndFrame()
{
	if (myDebugDrawer)
	{
		myDebugDrawer->Update(Application::GetInstance()->GetDeltaTime());
		myDebugDrawer->Render();
	}
}
