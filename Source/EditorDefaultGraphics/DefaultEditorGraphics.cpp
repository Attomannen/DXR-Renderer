#include "stdafx.h"
#include "DefaultEditorGraphics.h"
#include <age/render/CubemapPrefilter.h>

#include <filesystem>
#include <imgui.h>
#include <age/editor/Scene/ActiveScene.h>
#include <age/editor/Scene/SceneSelection.h>
#include <SceneUtil.h>
#include <age/editor/Tools/Viewport/Viewport.h>

#include "age/animation/Animation.h"
#include "age/animation/AnimationClip.h"
#include "age/animation/AnimationPlayer.h"
#include "age/graphics/GraphicsEngine.h"
#include "age/graphics/GraphicsStateStack.h"
#include "age/drawers/LineDrawer.h"
#include "age/drawers/ModelDrawer.h"
#include "age/model/AnimatedModelInstance.h"
#include "age/model/ModelFactory.h"
#include "age/primitives/LinePrimitive.h"
#include "age/render/RenderCommon.h"
#include "age/render/RenderGraph.h"
#include "age/render/DeferredRenderer.h"
#include <age/graphics/RenderTarget.h>
#include <age/graphics/DepthBuffer.h>
#include <age/texture/TextureManager.h>
#include <age/graphics/AmbientLight.h>
#include <age/graphics/DirectionalLight.h>
#include "age/settings/settings.h"
#include "age/texture/TextureManager.h"
#include "age/texture/texture.h"
#include "age/scene/ScenePropertyTypes.h"
#include "age/shaders/ModelShader.h"
#include <age/editor/ObjectDefinition/ObjectDefinitionDocument.h>
#include <age/shaders/SpriteShader.h>
#include <age/imgui/ImGuiPropertyEditor.h>
#include <age/editor/Editor.h>
#include <age/editor/Material/MaterialAsset.h>
#include "Material/MaterialGraphBake.h"
#include <age/render/RayTracingMaterialTable.h>
#include <age/editor/imgui_widgets/imgui_widgets.h>
#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>
#include <age/model/ModelInstance.h>
#include <age/rhi/ConstantBuffer.h>

using namespace Ag;

namespace Ag
{
	class DefaultObjectDefinitionEditorGraphics : public ObjectDefinitionEditorGraphicsBase
	{
	public:
		DefaultObjectDefinitionEditorGraphics()
		{
			if (!GraphicsEngine::GetInstance())
				GraphicsEngine::Start();

			myPreviewSettings.previewPixelShaderPath = "shaders/model_shader_PS"_tgaid;
			UpdatePreviewShaders();
		}
		void Draw(ObjectDefinitionDrawParameters& parameters) override;
		void DrawVisualPreviewSettings() override;
		SceneCache myCache;
		struct ObjectEditorPreviewSettings
		{
			StringId previewPixelShaderPath;
			ModelShader previewModelShader;
			SpriteShader previewSpriteShader;

			StringId cubeMapPath;
			AmbientLight ambientLight;
			float directionalLightYaw = 45.f;
			float directionalLightPitch = -45.f;
			Color ambientColor = { 0.1f, 0.5f, 0.8f };
			float ambientColorMultiplier = 1.0f;
			Color directionalLightColor = { 0.9f, 0.7f, 0.5f };
			float directionalLightColorMultiplier = 1.4f;
		};
	private:
		void UpdatePreviewShaders()
		{
			//	myPreviewSettings.previewPixelShaderPath = "shaders/model_shader_PS"_tgaid;
			// myPreviewSettings.previewPixelShaderPath = "shaders/model_shader_PS"_tgaid;

			myPreviewSettings.previewModelShader = {};
			myPreviewSettings.previewModelShader.Init("shaders/PbrModelShaderVS", myPreviewSettings.previewPixelShaderPath.GetString());

			myPreviewSettings.previewSpriteShader = {};
			myPreviewSettings.previewSpriteShader.Init("Shaders/instanced_sprite_shader_VS", myPreviewSettings.previewPixelShaderPath.GetString());

		}
		ObjectEditorPreviewSettings myPreviewSettings = {};

	};

	class  DefaultSceneEditorGraphics : public SceneEditorGraphicsBase
	{
	public:
		DefaultSceneEditorGraphics()
		{
			if (!GraphicsEngine::GetInstance())
				GraphicsEngine::Start();
		} 
		void Draw(const SceneDrawParameters& parameters) override;

	private:
		// Lit colour pass through the engine's deferred renderer. Returns false
		// when the renderer is unavailable so the caller can draw forward.
		bool DrawDeferredColorPass(const SceneDrawParameters& parameters, Frustum& frustum);

		SceneCache myCache;

		// Routes the color pass through the real deferred pipeline (G-buffer,
		// cascaded shadows, PBR resolve, tonemap) instead of a flat forward
		// draw, so the editor viewport matches what the game actually looks
		// like -- see the Draw()'s color-pass block for the DX11::BackBuffer/
		// DepthBuffer global-swap this requires (DeferredRenderer assumes
		// those globals rather than taking an explicit render target).
		// Point/spot lights aren't included yet: the scene format has no
		// light scene-object type to source them from. The single directional
		// sun + ambient (with real cascaded shadows) now live on Scene itself
		// (see Scene::GetSunYaw() etc.) instead of here, so the hierarchy
		// panel's Sun/Ambient pseudo-entries (SceneLightSelection) can expose
		// and edit the same state this reads.
		Vector2ui myDeferredResolution{ 0, 0 };
		// Authored equirectangular environments (.hdr panoramas) converted to a
		// prefiltered cubemap, keyed by the path they came from so the
		// conversion happens on change rather than per frame.
		std::unique_ptr<CubemapPrefilter> myEnvPrefilter;
		CubemapData myEnvPrefiltered;
		std::string myEnvPrefilteredPath;
		bool myEnvPrefilterFailed = false;
		// TLAS instances, rebuilt only when the scene's geometry changes.
		std::vector<rhi::RaytracingInstanceDesc> myRayInstances;
		uint64_t mySceneStamp = 0;
		bool myRayTracedViewport = true;
	};

	class  DefaultAnimationClipEditorGraphics : public AnimationClipEditorGraphicsBase
	{
	public:
		DefaultAnimationClipEditorGraphics()
		{
			if (!GraphicsEngine::GetInstance())
				GraphicsEngine::Start();
		}
		void Draw(const AnimationClipDrawParameters& parameters) override;

	private:
		SceneCache myCache;
	};

	// Unreal-style single-material PBR preview: one primitive/mesh lit by an
	// adjustable key light + ambient, drawn either with the flat const-material
	// shader (no maps) or the stock textured PBR shader (maps assigned).
	class DefaultMaterialEditorGraphics : public MaterialEditorGraphicsBase
	{
	public:
		enum class PreviewViewMode { Lit, Unlit, Wireframe, Normals };

