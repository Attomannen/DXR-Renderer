
#include "stdafx.h"
#include <cctype>
#include <algorithm>
#include <unordered_map>

#include "SceneUtil.h"

#include <filesystem>

#include <age/Application.h>
#include <age/scene/Scene.h>
#include <age/scene/ScenePropertyTypes.h>

#include <age/editor/Editor.h>
#include <age/editor/Material/MaterialAsset.h>
#include <age/render/RayTracingMaterialTable.h>
#include <age/editor/Tools/Viewport/Viewport.h>

#include <age/script/BaseProperties.h>

#include <age/graphics/DX11.h>
#include <age/rhi/ConstantBuffer.h>

#include <age/graphics/GraphicsEngine.h>
#include <age/texture/TextureManager.h>

#include <age/math/BoxSphereBounds.h>
#include <age/graphics/Camera.h>
#include <age/graphics/GraphicsStateStack.h>

#include <age/model/ModelFactory.h>
#include <age/model/ModelInstance.h>
#include <age/model/Model.h>
#include <age/rhi/Device.h>

#include <age/drawers/ModelDrawer.h>
#include <age/drawers/LineDrawer.h>
#include <age/drawers/SpriteDrawer.h>
#include <age/primitives/LinePrimitive.h>

#include <age/log/Log.h>
#include <age/settings/settings.h>
#include "age/shaders/SpriteShader.h"
#include "age/shaders/ModelShader.h"
#include "age/sprite/sprite.h"

using namespace Ag;

