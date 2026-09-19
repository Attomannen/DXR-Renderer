#include "stdafx.h"

#include <age/drawers/SpriteDrawer.h>
#include <age/graphics/GraphicsEngine.h>
#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>
#include <age/sprite/sprite.h>
#include <age/texture/TextureManager.h>
#include <age/shaders/SpriteShader.h>

#include <age/math/Matrix2x2.h>
#include <age/application.h>
#include <age/graphics/GraphicsStateStack.h>
#include <age/log/Log.h>


using namespace Ag;

constexpr size_t BATCH_SIZE = 1024;

SpriteBatchScope::~SpriteBatchScope()
{
	if (mySpriteDrawer != nullptr)
	{
		UnMapAndRender();
		mySpriteDrawer->EndBatch();
		mySpriteDrawer = nullptr;
	}
}

void SpriteBatchScope::Draw(const Sprite2DInstanceData& aInstance)
{
	Draw(&aInstance, 1);
};

void SpriteBatchScope::Draw(const Sprite2DInstanceData* aInstances, size_t aInstanceCount)
{
	aInstances;
	aInstanceCount;

	GraphicsStateStack& graphicsStateStack = Ag::GraphicsEngine::GetInstance()->GetGraphicsStateStack();

	assert(myInstanceCount < BATCH_SIZE);

	Vector2ui resolution = DX11::GetResolution();

	for (int i = 0; i < aInstanceCount; i++)
	{
		const Sprite2DInstanceData& instance = aInstances[i];

		if (instance.isHidden)
			continue;

		SpriteShaderInstanceData& shaderInstance = myInstanceData[myInstanceCount];

		Vector2f pivot = Vector2f(-instance.pivot.x, instance.pivot.y);
		Matrix2x2f scalingMatrix = Matrix2x2f::CreateFromScale(Vector2f((instance.size.x) * instance.sizeMultiplier.x, (instance.size.y) * instance.sizeMultiplier.y));
		Matrix2x2f rotationMatrix = Matrix2x2f::CreateFromRotation(instance.rotation);
		
		Matrix2x2f m = scalingMatrix * rotationMatrix;
		Vector2f p = pivot * m + instance.position;

		shaderInstance.transform = Matrix4x4f
		{
			m(1,1),m(1,2),0,0,
			m(2,1),m(2,2),0,0,
			0     ,0     ,1,0,
			p.x   ,p.y   ,0,1,
		} * graphicsStateStack.GetTransform();

		shaderInstance.uvRect.x = instance.textureRect.startX;
		shaderInstance.uvRect.y = instance.textureRect.endY;
		shaderInstance.uvRect.z = instance.textureRect.endX;
		shaderInstance.uvRect.w = instance.textureRect.startY;

		shaderInstance.uv.x = instance.uv.x;
		shaderInstance.uv.y = instance.uv.y;
		shaderInstance.uv.z = instance.uvScale.x;
		shaderInstance.uv.w = instance.uvScale.y;

		shaderInstance.color = instance.color.AsLinearVec4();

		myInstanceCount++;
		if (myInstanceCount >= BATCH_SIZE)
		{
			UnMapAndRender();
			Map();
		}
	}
};

void SpriteBatchScope::Draw(const Sprite3DInstanceData& aInstance)
{
	Draw(&aInstance, 1);
};

void SpriteBatchScope::Draw(const Sprite3DInstanceData* aInstances, size_t aInstanceCount)
{
	aInstances;
	aInstanceCount;

	assert(myInstanceCount < BATCH_SIZE);

	GraphicsStateStack& graphicsStateStack = Ag::GraphicsEngine::GetInstance()->GetGraphicsStateStack();

	for (int i = 0; i < aInstanceCount; i++)
	{
		const Sprite3DInstanceData& instance = aInstances[i];

		if (instance.isHidden)
			continue;

		SpriteShaderInstanceData& shaderInstance = myInstanceData[myInstanceCount];

		shaderInstance.transform = instance.transform * graphicsStateStack.GetTransform();

		shaderInstance.uvRect.x = instance.textureRect.startX;
		shaderInstance.uvRect.y = instance.textureRect.endY;
		shaderInstance.uvRect.z = instance.textureRect.endX;
		shaderInstance.uvRect.w = instance.textureRect.startY;

		shaderInstance.uv.x = instance.uv.x;
		shaderInstance.uv.y = instance.uv.y;
		shaderInstance.uv.z = instance.uvScale.x;
		shaderInstance.uv.w = instance.uvScale.y;

		shaderInstance.color = instance.color.AsLinearVec4();

		myInstanceCount++;
		if (myInstanceCount >= BATCH_SIZE)
		{
			UnMapAndRender();
			Map();
		}
	}
}

