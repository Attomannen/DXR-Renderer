#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"
#include <tge/script/Script.h>
#include <tge/script/ScriptManager.h>
#include <tge/script/JsonData.h>
#include <tge/scene/SceneObjectDefinition.h>

namespace Tga
{
	void EnsureScenePropertiesAreLoaded();
	void EnsureBasePropertiesAreLoaded();
}

// Per-object scripts.
//
// A .tgo's scripts are the .tgscript files in the folder named after it:
// Folder/Name.tgo -> <game root>/Folder/Name/*.tgscript (the same place the editor puts
// them). Every script starts when the scene loads and runs each frame, on the object
// that the .tgo was placed as.
//
// Variables are the .tgo's properties, as in the editor preview: properties flagged dynamic
// are read and written by scripts (and per-instance values from the scene file override the
// .tgo's defaults), the rest are read-only. A parent object definition is not followed yet.
//
// Not yet: collision events, spawning.

namespace
{
	using namespace Tga;

	// What a script sees of its object: the model instance's transform, and the physics
	// body when the object has one that is currently simulated.
	class ObjectScriptContext final : public GameScriptContext
	{
	public:
		ObjectScriptContext(GameWorld::Impl& world, size_t instance) : myWorld(world), myInstance(instance) {}

		Vector3f GetLocation() const override
		{
			return myWorld.models[myInstance].GetTransform().GetPosition();
		}

		void SetLocation(const Vector3f& location) override
		{
			if (const GameWorld::Impl::ScenePhysicsObject* object = FindBody())
			{
				// A simulated body is the truth; the next physics sync would undo a model-only move.
				Tga::PhysicsVec3 position;
				Tga::PhysicsQuat rotation;
				if (myWorld.physics.GetTransform(object->body, position, rotation))
				{
					myWorld.physics.SetTransform(object->body, { location.x, location.y, location.z }, rotation);
					myWorld.physics.SetLinearVelocity(object->body, {});
				}
			}
			Matrix4x4f transform = myWorld.models[myInstance].GetTransform();
			transform.SetPosition(location);
			myWorld.models[myInstance].SetTransform(transform);
		}

		void SetRotation(const Vector3f& eulerDegrees) override
		{
			// Keep the object's scale: rebuild it the way the scene loader does, scale then rotation.
			const Matrix4x4f current = myWorld.models[myInstance].GetTransform();
			Vector3f position, scale;
			Quaternionf ignored;
			current.DecomposeMatrix(position, ignored, scale);

			const Quaternionf rotation(eulerDegrees);
			Matrix4x4f transform = Matrix4x4f::CreateFromScale(scale) * Matrix4x4f::CreateFromRotation(rotation);
			transform.SetPosition(position);
			myWorld.models[myInstance].SetTransform(transform);

			if (const GameWorld::Impl::ScenePhysicsObject* object = FindBody())
				myWorld.physics.SetTransform(object->body, { position.x, position.y, position.z }, { rotation.X, rotation.Y, rotation.Z, rotation.W });
		}

		Vector3f GetForward() const override { return myWorld.models[myInstance].GetTransform().GetForward(); }
		Vector3f GetRight() const override { return myWorld.models[myInstance].GetTransform().GetRight(); }
		Vector3f GetUp() const override { return myWorld.models[myInstance].GetTransform().GetUp(); }

		Vector3f GetVelocity() const override
		{
			if (const GameWorld::Impl::ScenePhysicsObject* object = FindBody())
			{
				const Tga::PhysicsVec3 v = myWorld.physics.GetLinearVelocity(object->body);
				return { v.x, v.y, v.z };
			}
			return { 0.f, 0.f, 0.f };
		}

		void SetVelocity(const Vector3f& velocity) override
		{
			if (const GameWorld::Impl::ScenePhysicsObject* object = FindBody())
				myWorld.physics.SetLinearVelocity(object->body, { velocity.x, velocity.y, velocity.z });
		}

		// Scripts get no input while the debug UI is using it (typing in a field must not walk the player).
		bool IsKeyDown(int keyCode) const override
		{
			return myWorld.input && !UiWantsKeys(keyCode) && myWorld.input->IsKeyHeld(keyCode);
		}

		bool WasKeyPressed(int keyCode) const override
		{
			return myWorld.input && !UiWantsKeys(keyCode) && myWorld.input->IsKeyPressed(keyCode);
		}

		Vector2f GetMouseDelta() const override
		{
			return myWorld.input && !ImGui::GetIO().WantCaptureMouse ? myWorld.input->GetMouseDelta() : Vector2f{ 0.f, 0.f };
		}

		bool HasPhysicsBody() const override
		{
			return FindBody() != nullptr;
		}