namespace
{
	// Model assets reference .tgmat files; DDS paths are intentionally private to
	// MaterialAsset. Keep the editor preview on the same authored-data path as
	// the game runtime.
	template <typename Instance>
	void ApplyModelMaterials(const SceneModel& modelValue, Instance& instance, SceneCache& cache)
	{
		const int meshCount = std::min((int)instance.GetModel()->GetMeshCount(), MAX_MESHES_PER_MODEL);

		// Identify this (model, material assignment) pair. StringId is an
		// interned pointer, so the contents hash is just the pointers.
		uint64_t key = 1469598103934665603ull;
		const auto mix = [&key](const void* aPointer)
		{
			key = (key ^ reinterpret_cast<uint64_t>(aPointer)) * 1099511628211ull;
		};
		mix(instance.GetModel().get());
		for (const StringId candidate : modelValue.materials) mix(candidate.GetString());

		SceneCache::ResolvedMaterials& resolved = cache.GetResolvedMaterials(key);
		if (!resolved.built)
		{
			resolved.built = true;
			resolved.meshes.assign((size_t)meshCount, {});

			// Index the assigned material assets by lowercased filename stem.
			//
			// The obvious spelling -- scanning modelValue.materials per mesh and
			// taking each candidate's stem inline -- is quadratic in a way that
			// does not show until a real model arrives: materials is a fixed
			// MAX_MESHES_PER_MODEL (2048) array, so a 132-mesh model built
			// roughly 270k std::filesystem::path objects, plus a std::string per
			// comparison, every frame.
			static thread_local std::unordered_map<std::string, StringId> locStemToMaterial;
			static thread_local std::string locKey;
			locStemToMaterial.clear();
			for (const StringId candidate : modelValue.materials)
			{
				if (candidate.IsEmpty()) continue;
				locKey = std::filesystem::path(candidate.GetString()).stem().string();
				std::transform(locKey.begin(), locKey.end(), locKey.begin(),
					[](unsigned char c) { return (char)std::tolower(c); });
				// First assignment wins, matching the original loop's early break.
				locStemToMaterial.try_emplace(locKey, candidate);
			}

			for (int mesh = 0; mesh < meshCount; ++mesh)
			{
				// FBX traversal order is exporter-dependent. Prefer the material
				// asset whose filename matches the imported mesh material name,
				// falling back to the legacy row index for assets without names.
				StringId materialPath = modelValue.materials[mesh];
				const std::string_view meshMaterial = instance.GetModel()->GetMaterialName(mesh).GetStringView();
				if (!meshMaterial.empty())
				{
					locKey.assign(meshMaterial);
					std::transform(locKey.begin(), locKey.end(), locKey.begin(),
						[](unsigned char c) { return (char)std::tolower(c); });
					if (auto it = locStemToMaterial.find(locKey); it != locStemToMaterial.end())
						materialPath = it->second;
				}
				if (materialPath.IsEmpty()) continue;

				SceneCache::ResolvedMaterials::Mesh& out = resolved.meshes[(size_t)mesh];
				out.materialPath = materialPath;
				// Same material record the game builds (GameWorld::ApplySceneMaterial).
				const std::string recordName = std::string("tgmat/") + materialPath.GetString() + "@"
					+ instance.GetModel()->GetPath() + "#" + std::to_string(mesh);
				out.materialIndex = RayTracingMaterialTable::GetOrAssignMaterialIndex(StringRegistry::RegisterOrGetString(recordName));
				out.hasMaterial = true;
			}
		}

		// Per frame: hash lookups and pointer writes, no string building. The
		// instance is rebuilt on the stack each frame, so it still has to be
		// stamped even when nothing about the materials changed.
		for (int mesh = 0; mesh < (int)resolved.meshes.size(); ++mesh)
		{
			SceneCache::ResolvedMaterials::Mesh& out = resolved.meshes[(size_t)mesh];
			if (!out.hasMaterial) continue;
			const MaterialAsset* cached = cache.GetMaterialUsingCache(out.materialPath);
			if (!cached) continue;
			const MaterialAsset& material = *cached;

			// A reloaded asset is a new allocation, which is also the signal
			// that its contents may have changed on disk.
			const bool materialChanged = cached != out.lastMaterial;
			if (materialChanged)
			{
				out.lastMaterial = cached;
				for (int slot = 0; slot < 4; ++slot)
				{
					out.texturePaths[slot] = material.maps[slot].empty() ? StringId{}
						: StringRegistry::RegisterOrGetString(material.maps[slot]);
					out.textureModes[slot] = material.MapIsSrgb(slot) ? TextureSrgbMode::ForceSrgbFormat : TextureSrgbMode::ForceNoSrgbFormat;
				}
			}

			Texture* textures[4] = {};
			for (int slot = 0; slot < 4; ++slot)
			{
				if (out.texturePaths[slot].IsEmpty()) continue;
				textures[slot] = cache.GetTextureUsingCache(out.texturePaths[slot], out.textureModes[slot]);
				if (textures[slot]) instance.SetTexture(mesh, slot, textures[slot]);
			}

			if (materialChanged)
			{
				auto srv = [&](int slot) { return textures[slot] ? textures[slot]->GetSrv() : rhi::SrvHandle{}; };
				RayTracingMaterialTable::SetMaterialTextures(out.materialIndex, { srv(0), srv(1), srv(2), srv(3) });
				RayTracingMaterialTable::SetMaterialParams(out.materialIndex, material.ToParams());
				// Same classification as GameWorld::RegisterMaterial.
				const bool provablyOpaque = !material.IsMasked()
					&& (material.maps[MaterialAsset::BaseColor].empty() || !material.baseColorHasAlpha);
				RayTracingMaterialTable::SetRayVisibility(out.materialIndex, material.IsTransparent() ? RayTracingMaterialTable::kRayTransparent
					: provablyOpaque ? RayTracingMaterialTable::kRayOpaque : RayTracingMaterialTable::kRayMasked);
			}

			instance.SetMaterial(mesh, out.materialIndex);
		}
	}
}


struct RenderData
{
	bool isInitialized;

	ModelShader idAnimatedModelShader;
	ModelShader idModelShader;
	SpriteShader idSpriteShader;

	rhi::ConstantBuffer idConstantBuffer;
	rhi::ConstantBuffer selectionOutlineConstantBuffer;
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

		// use last slot to not interfere if slots are added to AttoEngine/in game project;
		// bound to both VS and PS (SetupIdPass/DrawOutlines bind it at both stages).
		locRenderdata.selectionOutlineConstantBuffer.Create(*DX11::Rhi(), sizeof(SelectionOutlineConstantBuffer),
			rhi::ShaderStage::AllGraphics, 13, "SelectionOutlineCb");
		locRenderdata.idConstantBuffer.Create(*DX11::Rhi(), sizeof(IdConstantBuffer),
			rhi::ShaderStage::AllGraphics, 13, "IdCb");

