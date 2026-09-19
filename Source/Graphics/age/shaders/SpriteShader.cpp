#include "stdafx.h"
#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>
#include <age/graphics/GraphicsEngine.h>
#include <age/shaders/SpriteShader.h>
#include <age/sprite/sprite.h>
#include <age/shaders/ShaderCommon.h>
#include <age/texture/texture.h>
#include <age/texture/TextureManager.h>
#include <age/render/RenderObject.h>
#include <d3dcommon.h>
#include <d3d11.h>
#include <age/application.h>
#include <age/log/Log.h>


Ag::SpriteShader::SpriteShader()
	: Shader()
{
	myCurrentDataIndex = -1;
	myBufferIndex = (int)ShaderDataBufferIndex::Index_1;
	myCurrentTextureCount = 0;
}

bool Ag::SpriteShader::Init(const char* aVertex, const char* aPixel)
{
	if (!CreateShaders(aVertex, aPixel))
	{
		return false;
	}

	// b5..b8 custom sprite data is a per-frame dynamic allocation now.

	GraphicsEngine& engine = *GraphicsEngine::GetInstance();
	myDefaultTextures[0] = engine.GetTextureManager().GetTexture("Textures/T_Default_n.dds");
	myDefaultTextures[1] = engine.GetTextureManager().GetTexture("Textures/T_Default_m.dds");
	myDefaultTextures[2] = engine.GetTextureManager().GetTexture("Textures/T_Default_fx.dds");

	return true;
}

void Ag::SpriteShader::SetDataBufferIndex(ShaderDataBufferIndex aBufferRegisterIndex)
{
	myBufferIndex = (unsigned char)aBufferRegisterIndex;
}

void Ag::SpriteShader::SetShaderdataFloat4(Ag::Vector4f someData, ShaderDataID aID)
{
	if (aID > MAX_SHADER_DATA)
	{
		ERROR_PRINT("DX2D::CCustomShader::SetShaderdataFloat4() The id is bigger than allowed size");
		return;
	}
	myCustomData[aID] = someData;
	if (aID > myCurrentDataIndex)
	{
		myCurrentDataIndex = aID;
	}
}

void Ag::SpriteShader::SetTextureAtRegister(Ag::TextureResource* aTexture, ShaderTextureSlot aRegisterIndex)
{
	myBoundTextures[aRegisterIndex - ShaderTextureSlot::ShaderTextureSlot_1] = BoundTexture(aTexture, (unsigned char)aRegisterIndex);

	if (myCurrentTextureCount < (aRegisterIndex - ShaderTextureSlot::ShaderTextureSlot_1) + 1)
	{
		myCurrentTextureCount = static_cast<unsigned char>((aRegisterIndex - ShaderTextureSlot::ShaderTextureSlot_1) + 1);
	}
}

bool Ag::SpriteShader::PrepareRender(const SpriteSharedData& aSharedData) const
{
	aSharedData;
	if (!Shader::PrepareRender())
		return false;


	rhi::IDevice& dev = *DX11::Rhi();
	rhi::ICommandContext& ctx = dev.GetContext();
	GraphicsEngine& engine = *GraphicsEngine::GetInstance();

	rhi::SrvHandle textures[1 + ShaderMap::MAP_MAX];
	textures[0] = aSharedData.texture ? aSharedData.texture->GetSrv()
	                                  : engine.GetTextureManager().GetWhiteSquareTexture()->GetSrv();
	for (unsigned short index = 0; index < ShaderMap::MAP_MAX; index++)
	{
		textures[1 + index] = aSharedData.maps[index] ? aSharedData.maps[index]->GetSrv()
		                                              : myDefaultTextures[index]->GetSrv();
	}
	ctx.SetShaderResources(rhi::ShaderStage::Pixel, 1, 1 + ShaderMap::MAP_MAX, textures);

	{
		rhi::DynamicAlloc a = dev.AllocateDynamicConstants(myCustomData, sizeof(myCustomData));
		ctx.SetDynamicConstantBuffer(rhi::ShaderStage::AllGraphics, myBufferIndex, a);
	}

	for (int i = 0; i < myCurrentTextureCount; i++)
	{
		ctx.SetShaderResource(rhi::ShaderStage::Pixel, myBoundTextures[i].index, myBoundTextures[i].texture->GetSrv());
	}

	return true;
}

bool Ag::SpriteShader::CreateInputLayout(const std::string& aVS)
{
	using F = rhi::Format;
	constexpr uint32_t A = ~0u; // append
	// slot-1 per-instance float4 stream, semantic TEXCOORD<idx>
	auto Inst = [](uint32_t idx) -> rhi::InputElement { return { "TEXCOORD", idx, F::R32G32B32A32_Float, 1, A, true, 1 }; };

	SetInputLayout({
		{ "POSITION", 0, F::R32G32B32A32_Float, 0, 0,      false, 0 },
		{ "TEXCOORD", 0, F::R32G32B32A32_UInt,  0, A,      false, 0 },
		Inst(1), Inst(2), Inst(3), Inst(4), Inst(5), Inst(6), Inst(7),
	}, aVS);
	return true;
}


