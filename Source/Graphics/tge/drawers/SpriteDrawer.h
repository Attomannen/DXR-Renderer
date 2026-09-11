#pragma once

#include <memory>
#include <vector>
#include <tge/render/RenderCommon.h>
#include <tge/render/RenderObject.h>
#include <tge/shaders/ShaderCommon.h>
#include <tge/rhi/Handles.h>

namespace Tga
{
	class Texture;
	class SpriteDrawer;
	class SpriteShader;
	struct Sprite2DInstanceData;
	struct Sprite3DInstanceData;
	struct SpriteSharedData;

	class SpriteBatchScope
	{
		friend class SpriteDrawer;
	public:
		~SpriteBatchScope();
		void Draw(const Sprite2DInstanceData& aInstance);
		void Draw(const Sprite2DInstanceData* aInstances, size_t aInstanceCount);
		void Draw(const Sprite3DInstanceData& aInstance);
		void Draw(const Sprite3DInstanceData* aInstances, size_t aInstanceCount);
	private:
		SpriteBatchScope(SpriteDrawer& aSpriteDrawer)
			: mySpriteDrawer(&aSpriteDrawer) {}
		SpriteBatchScope(SpriteBatchScope&& scope) noexcept
			: mySpriteDrawer(scope.mySpriteDrawer)
			, myInstanceData(std::move(scope.myInstanceData))
			, myInstanceCount(scope.myInstanceCount)
		{
			scope.mySpriteDrawer = nullptr;
			scope.myInstanceCount = 0;
		}

		SpriteBatchScope(const SpriteBatchScope&) = delete;
		void operator=(const SpriteBatchScope&) = delete;

		void UnMapAndRender();
		void Map();

		SpriteDrawer* mySpriteDrawer;
		std::vector<SpriteShaderInstanceData> myInstanceData;   // CPU staging for the instance buffer
		size_t myInstanceCount = 0;
	};

	class SpriteDrawer
	{
		friend class SpriteBatchScope;
	public:
		SpriteDrawer();
		~SpriteDrawer();
		void Init();

		inline void Draw(const SpriteSharedData& aSharedData, const Sprite2DInstanceData& aInstance);
		inline void Draw(const SpriteSharedData& aSharedData, const Sprite2DInstanceData* aInstances, size_t aInstanceCount);
		inline void Draw(const SpriteSharedData& aSharedData, const Sprite3DInstanceData& aInstance);
		inline void Draw(const SpriteSharedData& aSharedData, const Sprite3DInstanceData* aInstances, size_t aInstanceCount);

		SpriteBatchScope BeginBatch(const SpriteSharedData& aSharedData);

	private:
		void EndBatch();

		bool InitShaders();
		bool CreateBuffer();

		rhi::BufferHandle myVertexBuffer;
		rhi::BufferHandle myInstanceBuffer;
		VertexInstanced myVertices[6] = {};

		std::unique_ptr<SpriteShader> myDefaultShader;
		bool myIsLoaded = false;
		bool myIsInBatch = false;
	};

	void SpriteDrawer::Draw(const SpriteSharedData& aSharedData, const Sprite2DInstanceData& aInstance)
	{
		SpriteBatchScope scope = BeginBatch(aSharedData);
		scope.Draw(aInstance);
	}

	void SpriteDrawer::Draw(const SpriteSharedData& aSharedData, const Sprite2DInstanceData* aInstances, size_t aInstanceCount)
	{
		SpriteBatchScope scope = BeginBatch(aSharedData);
		scope.Draw(aInstances, aInstanceCount);
	}

	void SpriteDrawer::Draw(const SpriteSharedData& aSharedData, const Sprite3DInstanceData& aInstance)
	{
		SpriteBatchScope scope = BeginBatch(aSharedData);
		scope.Draw(aInstance);
	}

	void SpriteDrawer::Draw(const SpriteSharedData& aSharedData, const Sprite3DInstanceData* aInstances, size_t aInstanceCount)
	{
		SpriteBatchScope scope = BeginBatch(aSharedData);
		scope.Draw(aInstances, aInstanceCount);
	}
}
