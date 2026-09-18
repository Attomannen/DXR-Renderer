#include <stdafx.h>

#include "ScenePropertyTypes.h"
#include <tge/script/JsonData.h>
#include <tge/imgui/ImGuiPropertyEditor.h>
#include <tge/util/StringCast.h>
#include <imgui/imgui.h>

using namespace Tga;

StringId(*locAssetBrowserGetSelectionFunction)();
GetModelMeshInfoFunction locGetModelMeshInfoFunction = nullptr;
GetModelCollisionInfoFunction locGetModelCollisionInfoFunction = nullptr;

namespace
{
	const char* const kModelCollisionNames[] = { "None", "Auto", "Box", "ConvexHull", "TriangleMesh" };
	constexpr int kModelCollisionCount = 5;
}

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

	void RegisterGetModelCollisionInfoFunction(GetModelCollisionInfoFunction aGetFunction)
	{
		locGetModelCollisionInfoFunction = aGetFunction;
	}

	bool GetModelCollisionInfo(StringId modelPath, size_t maxTriangles, SceneModelCollisionInfo& outInfo)
	{
		return locGetModelCollisionInfoFunction && locGetModelCollisionInfoFunction(modelPath, maxTriangles, outInfo);
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

		model.collision = SceneModelCollision::None;
		if (jsonData.json.contains("collision"))
		{
			const std::string collision = jsonData.json.value("collision", "None");
			for (int i = 0; i < kModelCollisionCount; i++)
				if (collision == kModelCollisionNames[i])
					model.collision = static_cast<SceneModelCollision>(i);
		}

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
		if (model.collision != SceneModelCollision::None)
			jsonData.json["collision"] = kModelCollisionNames[static_cast<int>(model.collision)];

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
		ImGui::Indent();
		ImGui::Text("Collision");
		ImGui::Unindent();
		PropertyEditor::PropertyValue(true);
		{
			const int current = static_cast<int>(model.collision);
			if (ImGui::BeginCombo("##ModelCollision", kModelCollisionNames[current]))
			{
				for (int i = 0; i < kModelCollisionCount; i++)
				{
					if (ImGui::Selectable(kModelCollisionNames[i], i == current) && i != current)
					{
						makeEditable();
						editableModel->collision = static_cast<SceneModelCollision>(i);
						hasBeenEdited = true;
					}
				}
				ImGui::EndCombo();
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Collision built from this model's geometry.\nAuto: static objects get an exact triangle mesh, objects with a Rigidbody get a convex hull.");
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
							// Label the slot with its material, like Blender's slot list -- the
							// source mesh node's name ("Mesh.123") doesn't tell you which
							// material you're assigning. Fall back to the mesh name only for a
							// slot with no material recorded at all.
							const StringId materialName = meshInfo.materialNames[i];
							display = ImGui::TreeNode(!materialName.IsEmpty() ? materialName.GetString() : meshInfo.meshNames[i].GetString());
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

	IMPLEMENT_COMPONENT_PROPERTY_TYPE(CopyOnWriteWrapper<SceneModel>, "Model")

	namespace
	{
		const char* const kColliderShapeNames[] = { "Auto", "Box", "Sphere", "Capsule", "ConvexHull", "TriangleMesh" };
		const char* const kBodyMotionNames[] = { "Static", "Kinematic", "Dynamic" };

		template <typename E, size_t N>
		E EnumFromName(const std::string& name, const char* const (&names)[N], E fallback)
		{
			for (size_t i = 0; i < N; i++)
				if (name == names[i])
					return static_cast<E>(i);
			return fallback;
		}

		Vector3f ReadVector3(const nlohmann::json& json, const char* key, const Vector3f& fallback)
		{
			if (!json.contains(key) || !json[key].is_array() || json[key].size() < 3)
				return fallback;
			return { json[key][0].get<float>(), json[key][1].get<float>(), json[key][2].get<float>() };
		}

		void BeginRow(const char* label)
		{
			PropertyEditor::PropertyLabel(true);
			ImGui::Indent();
			ImGui::Text("%s", label);
			ImGui::Unindent();
			PropertyEditor::PropertyValue(true);
			ImGui::PushID(label);
		}

		void EndRow()
		{
			ImGui::PopID();
		}

		template <size_t N>
		bool EnumRow(const char* label, int& value, const char* const (&names)[N])
		{
			BeginRow(label);
			bool changed = false;
			if (ImGui::BeginCombo("##v", names[value]))
			{
				for (int i = 0; i < static_cast<int>(N); i++)
				{
					if (ImGui::Selectable(names[i], i == value) && i != value)
					{
						value = i;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			EndRow();
			return changed;
		}

		bool FloatRow(const char* label, float& value, float speed, float minValue, float maxValue)
		{
			BeginRow(label);
			const float before = value;
			const bool changed = ImGui::DragFloat("##v", &value, speed, minValue, maxValue) && value != before;
			EndRow();
			return changed;
		}

		bool Vector3Row(const char* label, Vector3f& value, float speed)
		{
			BeginRow(label);
			const Vector3f before = value;
			const bool changed = ImGui::DragFloat3("##v", &value.x, speed) && value != before;
			EndRow();
			return changed;
		}

		void PropertyHeader(const char* name, const char* description)
		{
			PropertyEditor::PropertyLabel();
			ImGui::Text("%s", name);
			if (description && *description != 0)
				PropertyEditor::HelpMarker(description);
			PropertyEditor::PropertyValue();
		}
	}

	template<>
	void LoadFromJson<CopyOnWriteWrapper<SceneCollider>>(CopyOnWriteWrapper<SceneCollider>& value, const JsonData& jsonData)
	{
		value = CopyOnWriteWrapper<SceneCollider>::Create();
		SceneCollider& collider = value.Edit();
		const nlohmann::json& json = jsonData.json;

		collider.shape = EnumFromName(json.value("shape", "Auto"), kColliderShapeNames, SceneColliderShape::Auto);
		collider.halfExtents = ReadVector3(json, "halfExtents", collider.halfExtents);
		collider.radius = json.value("radius", collider.radius);
		collider.halfHeight = json.value("halfHeight", collider.halfHeight);
		collider.offset = ReadVector3(json, "offset", collider.offset);
		collider.isTrigger = json.value("isTrigger", collider.isTrigger);
	}

	template<>
	void WriteToJson<CopyOnWriteWrapper<SceneCollider>>(const CopyOnWriteWrapper<SceneCollider>& value, JsonData& jsonData)
	{
		const SceneCollider& collider = value.Get();
		nlohmann::json& json = jsonData.json;

		json["shape"] = kColliderShapeNames[static_cast<int>(collider.shape)];
		json["halfExtents"] = { collider.halfExtents.x, collider.halfExtents.y, collider.halfExtents.z };
		json["radius"] = collider.radius;
		json["halfHeight"] = collider.halfHeight;
		json["offset"] = { collider.offset.x, collider.offset.y, collider.offset.z };
		json["isTrigger"] = collider.isTrigger;
	}

	template<>
	bool ShowImGuiEditor<CopyOnWriteWrapper<SceneCollider>>(CopyOnWriteWrapper<SceneCollider>& value, const char* name, const char* description)
	{
		const SceneCollider& collider = value.Get();

		if (name == nullptr)
		{
			ImGui::Text("%s", kColliderShapeNames[static_cast<int>(collider.shape)]);
			return false;
		}

		PropertyHeader(name, description);

		// Edit a copy and only touch the copy-on-write value if something changed.
		SceneCollider edited = collider;
		bool changed = false;

		int shape = static_cast<int>(edited.shape);
		if (EnumRow("Shape", shape, kColliderShapeNames))
		{
			edited.shape = static_cast<SceneColliderShape>(shape);
			changed = true;
		}

		if (edited.shape == SceneColliderShape::Box)
			changed |= Vector3Row("Half extents", edited.halfExtents, 1.f);
		if (edited.shape == SceneColliderShape::Sphere || edited.shape == SceneColliderShape::Capsule)
			changed |= FloatRow("Radius", edited.radius, 1.f, 0.1f, 100000.f);
		if (edited.shape == SceneColliderShape::Capsule)
			changed |= FloatRow("Half height", edited.halfHeight, 1.f, 0.1f, 100000.f);
		if (edited.shape == SceneColliderShape::Box || edited.shape == SceneColliderShape::Sphere || edited.shape == SceneColliderShape::Capsule)
			changed |= Vector3Row("Offset", edited.offset, 1.f);

		BeginRow("Is trigger");
		changed |= ImGui::Checkbox("##v", &edited.isTrigger);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("A trigger does not block. It raises On Trigger Enter on the objects that overlap it.");
		EndRow();

		if (changed)
			value.Edit() = edited;
		return changed;
	}

	IMPLEMENT_COMPONENT_PROPERTY_TYPE(CopyOnWriteWrapper<SceneCollider>, "Collider")

	template<>
	void LoadFromJson<CopyOnWriteWrapper<SceneRigidBody>>(CopyOnWriteWrapper<SceneRigidBody>& value, const JsonData& jsonData)
	{
		value = CopyOnWriteWrapper<SceneRigidBody>::Create();
		SceneRigidBody& body = value.Edit();
		const nlohmann::json& json = jsonData.json;

		body.motion = EnumFromName(json.value("motion", "Dynamic"), kBodyMotionNames, SceneBodyMotion::Dynamic);
		body.mass = json.value("mass", body.mass);
		body.friction = json.value("friction", body.friction);
		body.restitution = json.value("restitution", body.restitution);
		body.gravityFactor = json.value("gravityFactor", body.gravityFactor);
		body.linearDamping = json.value("linearDamping", body.linearDamping);
		body.angularDamping = json.value("angularDamping", body.angularDamping);
	}

	template<>
	void WriteToJson<CopyOnWriteWrapper<SceneRigidBody>>(const CopyOnWriteWrapper<SceneRigidBody>& value, JsonData& jsonData)
	{
		const SceneRigidBody& body = value.Get();
		nlohmann::json& json = jsonData.json;

		json["motion"] = kBodyMotionNames[static_cast<int>(body.motion)];
		json["mass"] = body.mass;
		json["friction"] = body.friction;
		json["restitution"] = body.restitution;
		json["gravityFactor"] = body.gravityFactor;
		json["linearDamping"] = body.linearDamping;
		json["angularDamping"] = body.angularDamping;
	}

	template<>
	bool ShowImGuiEditor<CopyOnWriteWrapper<SceneRigidBody>>(CopyOnWriteWrapper<SceneRigidBody>& value, const char* name, const char* description)
	{
		const SceneRigidBody& body = value.Get();

		if (name == nullptr)
		{
			ImGui::Text("%s", kBodyMotionNames[static_cast<int>(body.motion)]);
			return false;
		}

		PropertyHeader(name, description);

		SceneRigidBody edited = body;
		bool changed = false;

		int motion = static_cast<int>(edited.motion);
		if (EnumRow("Motion", motion, kBodyMotionNames))
		{
			edited.motion = static_cast<SceneBodyMotion>(motion);
			changed = true;
		}

		if (edited.motion == SceneBodyMotion::Dynamic)
		{
			changed |= FloatRow("Mass (kg, 0 = auto)", edited.mass, 0.1f, 0.f, 1000000.f);
			changed |= FloatRow("Gravity factor", edited.gravityFactor, 0.01f, -10.f, 10.f);
			changed |= FloatRow("Linear damping", edited.linearDamping, 0.005f, 0.f, 100.f);
			changed |= FloatRow("Angular damping", edited.angularDamping, 0.005f, 0.f, 100.f);
		}
		changed |= FloatRow("Friction", edited.friction, 0.01f, 0.f, 10.f);
		changed |= FloatRow("Restitution", edited.restitution, 0.01f, 0.f, 1.f);

		if (changed)
			value.Edit() = edited;
		return changed;
	}

	IMPLEMENT_COMPONENT_PROPERTY_TYPE(CopyOnWriteWrapper<SceneRigidBody>, "Rigidbody")

	template<>
	void LoadFromJson<CopyOnWriteWrapper<SceneCamera>>(CopyOnWriteWrapper<SceneCamera>& value, const JsonData& jsonData)
	{
		value = CopyOnWriteWrapper<SceneCamera>::Create();
		SceneCamera& camera = value.Edit();
		const nlohmann::json& json = jsonData.json;

		camera.offset = ReadVector3(json, "offset", camera.offset);
		camera.fov = json.value("fov", camera.fov);
		camera.activeOnStart = json.value("activeOnStart", camera.activeOnStart);
	}

	template<>
	void WriteToJson<CopyOnWriteWrapper<SceneCamera>>(const CopyOnWriteWrapper<SceneCamera>& value, JsonData& jsonData)
	{
		const SceneCamera& camera = value.Get();
		nlohmann::json& json = jsonData.json;

		json["offset"] = { camera.offset.x, camera.offset.y, camera.offset.z };
		json["fov"] = camera.fov;
		json["activeOnStart"] = camera.activeOnStart;
	}

	template<>
	bool ShowImGuiEditor<CopyOnWriteWrapper<SceneCamera>>(CopyOnWriteWrapper<SceneCamera>& value, const char* name, const char* description)
	{
		const SceneCamera& camera = value.Get();

		if (name == nullptr)
		{
			ImGui::Text("%.0f degrees", camera.fov);
			return false;
		}

		PropertyHeader(name, description);

		SceneCamera edited = camera;
		bool changed = false;

		changed |= Vector3Row("Offset", edited.offset, 1.f);
		changed |= FloatRow("Field of view", edited.fov, 0.5f, 10.f, 170.f);

		BeginRow("Active on start");
		changed |= ImGui::Checkbox("##v", &edited.activeOnStart);
		EndRow();

		if (changed)
			value.Edit() = edited;
		return changed;
	}

	IMPLEMENT_COMPONENT_PROPERTY_TYPE(CopyOnWriteWrapper<SceneCamera>, "Camera")

	template<>
	void LoadFromJson<CopyOnWriteWrapper<SceneCharacter>>(CopyOnWriteWrapper<SceneCharacter>& value, const JsonData& jsonData)
	{
		value = CopyOnWriteWrapper<SceneCharacter>::Create();
		SceneCharacter& character = value.Edit();
		const nlohmann::json& json = jsonData.json;

		character.radius = json.value("radius", character.radius);
		character.height = json.value("height", character.height);
		character.stepHeight = json.value("stepHeight", character.stepHeight);
		character.maxSlope = json.value("maxSlope", character.maxSlope);
		character.mass = json.value("mass", character.mass);
	}

	template<>
	void WriteToJson<CopyOnWriteWrapper<SceneCharacter>>(const CopyOnWriteWrapper<SceneCharacter>& value, JsonData& jsonData)
	{
		const SceneCharacter& character = value.Get();
		nlohmann::json& json = jsonData.json;

		json["radius"] = character.radius;
		json["height"] = character.height;
		json["stepHeight"] = character.stepHeight;
		json["maxSlope"] = character.maxSlope;
		json["mass"] = character.mass;
	}

	template<>
	bool ShowImGuiEditor<CopyOnWriteWrapper<SceneCharacter>>(CopyOnWriteWrapper<SceneCharacter>& value, const char* name, const char* description)
	{
		const SceneCharacter& character = value.Get();

		if (name == nullptr)
		{
			ImGui::Text("%.0f x %.0f cm", character.radius * 2.f, character.height);
			return false;
		}

		PropertyHeader(name, description);

		SceneCharacter edited = character;
		bool changed = false;

		changed |= FloatRow("Radius", edited.radius, 0.5f, 5.f, 200.f);
		changed |= FloatRow("Height", edited.height, 1.f, 20.f, 500.f);
		changed |= FloatRow("Step height", edited.stepHeight, 0.5f, 0.f, 150.f);
		changed |= FloatRow("Max slope", edited.maxSlope, 0.5f, 0.f, 89.f);
		changed |= FloatRow("Mass (kg)", edited.mass, 0.5f, 1.f, 1000.f);

		if (changed)
			value.Edit() = edited;
		return changed;
	}

	IMPLEMENT_COMPONENT_PROPERTY_TYPE(CopyOnWriteWrapper<SceneCharacter>, "Character")

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