		void AddImpulse(const Vector3f& impulse) override
		{
			if (const GameWorld::Impl::ScenePhysicsObject* object = FindBody())
				myWorld.physics.AddImpulse(object->body, { impulse.x, impulse.y, impulse.z });
		}

	private:
		static bool UiWantsKeys(int keyCode)
		{
			// Mouse buttons (VK 1-6) belong to the mouse, everything else to the keyboard.
			const ImGuiIO& io = ImGui::GetIO();
			return keyCode <= 6 ? io.WantCaptureMouse : io.WantCaptureKeyboard;
		}

		const GameWorld::Impl::ScenePhysicsObject* FindBody() const
		{
			if (!myWorld.physicsActive)
				return nullptr;
			for (const GameWorld::Impl::ScenePhysicsObject& object : myWorld.scenePhysicsObjects)
				if (object.instance == myInstance && object.dynamic && object.body.IsValid())
					return &object;
			return nullptr;
		}

		GameWorld::Impl& myWorld;
		size_t myInstance;
	};
}

static void LoadObjectVariables(const GameScene::SceneEntry& entry, GameWorld::Impl::SceneScriptObject& object)
{
	// The property types register themselves from static initialisers; make sure they linked.
	Tga::EnsureScenePropertiesAreLoaded();
	Tga::EnsureBasePropertiesAreLoaded();

	try
	{
		SceneObjectDefinition definition;
		definition.Load((entry.tgoPath + ".tgo").c_str());

		for (const ScenePropertyDefinition& property : definition.GetProperties())
		{
			if (!property.type || !property.value.HasValue())
				continue;

			Property value = property.value;

			// Per-instance override from the scene file.
			if ((property.flags & ScenePropertyFlags::IsPerInstance) != ScenePropertyFlags::None)
			{
				for (const GameScene::json& item : entry.instanceProperties)
				{
					if (item.value("name", "") != property.name.GetString() || item.value("type", "") != property.type->GetName().GetString())
						continue;
					if (item.contains("value"))
						value = Property::CreateFromJson(property.type, JsonData{ item["value"] });
					break;
				}
			}

			if ((property.flags & ScenePropertyFlags::IsDynamic) != ScenePropertyFlags::None)
				object.dynamicProperties[property.name] = value;
			else
				object.staticProperties[property.name] = value;
		}
	}
	catch (const std::exception& e)
	{
		ERROR_PRINT("script: could not read the variables of '%s': %s", entry.tgoPath.c_str(), e.what());
	}
}

void GameWorld::Impl::ClearSceneScripts()
{
	sceneScripts.clear();
	scriptFrame = 0;
}

void GameWorld::Impl::RegisterSceneScripts(const GameScene::SceneEntry& entry, size_t instanceIndex)
{
	if (entry.tgoPath.empty())
		return;

	namespace fs = std::filesystem;
	const fs::path root = Settings::GameAssetRoot();
	const fs::path folder = root / entry.tgoPath;
	std::error_code ec;
	if (!fs::is_directory(folder, ec))
		return;

	SceneScriptObject object;
	object.instance = instanceIndex;
	object.name = entry.tgoPath;
	LoadObjectVariables(entry, object);

	for (const fs::directory_entry& item : fs::recursive_directory_iterator(folder, ec))
	{
		if (!item.is_regular_file() || item.path().extension() != ".tgscript")
			continue;

		fs::path relative = fs::relative(item.path(), root, ec);
		relative.replace_extension("");
		const std::string scriptPath = relative.generic_string();

		std::shared_ptr<const Script> script = ScriptManager::GetScript(scriptPath);
		if (!script)
		{
			ERROR_PRINT("script: could not load '%s'", scriptPath.c_str());
			continue;
		}

		object.scripts.push_back(std::make_unique<ScriptRuntimeInstance>(script));
		object.scripts.back()->Init();
		INFO_PRINT("script: '%s' on object %zu", scriptPath.c_str(), instanceIndex);
	}

	if (!object.scripts.empty())
		sceneScripts.push_back(std::move(object));
}

void GameWorld::Impl::UpdateSceneScripts(float deltaSeconds)
{
	if (!scriptsEnabled || sceneScripts.empty())
		return;

	++scriptFrame;
	for (SceneScriptObject& object : sceneScripts)
	{
		if (object.instance >= models.size())
			continue;

		ObjectScriptContext context(*this, object.instance);
		context.deltaTime = deltaSeconds;
		context.frameNumber = scriptFrame;
		context.dynamicProperties = &object.dynamicProperties;
		context.staticProperties = &object.staticProperties;

		for (std::unique_ptr<ScriptRuntimeInstance>& script : object.scripts)
			script->Update(context);
	}
}
