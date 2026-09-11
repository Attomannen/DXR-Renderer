#pragma once
#include "FullscreenEffect.h"
#include <tge/graphics/RenderTarget.h>

struct ID3D11Buffer;

class FullscreenPixelateEffect : public Tga::FullscreenEffect
{
	float myPixelSize = 16.0f;

	struct EffectBufferData
	{
		float pixelSize;
		Tga::Vector2ui resolution;
		float garbage;
	} myEffectBufferData;

	ComPtr<ID3D11Texture2D> myEffectTexture;
	ComPtr<ID3D11ShaderResourceView> myEffectSRV;
	ComPtr<ID3D11RenderTargetView> myEffectRTV;
	ComPtr<ID3D11Buffer> myEffectBuffer;
	D3D11_VIEWPORT* myViewport;

public:
	~FullscreenPixelateEffect();
	void SetPixelSize(float aPixelSize);
	virtual bool Init(const char* aPixelShaderPath) override;
	virtual void Render() override;
	void Activate(Tga::DepthBuffer* aDepth = nullptr);
};