		locRenderdata.isInitialized = true;
	}



}

void Ag::SetupIdPass()
	{
	EnsureInitialized();
	// use last slot to not interfere if slots are added to AttoEngine/in game project
	locRenderdata.idConstantBuffer.Bind(DX11::Rhi()->GetContext());
	}
void Ag::DrawOutlines(const EditorViewport& viewport)
	{
	EnsureInitialized();

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();

	SelectionOutlineConstantBuffer data{};
	data.r = 0;
	data.g = 0;
	data.b = 255;
	data.a = 1;
	locRenderdata.selectionOutlineConstantBuffer.Update(ctx, data);
	locRenderdata.selectionOutlineConstantBuffer.Bind(ctx);

	// NOT SetAsResourceOnSlot(): that's a DX11-only legacy path (raw
	// PSSetShaderResources off TextureResource's raw ComPtr, which is never
	// populated on DX12 -- only myRhiSrv is), missed by this port's earlier
	// migration passes since it's editor-only code, not exercised by
	// GameMain's bench flow (found 2026-09-12: DX12 GameEditor asserted on
	// this the moment a scene's viewport actually needed its ID/selection-
	// outline pass -- i.e. as soon as a scene was open). Goes through the
	// RHI like every other texture bind in this file.
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 1, viewport.GetIdRenderTarget().GetSrv());
	locRenderdata.selectionOutlineEffect.Render();
}
void Ag::SetObjectAndSelectionId(uint32_t anObjectId, uint32_t aSelectionId)
	{
	EnsureInitialized();

	IdConstantBuffer data{};
	data.objectId = anObjectId;
	data.selectionId = aSelectionId;
	// p4status used to distinguish "checked out by you"/"by someone else" for
	// the selection outline shader; source-control integration was removed.
	// Left at 0 (its always-safe/no-highlight value) rather than reworking
	// the cbuffer layout and the shader that reads it.
	data.p4status = 0;

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	locRenderdata.idConstantBuffer.Update(ctx, data);
	locRenderdata.idConstantBuffer.Bind(ctx);
}

void Ag::SceneCache::ClearCache()
{
	myTextureCache.clear();
	myModelCache.clear();
	myMaterialCache.clear();
	// myResolvedMaterials is intentionally kept: see its declaration.
}

void Ag::SceneCache::ClearCacheThrottled(float aMinIntervalSeconds)
{
	const auto now = std::chrono::steady_clock::now();
	if (myLastClear.time_since_epoch().count() != 0 &&
		std::chrono::duration<float>(now - myLastClear).count() < aMinIntervalSeconds)
		return;
	myLastClear = now;
	// Not ClearCache(): these two are cheap to refill (their misses hit the
	// engine's own caches), materials are not. See myMaterialCache.
	myTextureCache.clear();
	myModelCache.clear();
	DropChangedMaterials();
}

void Ag::SceneCache::DropChangedMaterials()
{
	for (auto it = myMaterialCache.begin(); it != myMaterialCache.end(); )
	{
		std::error_code ec;
		const auto written = std::filesystem::last_write_time(it->second.file, ec);
		const long long stamp = ec ? 0 : written.time_since_epoch().count();
		if (stamp != it->second.stamp)
			it = myMaterialCache.erase(it);
		else
			++it;
	}
}

const Ag::MaterialAsset* Ag::SceneCache::GetMaterialUsingCache(StringId path)
{
	if (path.IsEmpty()) return nullptr;
	if (auto it = myMaterialCache.find(path); it != myMaterialCache.end()) return it->second.asset.get();

	auto material = std::make_shared<MaterialAsset>();
	const std::filesystem::path absolutePath = std::filesystem::path(Settings::GameAssetRoot()) / path.GetString();
	if (!material->Load(absolutePath.string()))
	{
		ERROR_PRINT("Model material could not be loaded: %s", path.GetString());
		material.reset();
	}

	CachedMaterial entry;
	entry.asset = material;
	entry.file = absolutePath.string();
	std::error_code ec;
	const auto written = std::filesystem::last_write_time(absolutePath, ec);
	entry.stamp = ec ? 0 : written.time_since_epoch().count();

	auto [it, inserted] = myMaterialCache.emplace(path, std::move(entry));
	return it->second.asset.get();
}

