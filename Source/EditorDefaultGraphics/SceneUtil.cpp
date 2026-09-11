
#include "stdafx.h"

#include "SceneUtil.h"

#include <tge/Application.h>
#include <tge/scene/Scene.h>
#include <tge/scene/ScenePropertyTypes.h>

#include <tge/editor/Editor.h>
#include <tge/editor/Tools/Viewport/Viewport.h>

#include <tge/script/BaseProperties.h>

#include <tge/graphics/DX11.h>

#include <tge/graphics/GraphicsEngine.h>
#include <tge/texture/TextureManager.h>

#include <tge/math/BoxSphereBounds.h>
#include <tge/graphics/Camera.h>
#include <tge/graphics/GraphicsStateStack.h>

#include <tge/model/ModelFactory.h>
#include <tge/model/ModelInstance.h>

#include <tge/drawers/ModelDrawer.h>
#include <tge/drawers/LineDrawer.h>
#include <tge/drawers/SpriteDrawer.h>
#include <tge/primitives/LinePrimitive.h>

#include <tge/editor/p4/p4.h>
#include <tge/log/Log.h>
#include "tge/shaders/SpriteShader.h"
#include "tge/shaders/ModelShader.h"
#include "tge/sprite/sprite.h"

using namespace Tga;


struct RenderData
{
	bool isInitialized;

	ModelShader idAnimatedModelShader;
	ModelShader idModelShader;
	SpriteShader idSpriteShader;

	ComPtr<ID3D11Buffer> idConstantBuffer;
	ComPtr<ID3D11Buffer> selectionOutlineConstantBuffer;
	FullscreenEffect selectionOutlineEffect;
};
static RenderData locRenderdata;

struct IdConstantBuffer
	{
	uint32_t objectId;
	uint32_t selectionId;
	uint32_t p4status;
	uint32_t unused3;
};

struct SelectionOutlineConstantBuffer
{
	uint32_t r, g, b, a;
};


static void EnsureInitialized()
{
	if (!locRenderdata.isInitialized)
	{
		locRenderdata.idAnimatedModelShader.Init("Shaders/animated_model_shader_VS", "Shaders/id_shader_ps");
		locRenderdata.idModelShader.Init("Shaders/id_shader_vs", "Shaders/id_shader_ps");
		locRenderdata.idSpriteShader.Init("Shaders/instanced_sprite_shader_VS", "Shaders/id_shader_ps");

		locRenderdata.selectionOutlineEffect.Init("Shaders/PostProcessSelectionOutline_PS");

		{
			HRESULT result = S_OK;

			D3D11_BUFFER_DESC bufferDesc;
			bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
			bufferDesc.ByteWidth = sizeof(SelectionOutlineConstantBuffer);
			bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			bufferDesc.MiscFlags = 0;
			bufferDesc.StructureByteStride = 0;

			result = DX11::Device->CreateBuffer(&bufferDesc, NULL, locRenderdata.selectionOutlineConstantBuffer.ReleaseAndGetAddressOf());
			if (FAILED(result))
			{
				return;
	}
		}
	{
			HRESULT result = S_OK;

			D3D11_BUFFER_DESC bufferDesc;
			bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
			bufferDesc.ByteWidth = sizeof(IdConstantBuffer);
			bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			bufferDesc.MiscFlags = 0;
			bufferDesc.StructureByteStride = 0;

			result = DX11::Device->CreateBuffer(&bufferDesc, NULL, locRenderdata.idConstantBuffer.ReleaseAndGetAddressOf());
			if (FAILED(result))
			{
				return;
	}
		}

		locRenderdata.isInitialized = true;
	}



}

void Tga::SetupIdPass()
	{
	EnsureInitialized();
	// use last slot to not interfere if slots are added to TGE/in game project
	DX11::Context->VSSetConstantBuffers((int)13, 1, locRenderdata.idConstantBuffer.GetAddressOf());
	DX11::Context->PSSetConstantBuffers((int)13, 1, locRenderdata.idConstantBuffer.GetAddressOf());
	}
