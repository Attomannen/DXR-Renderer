#pragma once

#include "shader.h"
#include "ShaderCommon.h"
#include <age/animation/Animation.h>
#include <age/math/CommonMath.h>
#include <age/math/matrix4x4.h>
#include <age/model/model.h>

namespace Ag
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
		// aMaterialIndex: RayTracingMaterialTable index; 0 uses the mesh's own.
		void RenderMesh(const TextureResource* const* someTextures, const Model::MeshData& aModelData, uint32_t aMaterialIndex = 0) const;

		// Forces every following draw onto one material (debug views). 0 clears.
		static void SetMaterialOverride(uint32_t aMaterialIndex) { ourMaterialOverride = aMaterialIndex; }

		bool CreateInputLayout(const std::string& aVS) override;

		// Skinned vertex shaders read the full Vertex; everything else reads the
		// compact MeshVertex (see Model::VertexFormat).
		Model::VertexFormat GetVertexFormat() const { return myVertexFormat; }

	private:
		static inline uint32_t ourMaterialOverride = 0;
		Model::VertexFormat myVertexFormat = Model::VertexFormat::Compact;
	};
} // namespace Ag