		DefaultMaterialEditorGraphics()
		{
			if (!GraphicsEngine::GetInstance())
				GraphicsEngine::Start();

			myConstShader.Init("shaders/PbrModelShaderVs", "shaders/PbrConstModelShaderPS");
			// Unlit reads the same per-mesh textures myInstance.SetTexture() below
			// already binds -- it just skips the lighting stack entirely, same as
			// UE's material preview "Unlit" view mode.
			myUnlitShader.Init("shaders/PbrModelShaderVs", "shaders/model_shader_PS");
			myNormalsShader.Init("shaders/PbrModelShaderVs", "shaders/DebugPixelNormalModelShaderPS");
		}

		void Draw(const MaterialEditorDrawParameters& parameters) override;
		void DrawPreviewSettings() override;
		bool BakeMaterialGraph(const MaterialGraphBakeRequest& request) override;

	private:
		ModelShader myConstShader;
		ModelShader myUnlitShader;
		ModelShader myNormalsShader;
		std::string myPreviewMeshLoaded;
		ModelInstance myInstance;

		PreviewViewMode myViewMode = PreviewViewMode::Lit;
		bool myAutoRotate = true;
		float myRotationSpeed = 30.f;   // degrees/sec
		float myRotationAngle = 0.f;

		float myLightYaw = 45.f;
		float myLightPitch = -40.f;
		Color myLightColor = { 1.0f, 0.98f, 0.95f };
		float myLightIntensity = 2.2f;
		Color myAmbientColor = { 0.25f, 0.30f, 0.38f };
		float myAmbientIntensity = 1.0f;
		StringId myCubeMapPath;
		AmbientLight myAmbient;
	};

}

