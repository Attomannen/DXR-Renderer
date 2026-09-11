#pragma once

#include "shader.h"
#include "ShaderCommon.h"
#include <tge/animation/Animation.h>
#include <tge/math/CommonMath.h>
#include <tge/math/matrix4x4.h>
#include <tge/model/model.h>

struct ID3D11Buffer;
namespace Tga
{
	class RenderObjectSprite;
	class ModelShader : public Shader
	{
	public:
		ModelShader();
		~ModelShader();

		bool Init() override;
		bool Init(const char* aVertexShaderFile, const char* aPixelShaderFile);

		// Convenience: full per-mesh draw (setup + one mesh). Kept for callers that
		// don't batch.
		void Render(const TextureResource* const* someTextures, const Model::MeshData& aModelData, const Matrix4x4f& aObToWorld, const Matrix4x4f* someBones = nullptr) const;

		// Split path: bind pipeline + per-object constants once, then issue one
		// RenderMesh() per sub-mesh. Hoists shader / input-layout / constant-buffer
		// binds out of the per-sub-mesh loop.
		void RenderSetup(const Matrix4x4f& aObToWorld, const Matrix4x4f* someBones = nullptr) const;
		void RenderMesh(const TextureResource* const* someTextures, const Model::MeshData& aModelData) const;

		bool CreateInputLayout(const std::string& aVS) override;
	};
} // namespace Tga