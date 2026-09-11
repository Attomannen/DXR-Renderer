#pragma once
#include <string>
#include <wrl/client.h>
#include "tge/rhi/Handles.h"

struct ID3D11PixelShader;
struct ID3D11VertexShader;
struct ID3D11ComputeShader;

namespace Tga
{
	// Cached compiled shaders. Held by DX11's Load*Shader map. During the DX11->DX12
	// migration each entry carries BOTH the raw D3D11 shader object (for call sites
	// not yet moved to the RHI) and an rhi::ShaderModuleHandle (for those that have).
	// The raw `shader` members go away once shader.cpp / DeferredRenderer migrate.
	struct PixelShader
	{
		Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
		rhi::ShaderModuleHandle module;
	};
	struct VertexShader
	{
		Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
		std::string data;                 // .cso bytes, kept for input-layout reflection
		rhi::ShaderModuleHandle module;
	};
	struct ComputeShader
	{
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
		rhi::ShaderModuleHandle module;
	};
}
