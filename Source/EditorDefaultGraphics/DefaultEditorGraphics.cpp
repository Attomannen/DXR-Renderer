#include "stdafx.h"
#include "DefaultEditorGraphics.h"

#include <imgui.h>
#include <tge/editor/p4/p4.h>
#include <tge/editor/Scene/ActiveScene.h>
#include <tge/editor/Scene/SceneSelection.h>
#include <SceneUtil.h>
#include <tge/editor/Tools/Viewport/Viewport.h>

#include "tge/animation/Animation.h"
#include "tge/animation/AnimationClip.h"
#include "tge/animation/AnimationPlayer.h"
#include "tge/graphics/GraphicsEngine.h"
#include "tge/graphics/GraphicsStateStack.h"
#include "tge/drawers/LineDrawer.h"
#include "tge/drawers/ModelDrawer.h"
#include "tge/model/AnimatedModelInstance.h"
#include "tge/model/ModelFactory.h"
#include "tge/primitives/LinePrimitive.h"
#include "tge/render/RenderCommon.h"
#include "tge/settings/settings.h"
#include "tge/texture/TextureManager.h"
#include "tge/texture/texture.h"
#include "tge/scene/ScenePropertyTypes.h"
#include "tge/shaders/ModelShader.h"
#include <tge/editor/ObjectDefinition/ObjectDefinitionDocument.h>
#include <tge/shaders/SpriteShader.h>
#include <tge/imgui/ImGuiPropertyEditor.h>
#include <tge/editor/Editor.h>
#include <tge/editor/Material/MaterialAsset.h>
#include <tge/graphics/DX11.h>
#include <tge/model/ModelInstance.h>
#include <tge/rhi/ConstantBuffer.h>

using namespace Tga;

namespace Tga
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
		SceneCache myCache;
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
		DefaultMaterialEditorGraphics()
		{
			if (!GraphicsEngine::GetInstance())
				GraphicsEngine::Start();

			myConstShader.Init("shaders/PbrModelShaderVs", "shaders/PbrConstModelShaderPS");

			myMatCb.Create(*DX11::Rhi(), 48, rhi::ShaderStage::Pixel, 11, "MaterialEditorMatCb");   // 3 x float4
		}

		void Draw(const MaterialEditorDrawParameters& parameters) override;
		void DrawPreviewSettings() override;

	private:
		ModelShader myConstShader;
		rhi::ConstantBuffer myMatCb;
		std::string myPreviewMeshLoaded;
		ModelInstance myInstance;

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
	// clearing out caches every frame to support updates to assets while the editor is running
	myCache.ClearCache();
	Camera& renderCamera = parameters.viewport->GetCamera();
	Frustum frustum = CalculateFrustum(renderCamera);

	{
		auto& graphicsStateStack = GraphicsEngine::GetInstance()->GetGraphicsStateStack();

		graphicsStateStack.SetCamera(renderCamera);
		graphicsStateStack.SetBlendState(Tga::BlendState::Disabled);
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

				SetObjectAndSelectionId(1 + propertyIndex, prop.name == parameters.selectedProperty ? 1 + propertyIndex : 0, P4::FileInfo());

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
void Tga::DefaultObjectDefinitionEditorGraphics::DrawVisualPreviewSettings()
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
			if (ImGui::Button("Set From AssetBrowser"))
			{
				StringId newValue = Editor::GetEditor()->GetAssetBrowser().GetSelectedAsset();
				std::string stringWithExtension = newValue.GetString();
				std::string::size_type pos = stringWithExtension.find(".hlsl");
				if (pos != std::string::npos)
				{
					std::string withoutPath = stringWithExtension.substr(0, pos);
					myPreviewSettings.previewPixelShaderPath = StringRegistry::RegisterOrGetString(withoutPath);
					UpdatePreviewShaders();
				}
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
			ImGui::Text(myPreviewSettings.cubeMapPath.GetString());

			if (ImGui::Button("Set From AssetBrowser"))
			{
				StringId newValue = Editor::GetEditor()->GetAssetBrowser().GetSelectedAsset();
				std::string stringWithExtension = newValue.GetString();
				std::string::size_type pos = stringWithExtension.find(".dds");
				if (pos != std::string::npos)
				{
					myPreviewSettings.cubeMapPath = StringRegistry::RegisterOrGetString(stringWithExtension);

					myPreviewSettings.ambientLight.type = AmbientLightType::Custom;
					myPreviewSettings.ambientLight.cubemap = GraphicsEngine::GetInstance()->GetTextureManager().GetTexture(myPreviewSettings.cubeMapPath.GetString());
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

	// clearing out caches every frame to support updates to assets while the editor is running
	myCache.ClearCache();

	const Camera& renderCamera = parameters.viewport->GetCamera();
	Frustum frustum = CalculateFrustum(renderCamera);

	{
		parameters.viewport->BeginDraw();
		auto& graphicsStateStack = GraphicsEngine::GetInstance()->GetGraphicsStateStack();

		graphicsStateStack.SetCamera(renderCamera);
		graphicsStateStack.SetBlendState(Tga::BlendState::Disabled);

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
				auto& info = P4::GetFileInfo(parameters.scene->GetObjectFilePath(p.first).GetString());

				SetObjectAndSelectionId(
					p.first,
					SceneSelection::GetActiveSceneSelection()->Contains(p.first) ? p.first : 0,
					info
				);

				DrawSceneObject(*p.second, drawParameters);
			}
		}

		{
			// And one pass to render to editor render-target
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
				drawParameters.boundsColor = Tga::Vector4f(0.f, 1.f, 0.f, 1.f);
				if (ImGui::GetIO().KeyShift)
				{
					drawParameters.boundsColor = SceneSelection::GetActiveSceneSelection()->Contains(p.first) ? Tga::Vector4f(0.f, 0.f, 0.f, 0.0f) : Tga::Vector4f(0.f, 1.f, 0.f, 1.f);
				}

				DrawSceneObject(*p.second, drawParameters);
			}
		}
	}
	DrawOutlines(*parameters.viewport);
	parameters.viewport->EndDraw();

	GraphicsEngine::GetInstance()->EndFrame();

}