void Tga::DrawOutlines(const EditorViewport& viewport)
	{
	EnsureInitialized();


	DX11::Context->VSSetConstantBuffers((int)13, 1, locRenderdata.selectionOutlineConstantBuffer.GetAddressOf());
	DX11::Context->PSSetConstantBuffers((int)13, 1, locRenderdata.selectionOutlineConstantBuffer.GetAddressOf());

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT result = DX11::Context->Map(locRenderdata.selectionOutlineConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (FAILED(result))
	{
		INFO_PRINT("Error in rendering!");
		return;
	}
	SelectionOutlineConstantBuffer* dataPtr = (SelectionOutlineConstantBuffer*)mappedResource.pData;
	// TODO, color outline depending on p4 file status for it
	//P4::FileInfo fileinfo = P4::QueryFileInfo(UUIDManager::GetUUIDStringFromID(p->));

	dataPtr->r = 0;
	dataPtr->g = 0;
	dataPtr->b = 255;
	dataPtr->a = 1;
	DX11::Context->Unmap(locRenderdata.selectionOutlineConstantBuffer.Get(), 0);

	viewport.GetIdRenderTarget().SetAsResourceOnSlot(1);
	locRenderdata.selectionOutlineEffect.Render();
}
void Tga::SetObjectAndSelectionId(uint32_t anObjectId, uint32_t aSelectionId, const P4::FileInfo& someInfo)
	{
	EnsureInitialized();

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT result = DX11::Context->Map(locRenderdata.idConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (FAILED(result))
	{
		INFO_PRINT("Error in rendering!");
		return;
	}

	IdConstantBuffer* dataPtr = (IdConstantBuffer*)mappedResource.pData;
	dataPtr->objectId = anObjectId;
	dataPtr->selectionId = aSelectionId;
	dataPtr->p4status = 0;

	if (someInfo.action != P4::FileAction::None)
	{
		if (strcmp(someInfo.user, P4::MyUser()) == 0 && strcmp(someInfo.client, P4::MyClient()) == 0)
		{
			// my user and workspace
			dataPtr->p4status = 1;
		}
		else if (strcmp(someInfo.user, P4::MyUser()) == 0)
		{
			// my user, but different workspace
			dataPtr->p4status = 2;
	}
		else
		{
			// checked out by someone else
			dataPtr->p4status = 3;
		}
	}

	DX11::Context->Unmap(locRenderdata.idConstantBuffer.Get(), 0);
}

void Tga::SceneCache::ClearCache()
{
	myTextureCache.clear();
	myModelCache.clear();
}

std::shared_ptr<Model> Tga::SceneCache::GetModelUsingCache(StringId path)
{
	if (path.IsEmpty())
		return nullptr;

	std::shared_ptr<Model> model;

	auto cacheIt = myModelCache.find(path);
	if (cacheIt != myModelCache.end())
	{
		model = cacheIt->second;
	}
	else
	{
		model = ModelFactory::GetInstance().GetModel(path.GetString());
		myModelCache[path] = model;
	}

	return model;
}

Texture* Tga::SceneCache::GetTextureUsingCache(StringId path, TextureSrgbMode srgbMode)
{
	if (path.IsEmpty())
		return nullptr;

	Texture* texture = nullptr;

	auto cacheIt = myTextureCache.find(path);
	if (cacheIt != myTextureCache.end())
	{
		texture = cacheIt->second;
	}
	else
	{
		auto& engine = *Tga::GraphicsEngine::GetInstance();
		auto& textureManager = engine.GetTextureManager();

		texture = textureManager.GetTexture(path.GetString(), srgbMode);
		myTextureCache[path] = texture;
	}

	return texture;
}

Scene* Tga::SceneCache::GetSceneUsingCache(StringId path)
{
	if (path.IsEmpty())
		return nullptr;

	Scene* scene = nullptr;

	auto cacheIt = mySceneCache.find(path);
	if (cacheIt != mySceneCache.end())
	{
		scene = cacheIt->second;
	}
	else
	{
		auto& editor = *Tga::Editor::GetEditor();
		auto& sceneManager = editor.GetEditorSceneManager();

		scene = sceneManager.Get(path.GetString());
		mySceneCache[path] = scene;
	}

	return scene;
}


bool Tga::CheckBounds(const Frustum& frustum, Tga::Matrix4x4f matrix, float maxScale, Model& model)
{
	int meshCount = (int)model.GetMeshCount();
	for (int i = 0; i < meshCount; i++)
	{
		const Tga::BoxSphereBounds& bounds = model.GetMeshData(i).bounds;
		Vector3f transformedCenter = bounds.center * matrix;

		if (CheckFrustum(frustum, transformedCenter, maxScale * bounds.radius))
			return true;
	}

	return false;
}

// ugly workaround to avoid allocations per object when drawing
static int locScenePropertyDepth = -1;
static std::vector<std::vector<ScenePropertyDefinition>> locSceneObjectProperties;

void DrawBounds(const Tga::BoxSphereBounds& bounds, Tga::Vector4f color)
{
	bounds;
	color;

	auto& debug = Tga::GraphicsEngine::GetInstance()->GetLineDrawer();
	Tga::LinePrimitive primitive{};
	primitive.color = color;
	auto min = bounds.center - bounds.boxExtents;
	auto max = bounds.center + bounds.boxExtents;
	primitive.fromPosition = min;
	primitive.toPosition = { min.x, max.y, min.z };
	debug.Draw(primitive);
	primitive.fromPosition = primitive.toPosition;
	primitive.toPosition = { max.x, max.y, min.z };
	debug.Draw(primitive);
	primitive.fromPosition = primitive.toPosition;
	primitive.toPosition = { max.x, min.y, min.z };
	debug.Draw(primitive);
	primitive.fromPosition = primitive.toPosition;
	primitive.toPosition = { min.x, min.y, min.z };
	debug.Draw(primitive);

	primitive.fromPosition = { min.x, min.y, max.z };
	primitive.toPosition = { min.x, max.y, max.z };
	debug.Draw(primitive);
	primitive.fromPosition = primitive.toPosition;
	primitive.toPosition = { max.x, max.y, max.z };
	debug.Draw(primitive);
	primitive.fromPosition = primitive.toPosition;
	primitive.toPosition = { max.x, min.y, max.z };
	debug.Draw(primitive);
	primitive.fromPosition = primitive.toPosition;
	primitive.toPosition = { min.x, min.y, max.z };
	debug.Draw(primitive);

	primitive.fromPosition = { min.x, min.y, min.z };
	primitive.toPosition = { min.x, min.y, max.z };
	debug.Draw(primitive);
	primitive.fromPosition = { min.x, max.y, min.z };
	primitive.toPosition = { min.x, max.y, max.z };
	debug.Draw(primitive);
	primitive.fromPosition = { max.x, max.y, min.z };
	primitive.toPosition = { max.x, max.y, max.z };
	debug.Draw(primitive);
	primitive.fromPosition = { max.x, min.y, min.z };
	primitive.toPosition = { max.x, min.y, max.z };
	debug.Draw(primitive);
};



bool Tga::DrawSceneProperty(const ScenePropertyDefinition& property, float maxScale, DrawParameters& drawParameters)
	{
	EnsureInitialized();
	
	auto& graphicsStateStack = GraphicsEngine::GetInstance()->GetGraphicsStateStack();

	bool hasBeenRendered = false;
	if (property.type == GetPropertyType<CopyOnWriteWrapper<SceneReference>>())
	{
		const SceneReference& value = property.value.Get<CopyOnWriteWrapper<SceneReference>>()->Get();

		Scene* scene = drawParameters.cache.GetSceneUsingCache(value.path);
		if (scene)
		{
			DrawScene(*scene, drawParameters);
		}

	}
	else if (drawParameters.useIdShader)
	{
		if (property.type == GetPropertyType<CopyOnWriteWrapper<SceneModel>>())
		{
			StringId path = property.value.Get<CopyOnWriteWrapper<SceneModel>>()->Get().path;
			std::shared_ptr<Model> model = drawParameters.cache.GetModelUsingCache(path);

			if (model && CheckBounds(drawParameters.frustum, graphicsStateStack.GetTransform(), maxScale, *model))
			{
				ModelSpacePose* pose = nullptr;
				if (drawParameters.previewPoses)
				{
					auto it = drawParameters.previewPoses->find(property.name);

					if (it != drawParameters.previewPoses->end())
					{
						pose = &it->second;
					}
				}

				if (pose)
				{
					AnimatedModelInstance instance;
					instance.Init(model);
					instance.SetPose(*pose);
					Tga::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance, locRenderdata.idAnimatedModelShader);
				}
				else
				{
					ModelInstance instance;
					instance.Init(model);
					Tga::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance, locRenderdata.idModelShader);
				}
				hasBeenRendered = true;
			}
		}
		else if (property.type == GetPropertyType<CopyOnWriteWrapper<SceneSprite>>())
		{
			const SceneSprite& value = property.value.Get<CopyOnWriteWrapper<SceneSprite>>()->Get();

			SpriteSharedData sharedData = {};
			sharedData.texture = drawParameters.cache.GetTextureUsingCache(value.textures[0], TextureSrgbMode::ForceSrgbFormat);
			sharedData.customShader = &locRenderdata.idSpriteShader;

			Sprite2DInstanceData instance = {};
			instance.pivot = value.pivot;
			instance.size = value.size;

			Tga::GraphicsEngine::GetInstance()->GetSpriteDrawer().Draw(sharedData, instance);

			hasBeenRendered = true;

		}
	}
	else
	{
	
		if (property.type == GetPropertyType<CopyOnWriteWrapper<SceneModel>>())
		{
			const SceneModel& value = property.value.Get<CopyOnWriteWrapper<SceneModel>>()->Get();

			StringId path = value.path;
			std::shared_ptr<Model> model = drawParameters.cache.GetModelUsingCache(path);

			if (model && CheckBounds(drawParameters.frustum, graphicsStateStack.GetTransform(), maxScale, *model))
			{
				ModelSpacePose* pose = nullptr;
				if (drawParameters.previewPoses)
				{
					auto it = drawParameters.previewPoses->find(property.name);

					if (it != drawParameters.previewPoses->end())
					{
						pose = &it->second;
					}
				}

				if (pose)
				{
					AnimatedModelInstance instance;
					instance.Init(model);
					instance.SetPose(*pose);

					int meshCount = (int)instance.GetModel()->GetMeshCount();
					if (meshCount > MAX_MESHES_PER_MODEL)
						meshCount = MAX_MESHES_PER_MODEL;

					for (int i = 0; i < meshCount; i++)
					{
						for (int j = 0; j < 4; j++)
						{
							if (!value.textures[i][j].IsEmpty())
							{
								// diffuse texture should be srgb, the rest not
								TextureSrgbMode srgbMode = (j == 0) ? TextureSrgbMode::ForceSrgbFormat : TextureSrgbMode::ForceNoSrgbFormat;
								Texture* texture = drawParameters.cache.GetTextureUsingCache(value.textures[i][j], srgbMode);

								if (texture != nullptr)
									instance.SetTexture(i, j, texture);
							}
						}
					}

					// todo override shader
					Tga::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance);
				}
				else
				{
					ModelInstance instance;
					instance.Init(model);

					int meshCount = (int)instance.GetModel()->GetMeshCount();
					if (meshCount > MAX_MESHES_PER_MODEL)
						meshCount = MAX_MESHES_PER_MODEL;

					for (int i = 0; i < meshCount; i++)
					{
						for (int j = 0; j < 4; j++)
						{
							if (!value.textures[i][j].IsEmpty())
							{
								// diffuse texture should be srgb, the rest not
								TextureSrgbMode srgbMode = (j == 0) ? TextureSrgbMode::ForceSrgbFormat : TextureSrgbMode::ForceNoSrgbFormat;
								Texture* texture = drawParameters.cache.GetTextureUsingCache(value.textures[i][j], srgbMode);

								if (texture != nullptr)
									instance.SetTexture(i, j, texture);
							}
						}
					}

					if (drawParameters.overrideModelShader)
					{
						Tga::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance, *drawParameters.overrideModelShader);

					}
					else
					{
						Tga::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance);
					}
				}

				if (drawParameters.drawBounds)
				{
					const BoxSphereBounds& bounds = model->GetMeshData(0).bounds;
					DrawBounds(bounds, drawParameters.boundsColor);
				}

				hasBeenRendered = true;
			}
		}
		else if (property.type == GetPropertyType<CopyOnWriteWrapper<SceneSprite>>())
		{
			const SceneSprite& value = property.value.Get<CopyOnWriteWrapper<SceneSprite>>()->Get();

			SpriteSharedData sharedData = {};
			sharedData.texture = drawParameters.cache.GetTextureUsingCache(value.textures[0], TextureSrgbMode::ForceSrgbFormat);
			sharedData.maps[0] = drawParameters.cache.GetTextureUsingCache(value.textures[1], TextureSrgbMode::ForceNoSrgbFormat);
			sharedData.maps[1] = drawParameters.cache.GetTextureUsingCache(value.textures[2], TextureSrgbMode::ForceNoSrgbFormat);
			sharedData.maps[2] = drawParameters.cache.GetTextureUsingCache(value.textures[3], TextureSrgbMode::ForceNoSrgbFormat);

			Sprite2DInstanceData instance = {};
			instance.pivot = value.pivot;
			instance.size = value.size;
			Tga::GraphicsEngine::GetInstance()->GetSpriteDrawer().Draw(sharedData, instance);

			hasBeenRendered = true;
		}
	}

	return hasBeenRendered;
}

