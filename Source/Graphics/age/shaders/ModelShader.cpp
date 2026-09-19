#include "stdafx.h"

#include <age/shaders/ModelShader.h>
#include <age/application.h>
#include <age/graphics/GraphicsEngine.h>
#include <age/graphics/GraphicsStateStack.h>
#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>
#include <age/model/model.h>
#include <age/texture/texture.h>
#include <age/texture/TextureManager.h>
#include <age/log/Log.h>
#include <age/render/RayTracingMaterialTable.h>

#include <algorithm>
#include <cctype>

using namespace Ag;

namespace
{
	constexpr uint32_t kMaterialConstantSlot = 11;   // MaterialBuffer in MaterialParams.hlsli users
}

Ag::ModelShader::ModelShader()
	: Shader()
{
}

ModelShader::~ModelShader()
{
}

struct CommonBuf
{
	Matrix4x4f obToWorld;
};

bool Ag::ModelShader::Init()
{
	return Init("shaders/model_shader_VS", "shaders/model_shader_PS");
}

bool  Ag::ModelShader::Init(const char* aVertexShaderFile, const char* aPixelShaderFile)
{
	// Per-object (b4) and bone-palette (b5) constants are per-frame dynamic
	// allocations now — nothing to create here.
	std::string vs = aVertexShaderFile ? aVertexShaderFile : "";
	std::transform(vs.begin(), vs.end(), vs.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
	myVertexFormat = vs.find("animated") != std::string::npos ? Model::VertexFormat::Full : Model::VertexFormat::Compact;
	return Shader::CreateShaders(aVertexShaderFile, aPixelShaderFile, nullptr);
}

void Ag::ModelShader::RenderSetup(const Matrix4x4f& aObToWorld, const Matrix4x4f* someBones) const
{
	if (!myIsReadyToRender)
	{
		return;
	}

	Shader::PrepareRender();

	rhi::IDevice& dev = *DX11::Rhi();
	rhi::ICommandContext& ctx = dev.GetContext();

	// Bone palette is only meaningful for the animated shader variants. Skip the
	// 8 KB alloc + bind entirely for static meshes.
	if (someBones)
	{
		rhi::DynamicAlloc a = dev.AllocateDynamicConstants(someBones, sizeof(Matrix4x4f) * MAX_ANIMATION_BONES);
		ctx.SetDynamicConstantBuffer(rhi::ShaderStage::Vertex, (uint32_t)ConstantBufferSlot::Bones, a);
	}

	GraphicsStateStack& graphicsStateStack = Ag::GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	graphicsStateStack.BindLightingTextures();
	CommonBuf obj;
	obj.obToWorld = aObToWorld * graphicsStateStack.GetTransform();
	rhi::DynamicAlloc a = dev.AllocateDynamicConstants(&obj, sizeof(obj));
	ctx.SetDynamicConstantBuffer(rhi::ShaderStage::AllGraphics, (uint32_t)ConstantBufferSlot::Object, a);
}

void Ag::ModelShader::RenderMesh(const TextureResource* const* someTextures, const Model::MeshData& aModelData, uint32_t aMaterialIndex) const
{
	if (!myIsReadyToRender || !aModelData.vertexBuffer.IsValid() || !aModelData.indexBuffer.IsValid() || aModelData.numberOfIndices == 0)
	{
		return;
	}
	if (aModelData.rayGeometry.vertexFormat != myVertexFormat)
	{
		// A skinned mesh through a static shader (or the reverse) would read
		// the vertex buffer with the wrong layout.
		static bool warned = false;
		if (!warned)
		{
			warned = true;
			ERROR_PRINT("ModelShader: mesh '%s' vertex format does not match the shader; skipped", aModelData.name.GetString());
		}
		return;
	}

	rhi::SrvHandle resourceViews[4];
	int i = 0;
	while (i < 4 && someTextures[i] != nullptr)
	{
		resourceViews[i] = someTextures[i]->GetSrv();
		i++;
	}

	rhi::IDevice& dev = *DX11::Rhi();
	rhi::ICommandContext& ctx = dev.GetContext();
	ctx.SetShaderResources(rhi::ShaderStage::Pixel, 1, (uint32_t)i, resourceViews);

	// Material parameters (MaterialParams.hlsli) on b11.
	const uint32_t materialIndex = ourMaterialOverride ? ourMaterialOverride
		: aMaterialIndex ? aMaterialIndex : aModelData.rayGeometry.materialIndex;
	const MaterialParams& params = RayTracingMaterialTable::GetMaterialParams(materialIndex);
	ctx.SetDynamicConstantBuffer(rhi::ShaderStage::Pixel, kMaterialConstantSlot,
		dev.AllocateDynamicConstants(&params, sizeof(params)));
	ctx.SetIndexBuffer(aModelData.indexBuffer, rhi::Format::R32_UInt, 0);
	ctx.SetVertexBuffer(0, aModelData.vertexBuffer, aModelData.stride, 0);

	// ctx.DrawIndexed() already calls DX11::LogDrawCall() internally.
	ctx.DrawIndexed(aModelData.numberOfIndices, 0, 0);
}

void Ag::ModelShader::Render(const TextureResource* const* someTextures, const Model::MeshData& aModelData, const Matrix4x4f& aObToWorld, const Matrix4x4f* someBones) const
{
	RenderSetup(aObToWorld, someBones);
	RenderMesh(someTextures, aModelData, 0);
}


bool Ag::ModelShader::CreateInputLayout(const std::string& aVS)
{
	using F = rhi::Format;
	constexpr uint32_t A = ~0u; // append
	auto V = [](const char* s, uint32_t i, F f) -> rhi::InputElement { return { s, i, f, 0, A, false, 0 }; };

	if (myVertexFormat == Model::VertexFormat::Compact)
	{
		// MeshVertex, see Vertex.h.
		SetInputLayout({
			V("POSITION", 0, F::R32G32B32A32_Float),   // xyz + bitangent sign
			V("NORMAL",   0, F::R16G16B16A16_SNorm),   // octahedral normal + tangent
			V("TEXCOORD", 0, F::R32G32_Float),
			V("TEXCOORD", 1, F::R32G32_Float),
			V("COLOR",    0, F::R8G8B8A8_UNorm),
		}, aVS);
		return true;
	}

	SetInputLayout({
		V("POSITION", 0, F::R32G32B32A32_Float),
		V("COLOR",    0, F::R32G32B32A32_Float),
		V("COLOR",    1, F::R32G32B32A32_Float),
		V("COLOR",    2, F::R32G32B32A32_Float),
		V("COLOR",    3, F::R32G32B32A32_Float),
		V("TEXCOORD", 0, F::R32G32_Float),
		V("TEXCOORD", 1, F::R32G32_Float),
		V("TEXCOORD", 2, F::R32G32_Float),
		V("TEXCOORD", 3, F::R32G32_Float),
		V("NORMAL",   0, F::R32G32B32_Float),
		V("TANGENT",  0, F::R32G32B32_Float),
		V("BINORMAL", 0, F::R32G32B32_Float),
		V("BONES",    0, F::R32G32B32A32_Float),
		V("WEIGHTS",  0, F::R32G32B32A32_Float),
	}, aVS);
	return true;
}