#include <stdafx.h>

#include "ScenePropertyTypes.h"
#include <tge/script/JsonData.h>
#include <tge/imgui/ImGuiPropertyEditor.h>
#include <tge/util/StringCast.h>
#include <imgui/imgui.h>

using namespace Tga;

StringId(*locAssetBrowserGetSelectionFunction)();
GetModelMeshInfoFunction locGetModelMeshInfoFunction = nullptr;

namespace Tga
{
	void RegisterAssetBrowserGetSelectionFunction(StringId(*aGetFunction)())
	{
		locAssetBrowserGetSelectionFunction = aGetFunction;
	};

	void RegisterGetModelMeshInfoFunction(GetModelMeshInfoFunction aGetFunction)
	{
		locGetModelMeshInfoFunction = aGetFunction;
	}

	bool GetModelMeshInfo(StringId modelPath, SceneModelMeshInfo& outMeshInfo)
	{
		if (locGetModelMeshInfoFunction)
		{
			return locGetModelMeshInfoFunction(modelPath, outMeshInfo);
		}
		return false;
	}

	// Workaround to ensure this object file is included
	// called from Engine.cpp
	void EnsureScenePropertiesAreLoaded() {}

	template<>
	void LoadFromJson<CopyOnWriteWrapper<SceneModel>>(CopyOnWriteWrapper<SceneModel>& value, const JsonData& jsonData)
	{
		using namespace nlohmann;

		value = CopyOnWriteWrapper<SceneModel>::Create();
		SceneModel& model = value.Edit();

		model.path = StringRegistry::RegisterOrGetString(jsonData.json.value("path", ""));

		if (jsonData.json.contains("materials"))
		{
			int i = 0;
			for (auto& material : jsonData.json["materials"])
			{
				model.materials[i] = StringRegistry::RegisterOrGetString(material.get<std::string>());
				i++;
				if (i == MAX_MESHES_PER_MODEL)
					break;
			}
		}
	}

	template<>
	void WriteToJson<CopyOnWriteWrapper<SceneModel>>(const CopyOnWriteWrapper<SceneModel>& value, JsonData& jsonData)
	{
		using namespace nlohmann;

		const SceneModel& model = value.Get();

		jsonData.json["path"] = model.path.GetString();

		// Only serialize up to the last assigned slot. MAX_MESHES_PER_MODEL is a
		// fixed ceiling shared by every model (256, occasionally raised for dense
		// FBXs), not the mesh count of any one instance -- writing every unused
		// slot as an empty string bloated every scene file by one JSON string per
		// unused slot, per SceneModel property, per object. LoadFromJson already
		// reads back however many entries are present, so this is a pure size fix
		// with no format/version change.
		int usedCount = 0;
		for (int i = 0; i < MAX_MESHES_PER_MODEL; i++)
			if (!model.materials[i].IsEmpty()) usedCount = i + 1;

		json materials;
		for (int i = 0; i < usedCount; i++)
			materials.push_back(model.materials[i].GetString());
		jsonData.json["materials"] = materials;
	}

