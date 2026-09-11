#pragma once
#include "FullscreenEffect.h"
#include <tge/graphics/RenderTarget.h>
#include <tge/rhi/ConstantBuffer.h>
#include <tge/rhi/Handles.h>

class FullscreenPixelateEffect : public Tga::FullscreenEffect
{
	float myPixelSize = 16.0f;

	struct EffectBufferData
	{
		float pixelSize;
		Tga::Vector2ui resolution;
		float garbage;
	} myEffectBufferData;

	Tga::rhi::TextureHandle myEffectTexture;
	Tga::rhi::SrvHandle myEffectSrv;
	Tga::rhi::RtvHandle myEffectRtv;
	Tga::rhi::ConstantBuffer myEffectBuffer;
	float myViewportWidth = 0.f;
	float myViewportHeight = 0.f;

public:
	void SetPixelSize(float aPixelSize);
	virtual bool Init(const char* aPixelShaderPath) override;
	virtual void Render() override;
	void Activate(Tga::DepthBuffer* aDepth = nullptr);
};