void SpriteBatchScope::UnMapAndRender()
{
	assert(mySpriteDrawer);
	if (myInstanceCount == 0)
		return;

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	ctx.UpdateBuffer(mySpriteDrawer->myInstanceBuffer, myInstanceData.data(),
	                 (uint32_t)(myInstanceCount * sizeof(SpriteShaderInstanceData)));
	ctx.DrawInstanced(6, (uint32_t)myInstanceCount, 0, 0);

	myInstanceCount = 0;
}

void SpriteBatchScope::Map()
{
	assert(mySpriteDrawer);
	assert(myInstanceCount == 0);

	if (myInstanceData.size() != BATCH_SIZE)
		myInstanceData.resize(BATCH_SIZE);
}

SpriteDrawer::SpriteDrawer()
{
}

SpriteDrawer::~SpriteDrawer()
{
}

void SpriteDrawer::Init()
{
	rhi::BufferDesc bd = {};
	bd.byteSize = sizeof(SpriteShaderInstanceData) * BATCH_SIZE;
	bd.stride = sizeof(SpriteShaderInstanceData);
	bd.usage = rhi::BufferUsage::Vertex;
	bd.memory = rhi::MemoryType::Upload;
	bd.debugName = "SpriteInstanceVB";
	myInstanceBuffer = DX11::Rhi()->CreateBuffer(bd);
	if (!myInstanceBuffer.IsValid())
	{
		ERROR_PRINT("%s", "Object Buffer error");
		return;
	}
	myIsLoaded = InitShaders() && CreateBuffer();
}

bool SpriteDrawer::InitShaders()
{
	myDefaultShader = std::make_unique<SpriteShader>();
	if (!myDefaultShader->Init())
	{
		return false;
	}

	return true;
}

bool SpriteDrawer::CreateBuffer()
{
	float startSize = 1.0f;
	float theZ = 0.0f;
	myVertices[0].x = -0;
	myVertices[0].y = -startSize;
	myVertices[0].z = theZ;
	myVertices[0].w = 1.0f;
	myVertices[0].vertexIndex = 0;

	myVertices[1].x = -0;
	myVertices[1].y = -0;
	myVertices[1].z = theZ;
	myVertices[1].w = 1.0f;
	myVertices[1].vertexIndex = 1;

	myVertices[2].x = startSize;
	myVertices[2].y = -startSize;
	myVertices[2].z = theZ;
	myVertices[2].w = 1.0f;
	myVertices[2].vertexIndex = 2;

	myVertices[3].x = startSize;
	myVertices[3].y = -0;
	myVertices[3].z = theZ;
	myVertices[3].w = 1.0f;
	myVertices[3].vertexIndex = 3;

	myVertices[4].x = startSize;
	myVertices[4].y = -startSize;
	myVertices[4].z = theZ;
	myVertices[4].w = 1.0f;
	myVertices[4].vertexIndex = 4;

	myVertices[5].x = -0;
	myVertices[5].y = -0;
	myVertices[5].z = theZ;
	myVertices[5].w = 1.0f;
	myVertices[5].vertexIndex = 5;

	rhi::BufferDesc bd = {};
	bd.byteSize = sizeof(VertexInstanced) * 6;
	bd.stride = sizeof(VertexInstanced);
	bd.usage = rhi::BufferUsage::Vertex;
	bd.memory = rhi::MemoryType::Default;   // the quad never changes
	bd.debugName = "SpriteQuadVB";
	myVertexBuffer = DX11::Rhi()->CreateBuffer(bd, myVertices);
	if (!myVertexBuffer.IsValid())
	{
		ERROR_PRINT("%s", "Buffer error");
		return false;
	}

	return true;
}

SpriteBatchScope SpriteDrawer::BeginBatch(const SpriteSharedData& aSharedData)
{
	assert(myIsLoaded);
	assert(!myIsInBatch);
	myIsInBatch = true;

	if (aSharedData.customShader)
	{
		aSharedData.customShader->PrepareRender(aSharedData);
	}
	else
	{
		myDefaultShader->PrepareRender(aSharedData);
	}

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	ctx.SetVertexBuffer(0, myVertexBuffer, sizeof(VertexInstanced), 0);
	ctx.SetVertexBuffer(1, myInstanceBuffer, sizeof(SpriteShaderInstanceData), 0);

	SpriteBatchScope scope(*this);
	scope.Map();

	return scope;
}

void SpriteDrawer::EndBatch()
{
	assert(myIsInBatch);
	myIsInBatch = false;
}