#include "stdafx.h"
#include <age/drawers/LineDrawer.h>
#include <age/graphics/GraphicsEngine.h>
#include <age/graphics/GraphicsStateStack.h>
#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>
#include <age/render/RenderObject.h>
#include <age/shaders/shader.h>
#include <age/application.h>
#include <age/log/Log.h>
#include <age/primitives/LinePrimitive.h>

using namespace Ag;
LineDrawer::LineDrawer()
	: Shader()
{}

LineDrawer::~LineDrawer() {}

bool LineDrawer::Init()
{
	Shader::Init();
	InitShaders();
	CreateBuffer();
	return true;
}

void LineDrawer::CreateBuffer()
{
	rhi::BufferDesc bd = {};
	bd.byteSize = sizeof(SimpleVertex) * kMaxVerts;
	bd.stride = sizeof(SimpleVertex);
	bd.usage = rhi::BufferUsage::Vertex;
	bd.memory = rhi::MemoryType::Upload;
	bd.debugName = "LineDrawerVB";
	myVertexBuffer = DX11::Rhi()->CreateBuffer(bd);
	if (!myVertexBuffer.IsValid())
		ERROR_PRINT("%s", "Buffer error");
}

void Ag::LineDrawer::Draw(const LineMultiPrimitive& aObject)
{
	assert(aObject.count <= 1000 && "A single multi Primivive can only draw 1000 lines"); //1000 is a magic number I dont know why it limits there. Maybe buffer size to graphics card.

	if (!myVertexBuffer.IsValid())
	{
		return;
	}
	PrepareRender();

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	ctx.SetPrimitiveTopology(rhi::Topology::LineList);

	GraphicsStateStack& graphicsStateStack = Ag::GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	Matrix4x4f transform = graphicsStateStack.GetTransform();

	myScratch.clear();
	myScratch.resize(aObject.count * 2);
	for (unsigned int i = 0; i < aObject.count; i++)
	{
		Vector3f fromPos = Vector4f(aObject.fromPositions[i], 1.f) * transform;
		Vector3f toPos = Vector4f(aObject.toPositions[i], 1.f) * transform;

		SimpleVertex& a = myScratch[i * 2];
		SimpleVertex& b = myScratch[i * 2 + 1];
		a.x = fromPos.x; a.y = fromPos.y; a.z = fromPos.z;
		b.x = toPos.x;   b.y = toPos.y;   b.z = toPos.z;

		a.colorA = b.colorA = aObject.colors[i].myA;
		a.colorR = b.colorR = aObject.colors[i].myR;
		a.colorG = b.colorG = aObject.colors[i].myG;
		a.colorB = b.colorB = aObject.colors[i].myB;
	}

	if (!myScratch.empty())
		ctx.UpdateBuffer(myVertexBuffer, myScratch.data(), (uint32_t)(myScratch.size() * sizeof(SimpleVertex)));

	ctx.SetVertexBuffer(0, myVertexBuffer, sizeof(SimpleVertex), 0);
	ctx.Draw(aObject.count * 2, 0);
}

bool Ag::LineDrawer::InitShaders()
{
	CreateShaders("shaders/lineshader_VS", "shaders/lineshader_PS");

	return true;
}

bool LineDrawer::CreateInputLayout(const std::string& aVS)
{
	using F = rhi::Format;
	constexpr uint32_t A = ~0u; // append
	return SetInputLayout({
		{ "POSITION", 0, F::R32G32B32_Float,    0, 0, false, 0 },
		{ "TEXCOORD", 0, F::R32G32B32A32_Float, 0, A, false, 0 },
	}, aVS);
}

void LineDrawer::SetShaderParameters(const LinePrimitive& aObject)
{
	UpdateVertexes(aObject);
	DX11::Rhi()->GetContext().SetVertexBuffer(0, myVertexBuffer, sizeof(SimpleVertex), 0);
}

void LineDrawer::UpdateVertexes(const LinePrimitive& aObject)
{
	GraphicsStateStack& graphicsStateStack = Ag::GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	Matrix4x4f transform = graphicsStateStack.GetTransform();

	Vector3f fromPos = Vector4f(aObject.fromPosition, 1.f) * transform;
	Vector3f toPos = Vector4f(aObject.toPosition, 1.f) * transform;

	SimpleVertex v[2] = {};
	v[0].x = fromPos.x; v[0].y = fromPos.y; v[0].z = fromPos.z;
	v[1].x = toPos.x;   v[1].y = toPos.y;   v[1].z = toPos.z;

	v[0].colorA = v[1].colorA = aObject.color.w;
	v[0].colorR = v[1].colorR = aObject.color.x;
	v[0].colorG = v[1].colorG = aObject.color.y;
	v[0].colorB = v[1].colorB = aObject.color.z;

	DX11::Rhi()->GetContext().UpdateBuffer(myVertexBuffer, v, sizeof(v));
}


void LineDrawer::Draw(const LinePrimitive& aObject)
{
	PrepareRender();

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	ctx.SetPrimitiveTopology(rhi::Topology::LineList);

	SetShaderParameters(aObject);
	ctx.Draw(2, 0);
}