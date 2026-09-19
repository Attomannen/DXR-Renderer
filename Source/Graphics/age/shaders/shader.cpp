#include "stdafx.h"

#include <age/shaders/shader.h>
#include <age/application.h>
#include <age/log/Log.h>
#include <age/graphics/GraphicsEngine.h>
#include <age/graphics/GraphicsStateStack.h>
#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>
#include <age/shaders/ShaderCommon.h>
#include <age/filewatcher/FileWatcher.h>
#include <age/texture/TextureManager.h>
#include <age/EngineDefines.h>
#include <age/util/StringCast.h>

Ag::Shader::Shader()
	: myRandomSeed(rand() % 100)
{
	myIsReadyToRender = false;
}

Ag::Shader::~Shader()
{}

namespace
{
	using Ag::rhi::Format;
	constexpr uint32_t kAppend = ~0u;
	Ag::rhi::InputElement IE(const char* sem, uint32_t idx, Format fmt, uint32_t slot,
	                          uint32_t off, bool perInstance = false, uint32_t stepRate = 0)
	{
		return { sem, idx, fmt, slot, off, perInstance, stepRate };
	}
}

bool Ag::Shader::SetInputLayout(std::vector<rhi::InputElement> someElements, const std::string& aVSBlob)
{
	myInputElements = std::move(someElements);
	myLayout.Reset();

	rhi::IDevice* r = DX11::Rhi();
	if (!r || myInputElements.empty() || aVSBlob.empty())
		return false;

	// DX12 has no separate native input-layout object -- myInputElements
	// (already set above) is fed directly into ResolveGraphicsPipeline's
	// GraphicsPipelineDesc immediately before each draw (see
	// Dx12CommandContext::SetInputLayout/ResolveGraphicsPipeline). myLayout
	// (a DX11-only ID3D11InputLayout ComPtr) stays unused/empty on DX12.
	if (r->GetBackend() == rhi::Backend::DX12)
		return true;

	void* native = r->CreateInputLayoutNative(myInputElements.data(), (uint32_t)myInputElements.size(),
	                                          aVSBlob.data(), (uint32_t)aVSBlob.size());
	if (!native)
	{
		ERROR_PRINT("%s", "Layout error");
		return false;
	}
	myLayout.Attach(static_cast<ID3D11InputLayout*>(native)); // takes the AddRef from the backend
	return true;
}

bool Ag::Shader::CreateShaders(const char* aVertex, const char* aPixel, callback_layout aLayout)
{
	myIsReadyToRender = false;
	myVertexShaderFile = aVertex;
	myPixelShaderFile = aPixel;

	myVertexShader = DX11::LoadVertexShader(aVertex);
	if (!myVertexShader)
		return false;

	myPixelShader = DX11::LoadPixelShader(aPixel);
	if (!myPixelShader)
		return false;

	if (myLayout)
	{
		myLayout.Reset();
	}

	if (aLayout)
	{
		aLayout(myVertexShader->data);
	}
	else if (!CreateInputLayout(myVertexShader->data))
	{
		if (!SetInputLayout({
			IE("POSITION", 0, Format::R32G32B32_Float,    0, 0),
			IE("TEXCOORD", 0, Format::R32G32_Float,       0, kAppend),
			IE("TEXCOORD", 1, Format::R32G32_Float,       1, 0,      true, 1),
			IE("TEXCOORD", 2, Format::R32G32B32A32_Float, 1, kAppend, true, 1),
			IE("TEXCOORD", 3, Format::R32G32B32A32_Float, 1, kAppend, true, 1),
			IE("TEXCOORD", 4, Format::R32G32B32A32_Float, 1, kAppend, true, 1),
			IE("TEXCOORD", 5, Format::R32G32B32A32_Float, 1, kAppend, true, 1),
		}, myVertexShader->data))
			return false;
	}
	myIsReadyToRender = true;
	return true;
}

bool Ag::Shader::PrepareRender() const
{
	if (!myVertexShader || !myPixelShader || !myIsReadyToRender)
	{
		return false;
	}

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	ctx.SetVertexShader(myVertexShader->module);
	ctx.SetPixelShader(myPixelShader->module);
	ctx.SetInputLayout(myVertexShader->module, myInputElements.data(), (uint32_t)myInputElements.size());
	ctx.SetPrimitiveTopology(rhi::Topology::TriangleList);

	Ag::GraphicsEngine::GetInstance()->GetGraphicsStateStack().UpdateGpuStates();

	return true;
}