void DefaultAnimationClipEditorGraphics::Draw(const AnimationClipDrawParameters& parameters)
{
	Tga::LineDrawer& lineDrawer = GraphicsEngine::GetInstance()->GetLineDrawer();
	myCache.ClearCache();

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
	graphicsStateStack.SetBlendState(Tga::BlendState::Disabled);

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

					Tga::LineMultiPrimitive lines{
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
	stack.SetBlendState(Tga::BlendState::Disabled);

	parameters.viewport->BeginDraw();
	parameters.viewport->SetupColorPass();

	myAmbient.color = myAmbientIntensity * myAmbientColor;
	stack.SetAmbientLight(myAmbient);
	stack.SetDirectionalLight(DirectionalLight{
		Matrix4x4f::CreateFromRollPitchYaw({ myLightPitch, myLightYaw, 0.f }),
		myLightIntensity * myLightColor, 0.f });

	// Centre the unit primitive (~100 units) in front of the camera focus.
	Matrix4x4f xf = Matrix4x4f::CreateIdentityMatrix();
	myInstance.SetTransform(xf);

	if (myInstance.IsValid())
	{
		ModelDrawer& md = GraphicsEngine::GetInstance()->GetModelDrawer();
		if (mat.AnyMaps())
		{
			auto& texMgr = GraphicsEngine::GetInstance()->GetTextureManager();
			const int meshCount = myInstance.GetModel() ? (int)myInstance.GetModel()->GetMeshCount() : 0;
			for (int m = 0; m < meshCount; ++m)
				for (int j = 0; j < 4; ++j)
				{
					if (mat.maps[j].empty()) continue;
					const TextureSrgbMode srgb = (j == 0) ? TextureSrgbMode::ForceSrgbFormat : TextureSrgbMode::ForceNoSrgbFormat;
					if (Texture* t = texMgr.GetTexture(mat.maps[j].c_str(), srgb))
						myInstance.SetTexture(m, j, t);
				}
			md.Draw(myInstance, md.GetPbrShader());
		}
		else
		{
			// Pack the 48-byte b11 material cbuffer and bind it for the const shader.
			if (myMatCb.IsValid())
			{
				float d[12];
				d[0] = mat.baseColor[0]; d[1] = mat.baseColor[1]; d[2] = mat.baseColor[2]; d[3] = 1.f;
				d[4] = mat.roughness;    d[5] = mat.metalness;    d[6] = mat.ao;           d[7] = mat.emissiveStrength;
				d[8] = mat.emissiveColor[0]; d[9] = mat.emissiveColor[1]; d[10] = mat.emissiveColor[2]; d[11] = 1.f;
				rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
				myMatCb.Update(ctx, d, sizeof(d));
				myMatCb.Bind(ctx);
			}
			md.Draw(myInstance, myConstShader);
		}
	}

	parameters.viewport->EndDraw();
	GraphicsEngine::GetInstance()->EndFrame();
}

void DefaultMaterialEditorGraphics::DrawPreviewSettings()
{
	if (PropertyEditor::PropertyHeader("Preview Lighting") && PropertyEditor::BeginPropertyTable())
	{
		PropertyEditor::PropertyLabel(); ImGui::Text("Key Light Yaw");
		PropertyEditor::PropertyValue(); ImGui::DragFloat("##ly", &myLightYaw);
		PropertyEditor::PropertyLabel(); ImGui::Text("Key Light Pitch");
		PropertyEditor::PropertyValue(); ImGui::DragFloat("##lp", &myLightPitch);
		PropertyEditor::PropertyLabel(); ImGui::Text("Key Light Colour");
		PropertyEditor::PropertyValue(); ImGui::ColorEdit3("##lc", &myLightColor.r, ImGuiColorEditFlags_Float);
		PropertyEditor::PropertyLabel(); ImGui::Text("Key Light Intensity");
		PropertyEditor::PropertyValue(); ImGui::DragFloat("##li", &myLightIntensity, 0.02f, 0.f, 50.f);
		PropertyEditor::PropertyLabel(); ImGui::Text("Ambient Colour");
		PropertyEditor::PropertyValue(); ImGui::ColorEdit3("##ac", &myAmbientColor.r, ImGuiColorEditFlags_Float);
		PropertyEditor::PropertyLabel(); ImGui::Text("Ambient Intensity");
		PropertyEditor::PropertyValue(); ImGui::DragFloat("##ai", &myAmbientIntensity, 0.02f, 0.f, 10.f);
		PropertyEditor::EndPropertyTable();
	}

	if (PropertyEditor::PropertyHeader("Ambient Cube Map") && PropertyEditor::BeginPropertyTable())
	{
		PropertyEditor::PropertyLabel(); ImGui::Text("Cube Map");
		PropertyEditor::PropertyValue(); ImGui::Text(myCubeMapPath.IsEmpty() ? "(uniform)" : myCubeMapPath.GetString());
		if (ImGui::Button("Set From AssetBrowser"))
		{
			std::string sel = Editor::GetEditor()->GetAssetBrowser().GetSelectedAsset().GetString();
			if (sel.find(".dds") != std::string::npos)
			{
				myCubeMapPath = StringRegistry::RegisterOrGetString(sel);
				myAmbient.type = AmbientLightType::Custom;
				myAmbient.cubemap = GraphicsEngine::GetInstance()->GetTextureManager().GetTexture(myCubeMapPath.GetString());
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
		{
			myCubeMapPath = {};
			myAmbient.type = AmbientLightType::Uniform;
			myAmbient.cubemap = nullptr;
		}
		PropertyEditor::EndPropertyTable();
	}
}

DefaultEditorGraphics::DefaultEditorGraphics()
{
	RegisterGetModelMeshInfoFunction([](StringId modelPath, SceneModelMeshInfo& outMeshInfo) -> bool
	{
		if (modelPath.IsEmpty())
			return false;

		if (!GraphicsEngine::GetInstance())
			GraphicsEngine::Start();

		std::shared_ptr<Model> fbxModel = ModelFactory::GetInstance().GetModel(modelPath.GetString());
		if (!fbxModel)
			return false;

		outMeshInfo.meshCount = (int)fbxModel->GetMeshCount();
		if (outMeshInfo.meshCount > MAX_MESHES_PER_MODEL)
			outMeshInfo.meshCount = MAX_MESHES_PER_MODEL;

		for (int i = 0; i < outMeshInfo.meshCount; i++)
		{
			outMeshInfo.meshNames[i] = fbxModel->GetMeshName(i);
		}
		if (outMeshInfo.meshCount > 0)
		{
			outMeshInfo.bounds = fbxModel->GetMeshData(0).bounds;
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

	Tga::TextureManager& tm = GraphicsEngine::GetInstance()->GetTextureManager();
	std::string pathStr(aTexturePath);
	const Texture* img = tm.GetTexture(pathStr.c_str(), TextureSrgbMode::ForceNoSrgbFormat);
	if (!img)
		return 0;

	return reinterpret_cast<ImTextureID>(img->GetShaderResourceView());
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

		Tga::LineMultiPrimitive lines{
			.colors = someColors + offset,
			.fromPositions = someFromPositions + offset,
			.toPositions = someToPositions + offset,
			.count = count
		};
		GraphicsEngine::GetInstance()->GetLineDrawer().Draw(lines);
	}
}