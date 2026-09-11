#pragma once
#include <tge/render/RenderCommon.h>
#include <tge/shaders/Shader.h>

namespace Tga
{
    struct LinePrimitive;
    struct LineMultiPrimitive;
    class GraphicsEngine;
    class LineDrawer : public Shader
    {
    public:
        LineDrawer();
        ~LineDrawer();
        bool Init() override;
        void Draw(const LinePrimitive& aObject);
		void Draw(const LineMultiPrimitive& aObject);
    private:
        LineDrawer &operator =( const LineDrawer &anOther ) = delete;
        bool CreateInputLayout(const std::string& aVS) override;
        bool InitShaders();
        void CreateBuffer();

        void SetShaderParameters(const LinePrimitive& aObject);
        void UpdateVertexes(const LinePrimitive& aObject);
        rhi::BufferHandle myVertexBuffer;
        std::vector<SimpleVertex> myScratch;
        static constexpr uint32_t kMaxVerts = 2000;
    };
}