#include "stdafx.h"
#include "FullscreenEffect.h"

#include <fstream>

#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>
#include <age/graphics/GraphicsEngine.h>
#include <age/graphics/GraphicsStateStack.h>
#include <age/application.h>

using namespace Ag;

FullscreenEffect::FullscreenEffect()
{

}

bool FullscreenEffect::Init(const char* aPixelShaderPath)
{
	std::string vsData;
	myVertexShader = DX11::LoadVertexShader("Shaders/PostprocessVS");
	if (!myVertexShader)
		return false;

	myPixelShader = DX11::LoadPixelShader(aPixelShaderPath);

	if (!myPixelShader)
		return false;

	return true;
}

void FullscreenEffect::Render()
{
	auto& graphicsStateStack = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	graphicsStateStack.UpdateGpuStates();

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	ctx.SetPrimitiveTopology(rhi::Topology::TriangleList);
	ctx.SetInputLayout({}, nullptr, 0);
	ctx.SetVertexBuffer(0, {}, 0, 0);
	ctx.SetIndexBuffer({}, rhi::Format::R32_UInt, 0);
	ctx.SetVertexShader(myVertexShader->module);
	ctx.SetPixelShader(myPixelShader->module);
	ctx.Draw(3, 0);   // fullscreen triangle from SV_VertexID
}