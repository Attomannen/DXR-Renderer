#pragma once
#include "FullscreenEffect.h"
#include <age/graphics/RenderTarget.h>
#include <age/rhi/ConstantBuffer.h>
#include <age/rhi/Handles.h>

class FullscreenPixelateEffect : public Ag::FullscreenEffect
{
	float myPixelSize = 16.0f;

	struct EffectBufferData
	{
		float pixelSize;
		Ag::Vector2ui resolution;
		float garbage;
	} myEffectBufferData;

	Ag::rhi::TextureHandle myEffectTexture;
	Ag::rhi::SrvHandle myEffectSrv;
	Ag::rhi::RtvHandle myEffectRtv;
	Ag::rhi::ConstantBuffer myEffectBuffer;
	float myViewportWidth = 0.f;
	float myViewportHeight = 0.f;

public:
	void SetPixelSize(float aPixelSize);
	virtual bool Init(const char* aPixelShaderPath) override;
	virtual void Render() override;
	void Activate(Ag::DepthBuffer* aDepth = nullptr);
};