void DefaultObjectDefinitionEditorGraphics::Draw(ObjectDefinitionDrawParameters& parameters)
{
	if (!GraphicsEngine::GetInstance())
		GraphicsEngine::Start();
	GraphicsEngine::GetInstance()->BeginFrame();
	// Asset edits still show up while the editor runs, but re-reading every
	// model, texture and material from disk every frame is far too expensive.
	myCache.ClearCacheThrottled();
	Camera& renderCamera = parameters.viewport->GetCamera();
	Frustum frustum = CalculateFrustum(renderCamera);

	{
		auto& graphicsStateStack = GraphicsEngine::GetInstance()->GetGraphicsStateStack();

		graphicsStateStack.SetCamera(renderCamera);
		graphicsStateStack.SetBlendState(Ag::BlendState::Disabled);
		parameters.viewport->BeginDraw();

		{
			parameters.viewport->SetupIdPass();
			SetupIdPass();
			DrawParameters drawParameters = {
				.useIdShader = true,
				.drawBounds = false,
				.boundsColor = {},
				.cache = myCache,
				.frustum = frustum,
				.viewport = *parameters.viewport,
				.overrideModelShader = nullptr,
				.previewPoses = &parameters.livePreviewData->poses
			};

			std::span<const ScenePropertyDefinition> properties = parameters.objectDefinition->GetProperties();
			for (int propertyIndex = 0; propertyIndex < properties.size(); propertyIndex++)
			{
				const ScenePropertyDefinition& prop = properties[propertyIndex];

				SetObjectAndSelectionId(1 + propertyIndex, prop.name == parameters.selectedProperty ? 1 + propertyIndex : 0);

				DrawSceneProperty(prop, 1.f, drawParameters);
			}
		}

		{
			DrawParameters drawParameters = {
				.useIdShader = false,
				.drawBounds = false,
				.boundsColor = {},
				.cache = myCache,

				.frustum = frustum,
				.viewport = *parameters.viewport,

				.overrideModelShader = &myPreviewSettings.previewModelShader,
				.previewPoses = &parameters.livePreviewData->poses
			};

			parameters.viewport->SetupColorPass();

			myPreviewSettings.ambientLight.color = myPreviewSettings.ambientColorMultiplier * myPreviewSettings.ambientColor;

			graphicsStateStack.SetAmbientLight(myPreviewSettings.ambientLight);
			graphicsStateStack.SetDirectionalLight(DirectionalLight{ Matrix4x4f::CreateFromRollPitchYaw({myPreviewSettings.directionalLightPitch, myPreviewSettings.directionalLightYaw, 0.f}), myPreviewSettings.directionalLightColorMultiplier * myPreviewSettings.directionalLightColor, 0.f });

			std::span<const ScenePropertyDefinition> properties = parameters.objectDefinition->GetProperties();
			for (int propertyIndex = 0; propertyIndex < properties.size(); propertyIndex++)
			{
				const ScenePropertyDefinition& prop = properties[propertyIndex];

				DrawSceneProperty(prop, 1.f, drawParameters);
			}

		}

		DrawOutlines(*parameters.viewport);
		parameters.viewport->EndDraw();
	}
	GraphicsEngine::GetInstance()->EndFrame();

}
void Ag::DefaultObjectDefinitionEditorGraphics::DrawVisualPreviewSettings()
{
	if (PropertyEditor::PropertyHeader("Default Value"))
	{
		if (PropertyEditor::BeginPropertyTable())
		{
			PropertyEditor::PropertyLabel();
			ImGui::Text("Preview Pixel Shader");
			PropertyEditor::PropertyValue();
			ImGui::Text(myPreviewSettings.previewPixelShaderPath.GetString());


			if (ImGui::Button("Set To Unlit Textured"))
			{
				myPreviewSettings.previewPixelShaderPath = "shaders/model_shader_PS"_tgaid;
				UpdatePreviewShaders();
			}
			if (ImGui::Button("Set To Unlit Vertex Color"))
			{
				myPreviewSettings.previewPixelShaderPath = "shaders/model_shader_vertex_color_PS"_tgaid;
				UpdatePreviewShaders();
			}
			if (ImGui::Button("Set To Unlit Textured + Vertex Color"))
			{
				myPreviewSettings.previewPixelShaderPath = "shaders/model_shader_vertex_color_textured_PS"_tgaid;
				UpdatePreviewShaders();
			}
			if (ImGui::Button("Set To Lambert Lighting"))
			{
				myPreviewSettings.previewPixelShaderPath = "shaders/LambertModelShaderPS"_tgaid;
				UpdatePreviewShaders();
			}
			if (ImGui::Button("Set To PBR Lighting"))
			{
				myPreviewSettings.previewPixelShaderPath = "shaders/PbrModelShaderPS"_tgaid;
				UpdatePreviewShaders();
			}
			if (ImGui::Button("Set To Vertex Normal Debug"))
			{
				myPreviewSettings.previewPixelShaderPath = "shaders/DebugVertexNormalModelShaderPS"_tgaid;
				UpdatePreviewShaders();
			}
			if (ImGui::Button("Set To Pixel Normal Debug"))
			{
				myPreviewSettings.previewPixelShaderPath = "shaders/DebugPixelNormalModelShaderPS"_tgaid;
				UpdatePreviewShaders();
			}
			if (ImGui::Button("Set To Roughness Debug"))
			{
				myPreviewSettings.previewPixelShaderPath = "shaders/DebugRoughnessModelShaderPS"_tgaid;
				UpdatePreviewShaders();
			}
			if (ImGui::Button("Set To Metalness Debug"))
			{
				myPreviewSettings.previewPixelShaderPath = "shaders/DebugMetalnessModelShaderPS"_tgaid;
				UpdatePreviewShaders();
			}
			if (ImGui::Button("Set To Ambient Occlusion Debug"))
			{
				myPreviewSettings.previewPixelShaderPath = "shaders/DebugAmbientOcclusionModelShaderPS"_tgaid;
				UpdatePreviewShaders();
			}
			if (ImGui::Button("Set To Emissive Debug"))
			{
				myPreviewSettings.previewPixelShaderPath = "shaders/DebugEmissiveModelShaderPS"_tgaid;
				UpdatePreviewShaders();
			}
			PropertyEditor::PropertyLabel();
			ImGui::Text("Directional Light Yaw");
			PropertyEditor::PropertyValue();
			ImGui::DragFloat("##Light Yaw", &myPreviewSettings.directionalLightYaw);

			PropertyEditor::PropertyLabel();
			ImGui::Text("Directional Light Pitch");
			PropertyEditor::PropertyValue();
			ImGui::DragFloat("##Light Pitch", &myPreviewSettings.directionalLightPitch);

			PropertyEditor::PropertyLabel();
			ImGui::Text("Directional Light Color");
			PropertyEditor::PropertyValue();
			ImGui::ColorEdit3("##Directional Light Color", &myPreviewSettings.directionalLightColor.r, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);

			PropertyEditor::PropertyLabel();
			ImGui::Text("Directional Light Multiplier");
			PropertyEditor::PropertyValue();
			ImGui::DragFloat("##Directional Light Color", &myPreviewSettings.directionalLightColorMultiplier);

			PropertyEditor::PropertyLabel();
			ImGui::Text("Ambient Light Color");
			PropertyEditor::PropertyValue();
			ImGui::ColorEdit3("##Ambient Light Color", &myPreviewSettings.ambientColor.r, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);

			PropertyEditor::PropertyLabel();
			ImGui::Text("Ambient Light Multiplier");
			PropertyEditor::PropertyValue();
			ImGui::DragFloat("##Ambient Light Color", &myPreviewSettings.ambientColorMultiplier);

			ImGui::PushID("Cubemap");

			PropertyEditor::PropertyLabel();
			ImGui::Text("Ambient Cube Map");
			PropertyEditor::PropertyValue();
			{
				StringId cubeMap = myPreviewSettings.cubeMapPath;
				if (PropertyEditor::AssetField("##CubeMap", cubeMap, { ".dds" }, "None (Cubemap)"))
				{
					myPreviewSettings.cubeMapPath = cubeMap;
					if (cubeMap.IsEmpty())
					{
						myPreviewSettings.ambientLight.type = AmbientLightType::Uniform;
					}
					else
					{
						myPreviewSettings.ambientLight.type = AmbientLightType::Custom;
						myPreviewSettings.ambientLight.cubemap = GraphicsEngine::GetInstance()->GetTextureManager().GetTexture(cubeMap.GetString());
					}
				}
			}

			if (ImGui::Button("Set To Uniform"))
			{
				myPreviewSettings.cubeMapPath = {};
				myPreviewSettings.ambientLight.type = AmbientLightType::Uniform;
			}

			if (ImGui::Button("Set To Above Horizon"))
			{
				myPreviewSettings.cubeMapPath = {};
				myPreviewSettings.ambientLight.type = AmbientLightType::UniformAboveHorizon;
			}

			ImGui::PopID();

			PropertyEditor::EndPropertyTable();

		}
	}
}
void DefaultSceneEditorGraphics::Draw(const SceneDrawParameters& parameters)
{
	if (!GraphicsEngine::GetInstance())
		GraphicsEngine::Start();

	GraphicsEngine::GetInstance()->BeginFrame();

	// Asset edits still show up while the editor runs, but re-reading every
	// model, texture and material from disk every frame is far too expensive.
	myCache.ClearCacheThrottled();

	const Camera& renderCamera = parameters.viewport->GetCamera();
	Frustum frustum = CalculateFrustum(renderCamera);

	{
		parameters.viewport->BeginDraw();
		auto& graphicsStateStack = GraphicsEngine::GetInstance()->GetGraphicsStateStack();

		graphicsStateStack.SetCamera(renderCamera);
		graphicsStateStack.SetBlendState(Ag::BlendState::Disabled);

		std::vector<ScenePropertyDefinition> sceneObjectProperties;

		{ // One pass to render ID
			parameters.viewport->SetupIdPass();
			SetupIdPass();

			DrawParameters drawParameters = {
				.useIdShader = true,
				.drawBounds = false,
				.boundsColor = {},
				.cache = myCache,
				.frustum = frustum,
				.viewport = *parameters.viewport,
				.overrideModelShader = nullptr
			};

			for (auto& p : GetActiveScene()->GetSceneObjects())
			{
				SetObjectAndSelectionId(
					p.first,
					SceneSelection::GetActiveSceneSelection()->Contains(p.first) ? p.first : 0
				);

				DrawSceneObject(*p.second, drawParameters);
			}
		}

		if (!DrawDeferredColorPass(parameters, frustum))
		{
			// Forward fallback: flat PBR, no shadows or glass.
			parameters.viewport->SetupColorPass();

			DrawParameters drawParameters = {
				.useIdShader = false,
				.drawBounds = false,
				.boundsColor = {},
				.cache = myCache,
				.frustum = frustum,
				.viewport = *parameters.viewport,
				.overrideModelShader = nullptr
			};


			for (auto& p : GetActiveScene()->GetSceneObjects())
			{
				drawParameters.boundsColor = Ag::Vector4f(0.f, 1.f, 0.f, 1.f);
				if (ImGui::GetIO().KeyShift)
				{
					drawParameters.boundsColor = SceneSelection::GetActiveSceneSelection()->Contains(p.first) ? Ag::Vector4f(0.f, 0.f, 0.f, 0.0f) : Ag::Vector4f(0.f, 1.f, 0.f, 1.f);
				}

				DrawSceneObject(*p.second, drawParameters);
			}
		}
	}
	DrawOutlines(*parameters.viewport);
	parameters.viewport->EndDraw();

	GraphicsEngine::GetInstance()->EndFrame();

}

