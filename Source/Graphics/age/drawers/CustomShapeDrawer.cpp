#include "stdafx.h"

#include <age/drawers/CustomShapeDrawer.h>
#include <age/render/RenderObject.h>
#include <age/application.h>
#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>
#include <age/graphics/GraphicsEngine.h>
#include <age/graphics/GraphicsStateStack.h>
#include <age/shaders/ShaderCommon.h>
#include <age/primitives/CustomShape.h>
#include <age/EngineDefines.h>
#include <age/math/Matrix2x2.h>
#include <age/log/Log.h>
using namespace Ag;

CustomShapeDrawer::CustomShapeDrawer()
    : Shader()
{}

CustomShapeDrawer::~CustomShapeDrawer() {}

bool CustomShapeDrawer::Init()
{
    myMaxPoints = MAX_POINTS_IN_CUSTOM_SHAPE;
    Shader::Init();

    InitShaders();
    CreateBuffer();
    return true;
}

void CustomShapeDrawer::CreateBuffer()
{
    rhi::BufferDesc bd = {};
    bd.byteSize = sizeof(SimpleVertex) * myMaxPoints;
    bd.stride = sizeof(SimpleVertex);
    bd.usage = rhi::BufferUsage::Vertex;
    bd.memory = rhi::MemoryType::Upload;
    bd.debugName = "CustomShapeVB";
    myVertexBuffer = DX11::Rhi()->CreateBuffer(bd);
    if (!myVertexBuffer.IsValid())
        ERROR_PRINT("%s", "Buffer error");
}

bool CustomShapeDrawer::InitShaders()
{
    CreateShaders("shaders/custom_shape_VS", "shaders/custom_shape_PS" );

    return true;
}

bool CustomShapeDrawer::CreateInputLayout(const std::string& aVS)
{
    using F = rhi::Format;
    constexpr uint32_t A = ~0u; // append
    return SetInputLayout({
        { "POSITION", 0, F::R32G32B32_Float,    0, 0, false, 0 },
        { "TEXCOORD", 0, F::R32G32B32A32_Float, 0, A, false, 0 },
        { "TEXCOORD", 1, F::R32G32_Float,       0, A, false, 0 },
    }, aVS);
}

int CustomShapeDrawer::SetShaderParameters(CustomShape& aObject)
{
    int tris = UpdateVertexes(aObject);

    rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
    ctx.SetVertexBuffer(0, myVertexBuffer, sizeof(SimpleVertex), 0);
    ctx.SetDynamicConstantBuffer(rhi::ShaderStage::Vertex, (uint32_t)ConstantBufferSlot::Object, myObjectCB);
    return tris;
}

int CustomShapeDrawer::SetShaderParameters(CustomShape2D& aObject )
{
    Matrix2x2f scalingMatrix = Matrix2x2f::CreateFromScale(aObject.GetSize());
    Matrix2x2f rotationMatrix = Matrix2x2f::CreateFromRotation(aObject.GetRotation());

    Matrix2x2f m = scalingMatrix * rotationMatrix;
    Vector2f p = aObject.GetPosition();
    GraphicsStateStack& graphicsStateStack = Ag::GraphicsEngine::GetInstance()->GetGraphicsStateStack();

    CustomShapeConstantBufferData data;
    data.modelToWorld = Matrix4x4f
    {
        m(1,1),m(1,2),0,0,
        m(2,1),m(2,2),0,0,
        0     ,0     ,1,0,
        p.x   ,p.y   ,0,1,
    } * graphicsStateStack.GetTransform();
    myObjectCB = DX11::Rhi()->AllocateDynamicConstants(&data, sizeof(data));

    return SetShaderParameters(static_cast<CustomShape&>(aObject));
}

int CustomShapeDrawer::SetShaderParameters(CustomShape3D& aObject)
{
    GraphicsStateStack& graphicsStateStack = Ag::GraphicsEngine::GetInstance()->GetGraphicsStateStack();
    CustomShapeConstantBufferData data;
    data.modelToWorld = aObject.GetTransform() * graphicsStateStack.GetTransform();
    myObjectCB = DX11::Rhi()->AllocateDynamicConstants(&data, sizeof(data));

    return SetShaderParameters(static_cast<CustomShape&>(aObject));
}

int CustomShapeDrawer::UpdateVertexes(CustomShape& aObject )
{
    myScratch.clear();
    myScratch.reserve(aObject.myPoints.size());

    for( SCustomPoint& point : aObject.myPoints )
    {
        if( (int)myScratch.size() >= myMaxPoints )
        {
            INFO_PRINT( "%s%i%s", "Customshape:Render - Too many points rendered at one custom shape! We support: ", myMaxPoints, " skipping the rest, increase this nuber in engine_defines.h" );
            continue;
        }

        SimpleVertex v = {};
        v.x = point.position.x;
        v.y = point.position.y;
        v.z = point.position.z;

        Vector4f linearColor = point.color.AsLinearVec4();
        v.colorR = linearColor.x;
        v.colorG = linearColor.y;
        v.colorB = linearColor.z;
        v.colorA = linearColor.w;

        v.u = point.uv.x;
        v.v = point.uv.y;

        myScratch.push_back(v);
    }

    if (!myScratch.empty())
        DX11::Rhi()->GetContext().UpdateBuffer(myVertexBuffer, myScratch.data(),
                                               (uint32_t)(myScratch.size() * sizeof(SimpleVertex)));
    return (int)myScratch.size();
}

void Ag::CustomShapeDrawer::Draw( CustomShape2D& aObject )
{
	if (!PrepareRender())
	{
		return;
	}

    rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
    ctx.SetPrimitiveTopology(rhi::Topology::TriangleList);

    int tris = SetShaderParameters( aObject );
    if( tris > 0 && tris % 3 == 0 )
    {
        ctx.Draw( tris, 0 );
    }
}

void Ag::CustomShapeDrawer::Draw(CustomShape3D& aObject)
{
    if (!PrepareRender())
    {
        return;
    }

    rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
    ctx.SetPrimitiveTopology(rhi::Topology::TriangleList);

    int tris = SetShaderParameters(aObject);
    if (tris > 0 && tris % 3 == 0)
    {
        ctx.Draw(tris, 0);
    }
}