std::shared_ptr<Model> Ag::SceneCache::GetModelUsingCache(StringId path)
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
		// Do not let a first-time FBX parse freeze the editor's render/UI frame.
		// The factory adopts completed CPU work on this render thread, then this
		// cache sees the model on a subsequent frame.
		ModelFactory& factory = ModelFactory::GetInstance();
		factory.PumpAsyncImports();
		model = factory.GetLoadedModel(path);
		if (!model)
		{
			factory.RequestAsyncImport(path);
			return nullptr;
		}
		myModelCache[path] = model;
	}

	return model;
}

Texture* Ag::SceneCache::GetTextureUsingCache(StringId path, TextureSrgbMode srgbMode)
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
		auto& engine = *Ag::GraphicsEngine::GetInstance();
		auto& textureManager = engine.GetTextureManager();

		texture = textureManager.GetTexture(path.GetString(), srgbMode);
		myTextureCache[path] = texture;
	}

	return texture;
}

Scene* Ag::SceneCache::GetSceneUsingCache(StringId path)
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
		auto& editor = *Ag::Editor::GetEditor();
		auto& sceneManager = editor.GetEditorSceneManager();

		scene = sceneManager.Get(path.GetString());
		mySceneCache[path] = scene;
	}

	return scene;
}


// One TLAS instance per sub-mesh, matching GameWorld's own TLAS build.
static void CollectRayInstances(const Ag::ModelInstance& anInstance, const Ag::Matrix4x4f& aTransform,
	std::vector<Ag::rhi::RaytracingInstanceDesc>& outInstances)
{
	using namespace Ag;
	rhi::IDevice* device = DX11::Rhi();
	const std::shared_ptr<Model> model = anInstance.GetModel();
	if (!device || !model) return;

	size_t meshIndex = 0;
	for (const Model::MeshData& mesh : model->GetMeshDataList())
	{
		const size_t mesh_ = meshIndex++;
		if (!mesh.rayGeometry.blas.IsValid()) continue;

		rhi::RaytracingInstanceDesc desc = {};
		desc.blas = mesh.rayGeometry.blas;
		desc.instanceId = (uint32_t)outInstances.size();
		desc.vertexSrv = device->RegisterRaySceneSrv(mesh.rayGeometry.vertexRawSrv);
		desc.indexSrv = device->RegisterRaySceneSrv(mesh.rayGeometry.indexRawSrv);
		desc.materialIndex = mesh_ < MAX_MESHES_PER_MODEL && anInstance.GetMaterialOverride(mesh_)
			? anInstance.GetMaterialOverride(mesh_) : mesh.rayGeometry.materialIndex;
		desc.rayOpaque = RayTracingMaterialTable::IsRayOpaque(desc.materialIndex);
		desc.vertexStride = mesh.rayGeometry.vertexStride;
		desc.positionOffset = mesh.rayGeometry.positionOffset;
		desc.normalOffset = mesh.rayGeometry.normalOffset;
		desc.uv0Offset = mesh.rayGeometry.uv0Offset;
		desc.tangentOffset = mesh.rayGeometry.tangentOffset;
		desc.binormalOffset = mesh.rayGeometry.binormalOffset;
		desc.vertexFormat = (uint32_t)mesh.rayGeometry.vertexFormat;
		desc.indexCount = mesh.numberOfIndices;
		for (uint32_t row = 0; row < 3; ++row)
			for (uint32_t col = 0; col < 4; ++col)
			{
				desc.transform[row * 4 + col] = aTransform(col + 1, row + 1);
				desc.previousTransform[row * 4 + col] = aTransform(col + 1, row + 1);
			}
		desc.motionHistoryValid = 1u;   // the editor camera moves, the scene does not
		outInstances.push_back(desc);
	}
}