// Point and spot lights authored as scene objects, in the renderer's format.
static void CollectSceneLights(const Scene& aScene, std::vector<DeferredLight>& outLights)
{
	for (const auto& entry : aScene.GetSceneObjects())
	{
		const SceneObject& object = *entry.second;
		if (!object.IsLight() || (int)outLights.size() >= DeferredRenderer::kMaxLights) continue;

		const Matrix4x4f transform = object.GetTransform();
		const Vector3f position = transform.GetPosition();
		const float* color = object.GetLightColor();
		DeferredLight light = {};
		light.position[0] = position.x; light.position[1] = position.y; light.position[2] = position.z;
		light.range = object.GetLightRange();
		light.color[0] = color[0]; light.color[1] = color[1]; light.color[2] = color[2];
		light.radius = object.GetLightRadius();
		if (object.GetType() == SceneObjectType::SpotLight)
		{
			const Vector3f direction = transform.GetForward();
			light.spotDir[0] = direction.x; light.spotDir[1] = direction.y; light.spotDir[2] = direction.z;
			light.spotCosOuter = std::cos(DegToRad(object.GetLightOuterAngle()));
			light.spotCosInner = std::cos(DegToRad(std::min(object.GetLightInnerAngle(), object.GetLightOuterAngle())));
		}
		else
		{
			light.spotDir[1] = -1.f;
			light.spotCosOuter = -1.f;
			light.spotCosInner = -1.f;
		}
		light.shadowSlot = -1.f;
		outLights.push_back(light);
	}
}

