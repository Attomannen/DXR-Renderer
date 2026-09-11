#pragma once
#include <array>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D11VertexShader;
struct ID3D11PixelShader;

namespace Tga
{
	struct VertexShader;
	struct PixelShader;

class FullscreenEffect
{
public:
	virtual ~FullscreenEffect() = default;
	FullscreenEffect();
	virtual bool Init(const char* aPixelShaderPath);
	virtual void Render();
private:
	const VertexShader* myVertexShader;
	const PixelShader* myPixelShader;
};

} // namespace Tga