	template<>
	bool ShowImGuiEditor<CopyOnWriteWrapper<SceneModel>>(CopyOnWriteWrapper<SceneModel>& value, const char* name, const char* description)
	{
		const SceneModel& model = value.Get();

		// Too complex to edit outside property editor
		if (name == nullptr)
		{
			ImGui::Text(model.path.IsEmpty() ? "None" : model.path.GetString());
			return false;
		}

		SceneModel* editableModel = nullptr;

		auto makeEditable = [&]()
		{
			if (!editableModel)
				editableModel = &value.Edit();
		};

		bool hasBeenEdited = false;

		PropertyEditor::PropertyLabel();
		ImGui::Text(name);
		if (description && *description != 0)
		{
			PropertyEditor::HelpMarker(description);
		}
		PropertyEditor::PropertyValue();

		PropertyEditor::PropertyLabel(true);
		ImGui::Indent();
		ImGui::Text("Path");
		ImGui::Unindent();
		PropertyEditor::PropertyValue(true);

		ImGui::Text(model.path.IsEmpty() ? "None" : model.path.GetString());
		if(ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".fbx")) 
			{
				makeEditable();

				const char* dropped = static_cast<const char*>(payload->Data);
				editableModel->path = StringRegistry::RegisterOrGetString(dropped);
				hasBeenEdited = true;
			}
			ImGui::EndDragDropTarget();
		}

		PropertyEditor::PropertyLabel(true);
		PropertyEditor::PropertyValue(true);

		if (locAssetBrowserGetSelectionFunction)
		{
			if (ImGui::Button("Set From AssetBrowser"))
			{
				// todo: validation
				StringId newValue = locAssetBrowserGetSelectionFunction();
				if (newValue != model.path)
				{
					makeEditable();

					editableModel->path = newValue;
					hasBeenEdited = true;
				}
			}
			ImGui::SameLine();
		}

		if (ImGui::Button("Clear"))
		{
			if (!model.path.IsEmpty())
			{
				makeEditable();

				editableModel->path = {};
				hasBeenEdited = true;
			}
		}

		if (!model.path.IsEmpty() && locGetModelMeshInfoFunction)
		{
			SceneModelMeshInfo meshInfo;
			if (locGetModelMeshInfoFunction(model.path, meshInfo))
			{
				auto showMaterialEditing = [&](int meshIndex)
				{
					PropertyEditor::PropertyLabel(true);
					ImGui::Text("Material");
					PropertyEditor::PropertyValue(true);

					ImGui::Text(model.materials[meshIndex].IsEmpty() ? "None (.tgmat)" : model.materials[meshIndex].GetString());
					if (ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".tgmat"))
						{
							makeEditable();

							const char* dropped = static_cast<const char*>(payload->Data);
							editableModel->materials[meshIndex] = StringRegistry::RegisterOrGetString(dropped);
							hasBeenEdited = true;
						}
						ImGui::EndDragDropTarget();
					}

					PropertyEditor::PropertyLabel(true);
					PropertyEditor::PropertyValue(true);

					if (locAssetBrowserGetSelectionFunction)
					{
						if (ImGui::Button("Set From AssetBrowser"))
						{
							// todo: validation
							StringId newValue = locAssetBrowserGetSelectionFunction();
							if (newValue != model.materials[meshIndex] && newValue.GetString()[0] != '\0' && std::filesystem::path(newValue.GetString()).extension() == ".tgmat")
							{
								makeEditable();

								editableModel->materials[meshIndex] = newValue;
								hasBeenEdited = true;
							}
						}
						ImGui::SameLine();
					}

					if (ImGui::Button("Clear"))
					{
						if (!model.materials[meshIndex].IsEmpty())
						{
							makeEditable();

							editableModel->materials[meshIndex] = {};
							hasBeenEdited = true;
						}
					}

				};

				PropertyEditor::PropertyLabel(true);
				bool showTree = ImGui::TreeNode("Materials");

				PropertyEditor::PropertyValue(true);
				
				if (showTree)
				{
					int meshCount = meshInfo.meshCount;
					if (meshCount > MAX_MESHES_PER_MODEL)
						meshCount = MAX_MESHES_PER_MODEL;

					for (int i = 0; i < meshCount; i++)
					{
						ImGui::PushID(i);

						bool display = true;
						if (meshCount > 1)
						{
							PropertyEditor::PropertyLabel(true);
							display = ImGui::TreeNode(meshInfo.meshNames[i].GetString());
							PropertyEditor::PropertyValue(true);
						}

						if (display)
						{
							showMaterialEditing(i);

							if (meshCount > 1)
								ImGui::TreePop();
						}

						ImGui::PopID();

					}

					ImGui::TreePop();

				}
			}
		}

		return hasBeenEdited;
	}

	IMPLEMENT_PROPERTY_TYPE(CopyOnWriteWrapper<SceneModel>, "Model")

	template<>
	void LoadFromJson<CopyOnWriteWrapper<SceneSprite>>(CopyOnWriteWrapper<SceneSprite>& value, const JsonData& jsonData)
	{
		value = CopyOnWriteWrapper<SceneSprite>::Create();
		SceneSprite& sprite = value.Edit();

		if (jsonData.json.contains("texturePath"))
			sprite.textures[0] = StringRegistry::RegisterOrGetString(jsonData.json.value("texturePath", ""));

		if (jsonData.json.contains("textures"))
		{
			int i = 0;
			for (auto& texture : jsonData.json["textures"])
			{
				sprite.textures[i] = StringRegistry::RegisterOrGetString(texture.get<std::string>());

				i++;
				if (i == 4)
					break;
			}
		}

		if (jsonData.json.contains("pivot"))
		{
			const nlohmann::json& pivot = jsonData.json["pivot"];
			sprite.pivot.x = pivot[0];
			sprite.pivot.y = pivot[1];
		}
		if (jsonData.json.contains("size"))
		{
			const nlohmann::json& size = jsonData.json["size"];
			sprite.size.x = size[0];
			sprite.size.y = size[1];
		}
	}

	template<>
	void WriteToJson<CopyOnWriteWrapper<SceneSprite>>(const CopyOnWriteWrapper<SceneSprite>& value, JsonData& jsonData)
	{
		using namespace nlohmann;

		const SceneSprite& sprite = value.Get();

		json textures;
		for (int i = 0; i < 4; i++)
		{
			textures.push_back(sprite.textures[i].GetString());
		}
		jsonData.json["textures"] = textures;
		jsonData.json["pivot"] = { sprite.pivot.x, sprite.pivot.y };
		jsonData.json["size"] = { sprite.size.x, sprite.size.y };
	}

	template<>
	bool ShowImGuiEditor<CopyOnWriteWrapper<SceneSprite>>(CopyOnWriteWrapper<SceneSprite>& value, const char* name, const char* description)
	{
		const SceneSprite& sprite = value.Get();

		// Too complex to edit outside property editor
		if (name == nullptr)
		{
			ImGui::Text(sprite.textures[0].IsEmpty() ? "None" : sprite.textures[0].GetString());
			return false;
		}

		SceneSprite* editableSprite = nullptr;

		auto makeEditable = [&]()
		{
			if (!editableSprite)
				editableSprite = &value.Edit();
		};

		bool hasBeenEdited = false;

		PropertyEditor::PropertyLabel();
		ImGui::Text(name);
		if (description && *description != 0)
		{
			PropertyEditor::HelpMarker(description);
		}
		PropertyEditor::PropertyValue();


		auto showTextureEditing = [&](int textureIndex, const char* label)
			{
				ImGui::PushID(textureIndex);

				PropertyEditor::PropertyLabel(true);
				ImGui::Text(label);
				PropertyEditor::PropertyValue(true);

				ImGui::Text(sprite.textures[textureIndex].IsEmpty() ? "None" : sprite.textures[textureIndex].GetString());
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".dds"))
					{
						makeEditable();

						const char* dropped = static_cast<const char*>(payload->Data);
						editableSprite->textures[textureIndex] = StringRegistry::RegisterOrGetString(dropped);
						hasBeenEdited = true;
					}
					ImGui::EndDragDropTarget();
				}

				PropertyEditor::PropertyLabel(true);
				PropertyEditor::PropertyValue(true);

				if (locAssetBrowserGetSelectionFunction)
				{
					if (ImGui::Button("Set From AssetBrowser"))
					{
						// todo: validation
						StringId newValue = locAssetBrowserGetSelectionFunction();
						if (newValue != sprite.textures[textureIndex])
						{
							makeEditable();

							editableSprite->textures[textureIndex] = newValue;
							hasBeenEdited = true;
						}
					}
					ImGui::SameLine();
				}

				if (ImGui::Button("Clear"))
				{
					if (!sprite.textures[textureIndex].IsEmpty())
					{
						makeEditable();

						editableSprite->textures[textureIndex] = {};
						hasBeenEdited = true;
					}
				}

				ImGui::PopID();
			};


		showTextureEditing(0, "[0] Color Texture");
		showTextureEditing(1, "[1] Normal Texture");
		showTextureEditing(2, "[2] Material Texture");
		showTextureEditing(3, "[3] Effects Texture");
		PropertyEditor::PropertyLabel(true);
		PropertyEditor::PropertyValue(true);

		PropertyEditor::PropertyLabel(true);
		ImGui::Indent();
		ImGui::Text("Size");
		ImGui::Unindent();
		PropertyEditor::PropertyValue(true);
		Vector2f newSize = sprite.size;
		if (ImGui::DragFloat2("##size", &newSize.x))
		{
			if (newSize != sprite.size)
			{
				makeEditable();

				editableSprite->size = newSize;
				hasBeenEdited = true;
			}
		}

		PropertyEditor::PropertyLabel(true);
		ImGui::Indent();
		ImGui::Text("Pivot");
		ImGui::Unindent();
		PropertyEditor::PropertyValue(true);
		Vector2f newPivot = sprite.pivot;
		if (ImGui::DragFloat2("##pivot", &newPivot.x))
		{
			if (newPivot != sprite.pivot)
			{
				makeEditable();

				editableSprite->pivot = newPivot;
				hasBeenEdited = true;
			}
		}

		return hasBeenEdited;
	}

	IMPLEMENT_PROPERTY_TYPE(CopyOnWriteWrapper<SceneSprite>, "Sprite")

	template<>
	void LoadFromJson<CopyOnWriteWrapper<SceneReference>>(CopyOnWriteWrapper<SceneReference>& value, const JsonData& jsonData)
	{
		value = CopyOnWriteWrapper<SceneReference>::Create();
		SceneReference& reference = value.Edit();

		reference.path = StringRegistry::RegisterOrGetString(jsonData.json.value("scene_path", ""));
	}

	template<>
	void WriteToJson<CopyOnWriteWrapper<SceneReference>>(const CopyOnWriteWrapper<SceneReference>& value, JsonData& jsonData)
	{
		using namespace nlohmann;

		const SceneReference& sceneReference = value.Get();

		jsonData.json["scene_path"] = sceneReference.path.GetString();
	}

	template<>
	bool ShowImGuiEditor<CopyOnWriteWrapper<SceneReference>>(CopyOnWriteWrapper<SceneReference>& value, const char* name, const char* description)
	{
		const SceneReference& sceneReference = value.Get();

		// Too complex to edit outside property editor
		if (name == nullptr)
		{
			ImGui::Text(sceneReference.path.IsEmpty() ? "None" : sceneReference.path.GetString());
			return false;
		}

		SceneReference* editableSceneReference = nullptr;

		auto makeEditable = [&]()
			{
				if (!editableSceneReference)
					editableSceneReference = &value.Edit();
			};

		bool hasBeenEdited = false;

		PropertyEditor::PropertyLabel();
		ImGui::Text(name);
		if (description && *description != 0)
		{
			PropertyEditor::HelpMarker(description);
		}
		PropertyEditor::PropertyValue();


		PropertyEditor::PropertyLabel(true);
		ImGui::Indent();
		ImGui::Text("Path");
		ImGui::Unindent();
		PropertyEditor::PropertyValue(true);

		ImGui::Text(sceneReference.path.IsEmpty() ? "None" : sceneReference.path.GetString());
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".tgs"))
			{
				makeEditable();

				const char* dropped = static_cast<const char*>(payload->Data);
				editableSceneReference->path = StringRegistry::RegisterOrGetString(dropped);
				hasBeenEdited = true;
			}
			ImGui::EndDragDropTarget();
		}

		PropertyEditor::PropertyLabel(true);
		PropertyEditor::PropertyValue(true);

		if (locAssetBrowserGetSelectionFunction)
		{
			if (ImGui::Button("Set From AssetBrowser"))
			{
				// todo: validation
				StringId newValue = locAssetBrowserGetSelectionFunction();
				if (newValue != sceneReference.path)
				{
					makeEditable();

					editableSceneReference->path = newValue;
					hasBeenEdited = true;
				}
			}
			ImGui::SameLine();
		}

		if (ImGui::Button("Clear"))
		{
			if (!sceneReference.path.IsEmpty())
			{
				makeEditable();

				editableSceneReference->path = {};
				hasBeenEdited = true;
			}
		}

		return hasBeenEdited;
	}

	IMPLEMENT_PROPERTY_TYPE(CopyOnWriteWrapper<SceneReference>, "Scene")

	template<>
	void LoadFromJson<CopyOnWriteWrapper<AnimationClipReference>>(CopyOnWriteWrapper<AnimationClipReference>& value, const JsonData& jsonData)
	{
		value = CopyOnWriteWrapper<AnimationClipReference>::Create();
		AnimationClipReference& reference = value.Edit();

		reference.path = StringRegistry::RegisterOrGetString(jsonData.json.value("clip_path", ""));
	}

	template<>
	void WriteToJson<CopyOnWriteWrapper<AnimationClipReference>>(const CopyOnWriteWrapper<AnimationClipReference>& value, JsonData& jsonData)
	{
		using namespace nlohmann;

		const AnimationClipReference& sceneReference = value.Get();

		jsonData.json["clip_path"] = sceneReference.path.GetString();
	}

	template<>
	bool ShowImGuiEditor<CopyOnWriteWrapper<AnimationClipReference>>(CopyOnWriteWrapper<AnimationClipReference>& value, const char* name, const char* description)
	{
		const AnimationClipReference& clipReference = value.Get();

		// Too complex to edit outside property editor
		if (name == nullptr)
		{
			ImGui::Text(clipReference.path.IsEmpty() ? "None" : clipReference.path.GetString());
			return false;
		}

		AnimationClipReference* editableClipReference = nullptr;

		auto makeEditable = [&]()
			{
				if (!editableClipReference)
					editableClipReference = &value.Edit();
			};

		bool hasBeenEdited = false;

		PropertyEditor::PropertyLabel();
		ImGui::Text(name);
		if (description && *description != 0)
		{
			PropertyEditor::HelpMarker(description);
		}
		PropertyEditor::PropertyValue();


		PropertyEditor::PropertyLabel(true);
		ImGui::Indent();
		ImGui::Text("Path");
		ImGui::Unindent();
		PropertyEditor::PropertyValue(true);

		ImGui::Text(clipReference.path.IsEmpty() ? "None" : clipReference.path.GetString());
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".tgac"))
			{
				makeEditable();

				const char* dropped = static_cast<const char*>(payload->Data);
				editableClipReference->path = StringRegistry::RegisterOrGetString(dropped);
				hasBeenEdited = true;
			}
			ImGui::EndDragDropTarget();
		}

		PropertyEditor::PropertyLabel(true);
		PropertyEditor::PropertyValue(true);

		if (locAssetBrowserGetSelectionFunction)
		{
			if (ImGui::Button("Set From AssetBrowser"))
			{
				// todo: validation
				StringId newValue = locAssetBrowserGetSelectionFunction();
				if (newValue != clipReference.path)
				{
					makeEditable();

					editableClipReference->path = newValue;
					hasBeenEdited = true;
				}
			}
			ImGui::SameLine();
		}

		if (ImGui::Button("Clear"))
		{
			if (!clipReference.path.IsEmpty())
			{
				makeEditable();

				editableClipReference->path = {};
				hasBeenEdited = true;
			}
		}

		return hasBeenEdited;
	}

	IMPLEMENT_PROPERTY_TYPE(CopyOnWriteWrapper<AnimationClipReference>, "Animation Clip")

	template<>
	void LoadFromJson<PoseAndMotion>(PoseAndMotion& value, const JsonData& jsonData)
	{
		jsonData;
		value = {};
	}

	template<>
	void WriteToJson<PoseAndMotion>(const PoseAndMotion& value, JsonData& jsonData)
	{
		jsonData = {};
		value;
	}

	template<>
	bool ShowImGuiEditor<PoseAndMotion>(PoseAndMotion& value, const char* name, const char* description)
	{
		value;

		if (name == nullptr)
		{
			ImGui::Text("Pose");
			return false;
		}

		PropertyEditor::PropertyLabel();
		ImGui::Text(name);
		if (description && *description != 0)
		{
			PropertyEditor::HelpMarker(description);
		}
		PropertyEditor::PropertyValue();


		return false;
	}

	IMPLEMENT_PROPERTY_TYPE(PoseAndMotion, "Pose")
}