void Tga::DrawSceneObject(const SceneObject& sceneObject, DrawParameters& drawParameters)
{
	EnsureInitialized();

	auto& graphicsStateStack = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	SceneObjectDefinitionManager& sceneObjectDefinitionManager = Editor::GetEditor()->GetSceneObjectDefinitionManager();

	graphicsStateStack.Push();

	Matrix4x4f transform = sceneObject.GetTransform();
	graphicsStateStack.ApplyTransform(transform);

	Vector3f scale = sceneObject.GetScale();
	float maxScale = std::max(scale.x, std::max(scale.y, scale.z));

	locScenePropertyDepth++;

	if (locSceneObjectProperties.size() <= locScenePropertyDepth)
		locSceneObjectProperties.resize(locScenePropertyDepth + 1);

	sceneObject.CalculateCombinedPropertySet(sceneObjectDefinitionManager, locSceneObjectProperties[locScenePropertyDepth]);

	bool hasBeenRendered = false;
	for (ScenePropertyDefinition& property : locSceneObjectProperties[locScenePropertyDepth])
	{
		if (DrawSceneProperty(property, maxScale, drawParameters))
			hasBeenRendered = true;
	}

	if (!hasBeenRendered)
	{
		std::shared_ptr<Model> model = drawParameters.cache.GetModelUsingCache("models/locator.fbx"_tgaid);

		if (model && CheckBounds(drawParameters.frustum, graphicsStateStack.GetTransform(), maxScale, *model))
		{
			ModelInstance instance;
			instance.Init(model);

			if (drawParameters.useIdShader)
			{
				Tga::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance, locRenderdata.idModelShader);
			}
			else
			{
				Tga::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance);

				if (drawParameters.drawBounds)
				{
					const BoxSphereBounds& bounds = instance.GetModel()->GetMeshData(0).bounds;
					DrawBounds(bounds, drawParameters.boundsColor);
				}
			}

		}
	}

	graphicsStateStack.Pop();

	locSceneObjectProperties[locScenePropertyDepth].clear();
	locScenePropertyDepth--;

}

void Tga::DrawScene(const Scene& scene, DrawParameters& drawParameters)
{
	EnsureInitialized();

	for (auto& p : scene.GetSceneObjects())
	{
		DrawSceneObject(*p.second, drawParameters);
	}
}
