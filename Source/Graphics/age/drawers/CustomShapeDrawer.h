#pragma once
#include <age/render/RenderCommon.h>
#include <age/shaders/shader.h>

namespace Ag
{
    struct CustomShapeConstantBufferData
    {
		Matrix4x4f modelToWorld;
    };

    class GraphicsEngine;
    class CustomShape;
    class CustomShape2D;
    class CustomShape3D;
    class RenderObjectCustom;
    class CustomShapeDrawer : public Shader
    {
    public:
        CustomShapeDrawer();
        ~CustomShapeDrawer();
        bool Init();
        void Draw(CustomShape2D& aObject);
        void Draw(CustomShape3D& aObject);

    private:
        CustomShapeDrawer &operator =( const CustomShapeDrawer &anOther ) = delete;

        bool CreateInputLayout(const std::string& aVS) override;
        bool InitShaders();
        void CreateBuffer();

        int SetShaderParameters(CustomShape& aObject);
        int SetShaderParameters(CustomShape2D& aObject);
        int SetShaderParameters(CustomShape3D& aObject);

        int UpdateVertexes(CustomShape& aObject);
        rhi::BufferHandle  myVertexBuffer;   // dynamic (Upload) vertex buffer
        rhi::DynamicAlloc  myObjectCB;       // per-shape b4 constants, filled per Draw
        std::vector<SimpleVertex> myScratch; // CPU staging for UpdateVertexes
        int myMaxPoints = 0;
    };
}