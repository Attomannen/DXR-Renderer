#include "stdafx.h"
#include "FullscreenPixelateEffect.h"

#include <tge/graphics/DX11.h>
#include "GraphicsEngine.h"
#include <tge/application.h>
using namespace Tga;

void FullscreenPixelateEffect::SetPixelSize(float aPixelSize)
{
	myPixelSize = aPixelSize;
}

bool FullscreenPixelateEffect::Init(const char* aPixelShaderPath)
{
	rhi::IDevice& dev = *DX11::Rhi();

	myEffectBuffer.Create(dev, sizeof(EffectBufferData), rhi::ShaderStage::Pixel, 13, "PixelateEffectCb");

	const Vector2ui res = dev.GetResolution();
	myViewportWidth = static_cast<float>(res.x);
	myViewportHeight = static_cast<float>(res.y);

	rhi::TextureDesc desc = {};
	desc.width = res.x;
	desc.height = res.y;
	desc.format = rhi::Format::B8G8R8A8_UNorm;   // matches the swapchain's resource format
	desc.bind = rhi::TextureBind::RenderTarget | rhi::TextureBind::ShaderResource | rhi::TextureBind::UnorderedAccess;
	desc.debugName = "PixelateEffectTex";

	myEffectTexture = dev.CreateTexture(desc);
	if (!myEffectTexture.IsValid())
		return false;

	myEffectSrv = dev.CreateSrv(myEffectTexture, {});
	myEffectRtv = dev.CreateRtv(myEffectTexture, {});

	return FullscreenEffect::Init(aPixelShaderPath);
}

void FullscreenPixelateEffect::Render()
{
	// We should not be the RTV here or we won't get any effects.
	// For now we can just force backbuffer :P Easier and we don't
	// allow depth testing here.
	DX11::BackBuffer->SetAsActiveTarget();

	myEffectBufferData.pixelSize = myPixelSize;
	myEffectBufferData.resolution = Application::GetInstance()->GetRenderSize();

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	myEffectBuffer.Update(ctx, myEffectBufferData);
	myEffectBuffer.Bind(ctx);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 0, myEffectSrv);

	FullscreenEffect::Render();
}

void FullscreenPixelateEffect::Activate(DepthBuffer* aDepth)
{
	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();

	const Color clearColor = Application::GetInstance()->GetClearColor();
	ctx.ClearRenderTarget(myEffectRtv, &clearColor.myR);

	rhi::DsvHandle dsv = aDepth ? aDepth->GetDsv() : rhi::DsvHandle{};
	ctx.SetRenderTargets(1, &myEffectRtv, dsv);

	ctx.SetViewport(0.f, 0.f, myViewportWidth, myViewportHeight);
}