bool Ag::CheckBounds(const Frustum& frustum, Ag::Matrix4x4f matrix, float maxScale, Model& model)
{
	int meshCount = (int)model.GetMeshCount();
	for (int i = 0; i < meshCount; i++)
	{
		const Ag::BoxSphereBounds& bounds = model.GetMeshData(i).bounds;
		Vector3f transformedCenter = bounds.center * matrix;

		if (CheckFrustum(frustum, transformedCenter, maxScale * bounds.radius))
			return true;
	}

	return false;
}

// ugly workaround to avoid allocations per object when drawing
static int locScenePropertyDepth = -1;
static std::vector<std::vector<ScenePropertyDefinition>> locSceneObjectProperties;

void DrawBounds(const Ag::BoxSphereBounds& bounds, Ag::Vector4f color)
{
	bounds;
	color;

	auto& debug = Ag::GraphicsEngine::GetInstance()->GetLineDrawer();
	Ag::LinePrimitive primitive{};
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



bool Ag::DrawSceneProperty(const ScenePropertyDefinition& property, float maxScale, DrawParameters& drawParameters)
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
					Ag::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance, locRenderdata.idAnimatedModelShader);
				}
				else
				{
					ModelInstance instance;
					instance.Init(model);
					Ag::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance, locRenderdata.idModelShader);
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

			Ag::GraphicsEngine::GetInstance()->GetSpriteDrawer().Draw(sharedData, instance);

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

					ApplyModelMaterials(value, instance, drawParameters.cache);

					// todo override shader
					Ag::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance);
				}
				else
				{
					ModelInstance instance;
					instance.Init(model);

					ApplyModelMaterials(value, instance, drawParameters.cache);

					if (drawParameters.rayInstances)
					{
						CollectRayInstances(instance, graphicsStateStack.GetTransform(), *drawParameters.rayInstances);
					}
					else if (drawParameters.meshPass != DrawParameters::MeshPass::All)
					{
						// Split by material: glass goes to the forward transparent pass.
						const bool wantTransparent = drawParameters.meshPass == DrawParameters::MeshPass::Transparent;
						std::vector<int> meshes;
						for (int m = 0; m < (int)model->GetMeshCount(); ++m)
						{
							const uint32_t material = m < MAX_MESHES_PER_MODEL && instance.GetMaterialOverride(m)
								? instance.GetMaterialOverride(m) : model->GetMeshData(m).rayGeometry.materialIndex;
							const bool transparent = RayTracingMaterialTable::GetRayVisibility(material) == RayTracingMaterialTable::kRayTransparent;
							if (transparent == wantTransparent) meshes.push_back(m);
						}
						const ModelShader& shader = drawParameters.overrideModelShader
							? *drawParameters.overrideModelShader : Ag::GraphicsEngine::GetInstance()->GetModelDrawer().GetPbrShader();
						instance.Render(shader, meshes);
					}
					else if (drawParameters.overrideModelShader)
					{
						Ag::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance, *drawParameters.overrideModelShader);

					}
					else
					{
						Ag::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance);
					}
				}

				if (drawParameters.drawBounds && drawParameters.drawHelpers)
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
			Ag::GraphicsEngine::GetInstance()->GetSpriteDrawer().Draw(sharedData, instance);

			hasBeenRendered = true;
		}
	}

	return hasBeenRendered;
}

void Ag::DrawSceneObject(const SceneObject& sceneObject, DrawParameters& drawParameters)
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

	if (!hasBeenRendered && drawParameters.drawHelpers)
	{
		std::shared_ptr<Model> model = drawParameters.cache.GetModelUsingCache("models/locator.fbx"_tgaid);

		if (model && CheckBounds(drawParameters.frustum, graphicsStateStack.GetTransform(), maxScale, *model))
		{
			ModelInstance instance;
			instance.Init(model);

			if (drawParameters.useIdShader)
			{
				Ag::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance, locRenderdata.idModelShader);
			}
			else
			{
				Ag::GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance);

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

void Ag::DrawScene(const Scene& scene, DrawParameters& drawParameters)
{
	EnsureInitialized();

	for (auto& p : scene.GetSceneObjects())
	{
		DrawSceneObject(*p.second, drawParameters);
	}
}