// Changes that invalidate the ray-tracing instance list: which objects exist,
// where they are and which model / materials they use.
static uint64_t HashSceneGeometry(const Scene& aScene)
{
	uint64_t hash = 1469598103934665603ull;
	auto mix = [&hash](uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
	auto mixFloat = [&mix](float value) { uint32_t bits; std::memcpy(&bits, &value, 4); mix(bits); };
	for (const auto& entry : aScene.GetSceneObjects())
	{
		mix(entry.first);
		const TRS& trs = entry.second->GetTRS();
		mixFloat(trs.translation.x); mixFloat(trs.translation.y); mixFloat(trs.translation.z);
		mixFloat(trs.rotation.x); mixFloat(trs.rotation.y); mixFloat(trs.rotation.z);
		mixFloat(trs.scale.x); mixFloat(trs.scale.y); mixFloat(trs.scale.z);
	}
	return hash;
}

bool DefaultSceneEditorGraphics::DrawDeferredColorPass(const SceneDrawParameters& parameters, Frustum& frustum)
{
	GraphicsEngine& ge = *GraphicsEngine::GetInstance();
	DeferredRenderer& dr = ge.GetDeferredRenderer();
	Scene* scene = parameters.scene ? parameters.scene : GetActiveScene();
	size_t forwardFlagLength = 0;
	getenv_s(&forwardFlagLength, nullptr, 0, "AGE_EDITOR_FORWARD");   // any value keeps the old forward view
	if (!dr.IsReady() || !scene || forwardFlagLength > 0) return false;

	EditorViewport& viewport = *parameters.viewport;
	const Vector2ui resolution = viewport.GetRenderTarget().GetResolution();
	if (resolution.x == 0 || resolution.y == 0) return false;

	// The deferred renderer follows this viewport, not the editor window.
	ge.SetDeferredFollowsWindowSize(false);
	if (resolution != myDeferredResolution || resolution != dr.GetResolution())
	{
		dr.OnResize(resolution);
		myDeferredResolution = resolution;
	}

	// Scene lighting. Scene pitch is negative-down; the renderer's is positive-down.
	auto& gss = ge.GetGraphicsStateStack();
	const float* sunColor = scene->GetSunColor();
	const float sunIntensity = scene->GetSunIntensity();
	DirectionalLight sun{};
	sun.transform = Matrix4x4f::CreateFromRollPitchYaw({ -scene->GetSunPitch(), scene->GetSunYaw(), 0.f });
	sun.color = Color{ sunColor[0] * sunIntensity, sunColor[1] * sunIntensity, sunColor[2] * sunIntensity };
	gss.SetDirectionalLight(sun);

	const float* ambientColor = scene->GetAmbientColor();
	AmbientLight ambient{};
	ambient.color = Color{ ambientColor[0], ambientColor[1], ambientColor[2] };
	ambient.type = AmbientLightType::Custom;
	// The scene's own environment picker (SceneObjectProperties.cpp,
	// DrawEnvironmentTexturePicker) already saves this path -- the editor
	// viewport ignoring it and always showing horizonCubeMap instead was
	// exactly the "what you author is not what you see" gap the roadmap
	// calls out. Empty (no explicit choice) still falls back to the same
	// default as before, so every existing scene's look is unchanged; only
	// scenes that actually pick something now see it take effect.
	const std::string& envPath = scene->GetEnvironmentTexturePath();
	const char* envAsset = envPath.empty() ? "Textures/horizonCubeMap.dds" : envPath.c_str();
	ambient.cubemap = ge.GetTextureManager().GetTexture(envAsset, TextureSrgbMode::None);
	if (!ambient.cubemap) ambient.type = AmbientLightType::Uniform;

	// An authored .hdr is an equirectangular panorama, not a cube. Everything
	// that consumes the environment declares it as TextureCube, and binding a
	// 2D view to a cube register is undefined behaviour -- on this GPU it has
	// been seen to remove the device outright, and at best it samples as black
	// (which is what "environment sky averages 1e-08" was reporting).
	//
	// So convert it once, through the same prefilter the game uses, and cache
	// the result against the path that produced it.
	rhi::SrvHandle environmentSrv = ambient.cubemap ? ambient.cubemap->GetSrv() : rhi::SrvHandle{};
	if (ambient.cubemap && !TextureManager::IsCubemapAsset(envAsset))
	{
		if (myEnvPrefilteredPath != envAsset)
		{
			myEnvPrefilteredPath = envAsset;
			myEnvPrefilterFailed = false;
			myEnvPrefiltered.Reset();
			if (!myEnvPrefilter)
			{
				myEnvPrefilter = std::make_unique<CubemapPrefilter>();
				if (!myEnvPrefilter->Init()) myEnvPrefilter.reset();
			}
			CubemapData baseCube;
			if (myEnvPrefilter &&
				myEnvPrefilter->BuildCubemapFromEquirectangular(ambient.cubemap->GetSrv(), 512, baseCube) &&
				myEnvPrefilter->GeneratePrefilteredCubemap(baseCube.GetSrv(), 512, 256, 128, myEnvPrefiltered))
			{
				INFO_PRINT("editor: environment '%s' converted from equirectangular to a prefiltered cubemap", envAsset);
			}
			else
			{
				// The guard. Falling back to a known cubemap keeps the viewport
				// lit and, more importantly, keeps a 2D view out of a cube slot.
				myEnvPrefiltered.Reset();
				myEnvPrefilterFailed = true;
				ERROR_PRINT("editor: environment '%s' is not a cubemap and could not be converted; falling back to Textures/horizonCubeMap.dds", envAsset);
			}
		}

		if (myEnvPrefiltered.IsValid())
		{
			environmentSrv = myEnvPrefiltered.GetSrv();
		}
		else
		{
			Texture* fallback = ge.GetTextureManager().GetTexture("Textures/horizonCubeMap.dds", TextureSrgbMode::None);
			environmentSrv = fallback ? fallback->GetSrv() : rhi::SrvHandle{};
			if (fallback) ambient.cubemap = fallback;
		}
	}
	gss.SetAmbientLight(ambient);

	// Scene point / spot lights. Authored as ordinary scene objects, so one
	// pass over the hierarchy collects them.
	std::vector<DeferredLight> lights;
	CollectSceneLights(*scene, lights);
	dr.UploadLights(lights.data(), (int)lights.size());

	// The ray-traced path samples this cube for sky and ambient light.
	if (environmentSrv.IsValid())
		dr.SetGiEnvironment(environmentSrv, { ambientColor[0], ambientColor[1], ambientColor[2] }, true);
	else
		dr.SetGiEnvironment({}, { 0.f, 0.f, 0.f }, false);
	// No irradiance probe volume in the editor yet.
	dr.SetGiVolume({ 0.f, 0.f, 0.f }, { 1.f, 1.f, 1.f }, 1, 1, 1, 0.f, false);
	dr.SetReflectionProbeBox({ 0.f, 0.f, 0.f }, { 1.f, 1.f, 1.f }, false);

	// Ray tracing when the device supports it: the same renderer the game uses,
	// fed by a TLAS built from this scene. Rebuilt only when the scene changes.
	rhi::IDevice* device = DX11::Rhi();
	const bool wantRayTracing = device && device->SupportsRaytracingTier11() && myRayTracedViewport;
	const Camera& camera = viewport.GetCamera();
	dr.SetShadows(true);
	dr.SetSSAO(true);
	dr.SetSSR(true);
	dr.SetClustered(true);
	// Was forced off: point/spot lights placed in the editor cast no shadows
	// at all, unlike in the game -- one more "what you author is not what
	// you see" gap (roadmap 1.2/4.4).
	dr.SetLocalShadows(true);
	dr.SetPostFx(true);
	const Vector3f cameraPos = camera.GetTransform().GetPosition();
	dr.SetShadowLight(sun.transform.GetForward(), cameraPos, 5000.f);
	dr.SetCamera(camera);
	gss.SetCamera(camera);
	// The id pass and editor overlays leave GPU state behind that the state
	// stack does not track; start the lit frame from a known state.
	gss.SetBlendState(BlendState::Disabled);
	gss.SetDepthStencilState(DepthStencilState::WriteLess);
	gss.SetRasterizerState(RasterizerState::BackfaceCulling);
	gss.UpdateGpuStates(true);

	bool rayTracing = false;
	if (wantRayTracing)
	{
		// The instance list only changes when the scene does; a moving editor
		// camera must not rebuild it.
		const uint64_t sceneStamp = HashSceneGeometry(*scene);
		if (sceneStamp != mySceneStamp || myRayInstances.empty())
		{
			myRayInstances.clear();
			DrawParameters collect = {
				.useIdShader = false,
				.drawBounds = false,
				.boundsColor = {},
				.cache = myCache,
				.frustum = frustum,
				.viewport = viewport,
				.overrideModelShader = nullptr,
			};
			collect.drawHelpers = false;
			collect.rayInstances = &myRayInstances;
			for (auto& object : scene->GetSceneObjects())
				DrawSceneObject(*object.second, collect);
			mySceneStamp = sceneStamp;
		}
		if (!myRayInstances.empty())
		{
			device->BuildRaytracingTlas(myRayInstances.data(), (uint32_t)myRayInstances.size());
			dr.SetRaySceneStationary(true);   // static scene: only the camera moves
			rayTracing = true;
		}
	}
	dr.SetDxrRenderer(rayTracing);

	// DeferredRenderer draws into the DX11 back/depth buffer globals.
	RenderTarget* savedBackBuffer = DX11::BackBuffer;
	DepthBuffer* savedDepthBuffer = DX11::DepthBuffer;
	DX11::BackBuffer = &viewport.GetRenderTarget();
	DX11::DepthBuffer = &viewport.GetColorDepthBuffer();
	viewport.GetColorDepthBuffer().Clear(1.0f, 0);

	auto makeParameters = [&](DrawParameters::MeshPass aPass, ModelShader* aShader, Frustum& aFrustum)
	{
		DrawParameters p = {
			.useIdShader = false,
			.drawBounds = false,
			.boundsColor = {},
			.cache = myCache,
			.frustum = aFrustum,
			.viewport = viewport,
			.overrideModelShader = aShader,
		};
		p.meshPass = aPass;
		p.drawHelpers = false;
		return p;
	};
	auto drawScene = [this, scene](DrawParameters& p)
	{
		for (auto& object : scene->GetSceneObjects())
			DrawSceneObject(*object.second, p);
	};

	ModelShader& geometryShader = const_cast<ModelShader&>(dr.GetGeometryShader());
	ModelShader& shadowShader = const_cast<ModelShader&>(dr.GetShadowShader());
	ModelShader* glassShader = dr.HasGlassShader() ? &const_cast<ModelShader&>(dr.GetGlassShader()) : nullptr;

	auto drawOpaque = [&]()
	{
		DrawParameters p = makeParameters(DrawParameters::MeshPass::Opaque, &geometryShader, frustum);
		drawScene(p);
	};
	auto drawTransparent = [&]()
	{
		DrawParameters p = makeParameters(DrawParameters::MeshPass::Transparent, glassShader, frustum);
		drawScene(p);
	};
	auto drawShadowCasters = [&](const Camera& shadowCamera)
	{
		Frustum shadowFrustum = CalculateFrustum(shadowCamera);
		DrawParameters p = makeParameters(DrawParameters::MeshPass::Opaque, &shadowShader, shadowFrustum);
		drawScene(p);
	};

	{
		RenderGraph graph(ge.GetRenderResourcePool(), nullptr);
		// AGE_EDITOR_GBUF=1..8 shows one G-buffer channel instead of lighting.
		char channel[8] = {};
		size_t channelLength = 0;
		getenv_s(&channelLength, channel, sizeof(channel), "AGE_EDITOR_GBUF");
		dr.BuildFrame(graph, drawOpaque, drawTransparent, drawShadowCasters, channelLength ? atoi(channel) : 0);
		graph.Execute();
	}

	DX11::BackBuffer = savedBackBuffer;
	DX11::DepthBuffer = savedDepthBuffer;

	// Editor overlay on top of the lit image.
	gss.SetCamera(camera);
	gss.UpdateGpuStates(true);
	viewport.DrawGrid();
	return true;
}

void DefaultAnimationClipEditorGraphics::Draw(const AnimationClipDrawParameters& parameters)
{
	Ag::LineDrawer& lineDrawer = GraphicsEngine::GetInstance()->GetLineDrawer();
	myCache.ClearCacheThrottled();

	std::shared_ptr<Model> model;
	FilePathStream dummyPath;
	if (!parameters.clip->previewModelPath.IsEmpty() && Settings::ResolveAssetPath(parameters.clip->previewModelPath, dummyPath))
	{
		model = ModelFactory::GetInstance().GetModel(parameters.clip->previewModelPath.GetString());
	}

	std::shared_ptr<const Animation> animation;
	if (model && !parameters.clip->animationSourcePath.IsEmpty() && Settings::ResolveAssetPath(parameters.clip->animationSourcePath, dummyPath))
	{
		animation = ModelFactory::GetInstance().GetAnimation(parameters.clip->animationSourcePath.GetString(), model->GetSkeleton());
	}

	parameters.viewport->BeginDraw();
	auto& graphicsStateStack = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	const Camera& renderCamera = parameters.viewport->GetCamera();

	graphicsStateStack.SetCamera(renderCamera);
	graphicsStateStack.SetBlendState(Ag::BlendState::Disabled);

	{
		parameters.viewport->SetupIdPass();
		SetupIdPass();
	}

	{
		parameters.viewport->SetupColorPass();

		if (model)
		{

			{

				AnimatedModelInstance instance;
				instance.Init(model);

				const Skeleton* skeleton = instance.GetModel()->GetSkeleton().get();

				ModelSpacePose pose;

				if (animation)
				{
					AnimationPlayer player;
					player.Init(animation);
					player.SetTime(parameters.currentTime);
					player.UpdatePose();

					skeleton->ConvertPoseToModelSpace(player.GetLocalSpacePose(), pose);
				}
				else
				{
					skeleton->ConvertPoseToModelSpace(skeleton->localBindPose, pose);
				}


				if (!skeleton->joints.empty())
					instance.SetPose(pose);

				GraphicsEngine::GetInstance()->GetModelDrawer().Draw(instance);
				GraphicsEngine::GetInstance()->GetGraphicsStateStack().SetBlendState(BlendState::AlphaBlend);

				if (parameters.selectedSkeletonNodeIndex >= 0 || parameters.selectedSkeletonNodeIndex < skeleton->joints.size())
				{
					parameters.viewport->SetColorAsTarget(false);

					// Draw lines to all children, with low transparency
					{
						auto drawChildren = [&](const auto& self, int jointIndex, const Vector3f& parentPos) -> void
							{
								const auto& joint = skeleton->joints[jointIndex];

								for (unsigned childIndex : joint.children)
								{
									Vector3f childPos = pose.jointTransforms[childIndex].GetPosition();

									lineDrawer.Draw(LinePrimitive{ {1.f,1.f, 1.f, 0.2f}, parentPos, childPos });

									self(self, childIndex, childPos);
								}
							};


						drawChildren(drawChildren, 0, pose.jointTransforms[0].GetPosition());
					}

					// Draw lines to parents:
					{

						int index = parameters.selectedSkeletonNodeIndex;
						Vector3f prevPos = pose.jointTransforms[index].GetPosition();
						index = skeleton->joints[index].parent;

						while (index != -1)
						{
							Vector3f pos = pose.jointTransforms[index].GetPosition();

							lineDrawer.Draw(LinePrimitive{ {1.f, 1.f, 1.f}, prevPos, pos });

							index = skeleton->joints[index].parent;
							prevPos = pos;
						}

					}


					// draw axes for the selected node:

					Matrix4x4f m = pose.jointTransforms[parameters.selectedSkeletonNodeIndex];

					Vector4f o = Vector4f(0.f, 0.f, 0.f, 1.f) * m;
					Vector4f x = Vector4f(10.f, 0.f, 0.f, 1.f) * m;
					Vector4f y = Vector4f(0.f, 10.f, 0.f, 1.f) * m;
					Vector4f z = Vector4f(0.f, 0.f, 10.f, 1.f) * m;

					Color colors[3] = { {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, };
					Vector3f from[3] = { o, o, o };
					Vector3f to[3] = { x, y, z };

					Ag::LineMultiPrimitive lines{
						.colors = colors,
						.fromPositions = from,
						.toPositions = to,
						.count = 3
					};
					lineDrawer.Draw(lines);

					parameters.viewport->SetColorAsTarget(true);
				}
			}
		}

	}

	parameters.viewport->EndDraw();
}

void DefaultMaterialEditorGraphics::Draw(const MaterialEditorDrawParameters& parameters)
{
	if (!GraphicsEngine::GetInstance())
		GraphicsEngine::Start();

	MaterialAsset& mat = *parameters.material;

	// (Re)load the preview mesh only when the name changes. Built-in primitives:
	// Sphere / Cube / Cylinder / Cone / Torus / Plane.
	if (mat.previewMesh != myPreviewMeshLoaded)
	{
		myInstance = ModelFactory::GetInstance().GetModelInstance(std::string_view(mat.previewMesh));
		if (!myInstance.IsValid())
			myInstance = ModelFactory::GetInstance().GetModelInstance(std::string_view("Sphere"));
		myPreviewMeshLoaded = mat.previewMesh;
	}

	GraphicsEngine::GetInstance()->BeginFrame();

	auto& stack = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	const Camera& cam = parameters.viewport->GetCamera();
	stack.SetCamera(cam);
	stack.SetBlendState(Ag::BlendState::Disabled);

	parameters.viewport->BeginDraw();
	parameters.viewport->SetupColorPass();

	myAmbient.color = myAmbientIntensity * myAmbientColor;
	stack.SetAmbientLight(myAmbient);
	stack.SetDirectionalLight(DirectionalLight{
		Matrix4x4f::CreateFromRollPitchYaw({ myLightPitch, myLightYaw, 0.f }),
		myLightIntensity * myLightColor, 0.f });

	// Auto-spin, same idea as UE's material preview turntable -- driven off
	// ImGui's frame delta since Draw() isn't handed one of its own.
	if (myAutoRotate)
	{
		myRotationAngle += ImGui::GetIO().DeltaTime * myRotationSpeed;
		if (myRotationAngle > 360.f) myRotationAngle -= 360.f;
	}
	Matrix4x4f xf = Matrix4x4f::CreateFromRollPitchYaw({ 0.f, myRotationAngle, 0.f });
	myInstance.SetTransform(xf);

	int triCount = 0;
	int mapCount = 0;
	for (int j = 0; j < 4; ++j) if (!mat.maps[j].empty()) ++mapCount;

	if (myInstance.IsValid())
	{
		ModelDrawer& md = GraphicsEngine::GetInstance()->GetModelDrawer();
		auto& texMgr = GraphicsEngine::GetInstance()->GetTextureManager();
		const int meshCount = myInstance.GetModel() ? (int)myInstance.GetModel()->GetMeshCount() : 0;
		for (int m = 0; m < meshCount; ++m)
		{
			triCount += myInstance.GetModel()->GetMeshData(m).numberOfIndices / 3;
			for (int j = 0; j < 4; ++j)
			{
				if (mat.maps[j].empty()) continue;
				const TextureSrgbMode srgb = mat.MapIsSrgb(j) ? TextureSrgbMode::ForceSrgbFormat : TextureSrgbMode::ForceNoSrgbFormat;
				if (Texture* t = texMgr.GetTexture(mat.maps[j].c_str(), srgb))
					myInstance.SetTexture(m, j, t);
			}
		}

		// The preview is an ordinary material-table entry, so it renders with
		// exactly the parameters the game uses (constants only without maps).
		const uint32_t materialIndex = RayTracingMaterialTable::GetOrAssignMaterialIndex("editor/materialPreview"_tgaid);
		RayTracingMaterialTable::SetMaterialParams(materialIndex, mat.ToParams());
		myInstance.SetMaterialAll(materialIndex);

		const bool wireframe = myViewMode == PreviewViewMode::Wireframe;
		if (wireframe) stack.SetRasterizerState(RasterizerState::WireframeNoCulling);

		const ModelShader* shader = &myConstShader;
		if (myViewMode == PreviewViewMode::Unlit) shader = &myUnlitShader;
		else if (myViewMode == PreviewViewMode::Normals) shader = &myNormalsShader;
		md.Draw(myInstance, *shader);

		if (wireframe) stack.SetRasterizerState(RasterizerState::BackfaceCulling);
	}

	// Small UE-style floating toolbar (view mode + turntable) and stats line,
	// overlaid directly on the preview viewport rather than living in the
	// separate Preview Settings panel -- those are one-off setup, this is
	// what you reach for while actually looking at the material.
	{
		const Vector2i vpPos = parameters.viewport->GetViewportPos();
		const ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoMove;

		// Every open Material document's DefaultMaterialEditorGraphics draws every
		// frame, not just the focused tab -- a literal "##MaterialPreviewToolbar"
		// window name is identical across all of them, which ImGui treats as one
		// shared window. With two materials open, the last one drawn each frame
		// wins the ID, so an inactive tab's stats/toolbar (e.g. a blank new
		// material's "Maps: 0/4") could visibly clobber another tab's real one.
		// 'this' is unique per document (each owns its own graphics interface
		// instance), so folding it into the id makes each overlay its own window.
		char toolbarId[48], statsId[48];
		snprintf(toolbarId, sizeof toolbarId, "##MatPreviewToolbar%p", (void*)this);
		snprintf(statsId, sizeof statsId, "##MatPreviewStats%p", (void*)this);

		ImGui::SetNextWindowBgAlpha(0.6f);
		ImGui::SetNextWindowPos(ImVec2((float)vpPos.x + 8.f, (float)vpPos.y + 8.f), ImGuiCond_Always);
		if (ImGui::Begin(toolbarId, nullptr, overlayFlags))
		{
			auto modeButton = [&](const char* label, PreviewViewMode mode)
			{
				const bool active = myViewMode == mode;
				if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				if (ImGui::SmallButton(label)) myViewMode = mode;
				if (active) ImGui::PopStyleColor();
			};
			modeButton("Lit", PreviewViewMode::Lit);
			ImGui::SameLine();
			modeButton("Unlit", PreviewViewMode::Unlit);
			ImGui::SameLine();
			modeButton("Wireframe", PreviewViewMode::Wireframe);
			ImGui::SameLine();
			modeButton("Normals", PreviewViewMode::Normals);
			ImGui::SameLine();
			ImGui::Dummy(ImVec2(6.f, 0.f));
			ImGui::SameLine();
			ImGui::Checkbox("Rotate", &myAutoRotate);
			if (myAutoRotate)
			{
				ImGui::SameLine();
				ImGui::SetNextItemWidth(80.f);
				ImGui::DragFloat("##rotspeed", &myRotationSpeed, 1.f, -180.f, 180.f, "%.0f deg/s");
			}
		}
		ImGui::End();

		ImGui::SetNextWindowBgAlpha(0.6f);
		ImGui::SetNextWindowPos(ImVec2((float)vpPos.x + 8.f, (float)vpPos.y + 40.f), ImGuiCond_Always);
		if (ImGui::Begin(statsId, nullptr, overlayFlags))
		{
			ImGui::Text("Tris: %d   Maps: %d/4", triCount, mapCount);
		}
		ImGui::End();
	}

	parameters.viewport->EndDraw();
	GraphicsEngine::GetInstance()->EndFrame();
}

void DefaultMaterialEditorGraphics::DrawPreviewSettings()
{
	{
	InspectorSection lighting("Preview Lighting", true, "A fast, local lighting rig used only by the material preview.");
	if (lighting.IsOpen() && BeginInspectorPropertyTable("MaterialPreviewLighting"))
	{
		InspectorPropertyLabel("Actions"); InspectorPropertyValue();
		if (ImGui::SmallButton("Reset Lighting"))
		{
			myLightYaw = 45.f; myLightPitch = -40.f; myLightColor = { 1.f, .98f, .95f }; myLightIntensity = 2.2f;
			myAmbientColor = { .25f, .30f, .38f }; myAmbientIntensity = 1.f;
		}
		InspectorPropertyLabel("Key Light Yaw"); InspectorPropertyValue(); ImGui::DragFloat("##ly", &myLightYaw);
		InspectorPropertyLabel("Key Light Pitch"); InspectorPropertyValue(); ImGui::DragFloat("##lp", &myLightPitch);
		InspectorPropertyLabel("Key Light Colour"); InspectorPropertyValue(); ImGui::ColorEdit3("##lc", &myLightColor.r, ImGuiColorEditFlags_Float);
		InspectorPropertyLabel("Key Light Intensity"); InspectorPropertyValue(); ImGui::DragFloat("##li", &myLightIntensity, 0.02f, 0.f, 50.f);
		InspectorPropertyLabel("Ambient Colour"); InspectorPropertyValue(); ImGui::ColorEdit3("##ac", &myAmbientColor.r, ImGuiColorEditFlags_Float);
		InspectorPropertyLabel("Ambient Intensity"); InspectorPropertyValue(); ImGui::DragFloat("##ai", &myAmbientIntensity, 0.02f, 0.f, 10.f);
		EndInspectorPropertyTable();
	}
	}

	{
	InspectorSection environment("Environment", true, "Pick a DDS cubemap, or drop one here, or use uniform ambient light.");
	if (environment.IsOpen() && BeginInspectorPropertyTable("MaterialPreviewEnvironment"))
	{
		InspectorPropertyLabel("Cube Map"); InspectorPropertyValue();
		{
			StringId cubeMap = myCubeMapPath;
			if (PropertyEditor::AssetField("##EnvironmentCubeMap", cubeMap, { ".dds" }, "None (Cubemap)"))
			{
				myCubeMapPath = cubeMap;
				if (cubeMap.IsEmpty())
				{
					myAmbient.type = AmbientLightType::Uniform;
					myAmbient.cubemap = nullptr;
				}
				else
				{
					myAmbient.type = AmbientLightType::Custom;
					myAmbient.cubemap = GraphicsEngine::GetInstance()->GetTextureManager().GetTexture(myCubeMapPath.GetString());
				}
			}
		}
		EndInspectorPropertyTable();
	}
	}
}

bool DefaultMaterialEditorGraphics::BakeMaterialGraph(const MaterialGraphBakeRequest& request)
{
	return Ag::BakeMaterialGraphImpl(request);
}

DefaultEditorGraphics::DefaultEditorGraphics()
{
	RegisterGetModelMeshInfoFunction([](StringId modelPath, SceneModelMeshInfo& outMeshInfo) -> bool
	{
		if (modelPath.IsEmpty())
			return false;

		if (!GraphicsEngine::GetInstance())
			GraphicsEngine::Start();

		// The property inspector asks for mesh names as soon as a model field is
		// focused. Never synchronously parse a large FBX (such as Sponza) from an
		// ImGui callback: it can stall or crash the editor's UI/render frame.
		ModelFactory& factory = ModelFactory::GetInstance();
		factory.PumpAsyncImports();
		std::shared_ptr<Model> fbxModel = factory.GetLoadedModel(modelPath);
		if (!fbxModel)
		{
			factory.RequestAsyncImport(modelPath);
			return false;
		}
		outMeshInfo.meshCount = (int)fbxModel->GetMeshCount();
		if (outMeshInfo.meshCount > MAX_MESHES_PER_MODEL)
			outMeshInfo.meshCount = MAX_MESHES_PER_MODEL;

		for (int i = 0; i < outMeshInfo.meshCount; i++)
		{
			outMeshInfo.meshNames[i] = fbxModel->GetMeshName(i);
			outMeshInfo.materialNames[i] = fbxModel->GetMaterialName(i);
		}
		if (outMeshInfo.meshCount > 0)
		{
			outMeshInfo.bounds = fbxModel->GetMeshData(0).bounds;
		}
		return true;
	});

	RegisterGetModelCollisionInfoFunction([](StringId modelPath, size_t maxTriangles, SceneModelCollisionInfo& outInfo) -> bool
	{
		if (modelPath.IsEmpty() || !GraphicsEngine::GetInstance())
			return false;

		// Same rule as the mesh info above: never import synchronously from a UI callback.
		ModelFactory& factory = ModelFactory::GetInstance();
		std::shared_ptr<Model> model = factory.GetLoadedModel(modelPath);
		if (!model)
			return false;

		outInfo.bounds = model->GetBounds();
		CollisionGeometry geometry;
		if (factory.GetCollisionGeometry(modelPath, geometry) && geometry.indices.size() / 3 <= maxTriangles)
		{
			outInfo.hasGeometry = true;
			outInfo.positions = std::move(geometry.positions);
			outInfo.indices = std::move(geometry.indices);
		}
		return true;
	});
}

std::unique_ptr<ObjectDefinitionEditorGraphicsBase> DefaultEditorGraphics::CreateObjectDefinitionGraphicsInterface() const
{
	return std::make_unique<DefaultObjectDefinitionEditorGraphics>();
}

std::unique_ptr<SceneEditorGraphicsBase> DefaultEditorGraphics::CreateSceneGraphicsInterface() const
{
	return std::make_unique<DefaultSceneEditorGraphics>();
}

std::unique_ptr<AnimationClipEditorGraphicsBase> DefaultEditorGraphics::CreateAnimationClipGraphicsInterface() const
{
	return std::make_unique<DefaultAnimationClipEditorGraphics>();
}

std::unique_ptr<MaterialEditorGraphicsBase> DefaultEditorGraphics::CreateMaterialGraphicsInterface() const
{
	return std::make_unique<DefaultMaterialEditorGraphics>();
}

ImTextureID DefaultEditorGraphics::GetTextureID(std::string_view aTexturePath) const
{
	if (aTexturePath.empty())
		return 0;

	if (!GraphicsEngine::GetInstance())
		GraphicsEngine::Start();

	Ag::TextureManager& tm = GraphicsEngine::GetInstance()->GetTextureManager();
	std::string pathStr(aTexturePath);
	const Texture* img = tm.GetTexture(pathStr.c_str(), TextureSrgbMode::ForceNoSrgbFormat);
	if (!img)
		return 0;

	// NOT GetShaderResourceView(): the DX11-only raw pointer, always null on
	// DX12 -- and worse than a null-texture no-op there, since DX12's
	// ImTextureID convention is a GPU_DESCRIPTOR_HANDLE value, not a view
	// pointer at all; reinterpret-casting a null/dangling ID3D11 pointer as
	// one feeds ImGui's DX12 backend a bogus GPU address for every asset-
	// browser thumbnail/material-preview icon (found 2026-09-12: this is very
	// likely what actually crashed GameEditor a frame after opening any scene
	// with visible thumbnails -- same class of bug as Viewport.cpp's main
	// viewport image and SceneUtil.cpp's selection-outline bind, both also
	// fixed this session). Goes through the same backend-agnostic bridge.
	return reinterpret_cast<ImTextureID>(DX11::Rhi()->ImGuiTextureId(img->GetSrv()));
}

void DefaultEditorGraphics::DrawLines(const Color* someColors, const Vector3f* someFromPositions, const Vector3f* someToPositions, unsigned int aCount) const
{
	if (aCount == 0 || !someColors || !someFromPositions || !someToPositions)
		return;

	if (!GraphicsEngine::GetInstance())
		GraphicsEngine::Start();

	constexpr unsigned int MaxLinesPerBatch = 1000;
	for (unsigned int offset = 0; offset < aCount; offset += MaxLinesPerBatch)
	{
		unsigned int count = std::min(MaxLinesPerBatch, aCount - offset);

		Ag::LineMultiPrimitive lines{
			.colors = someColors + offset,
			.fromPositions = someFromPositions + offset,
			.toPositions = someToPositions + offset,
			.count = count
		};
		GraphicsEngine::GetInstance()->GetLineDrawer().Draw(lines);
	}
}
