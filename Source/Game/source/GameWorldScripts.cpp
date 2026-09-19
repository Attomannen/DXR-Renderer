#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"
#include <age/script/Script.h>
#include <age/script/JsonData.h>
#include <age/script/Nodes/EventNode.h>
#include <age/scene/SceneObjectDefinition.h>

namespace Ag
{
	void EnsureScenePropertiesAreLoaded();
	void EnsureBasePropertiesAreLoaded();
}

// Per-object scripts.
//
// An object's script is the event graph stored inside its .tgo. It starts when the scene
// loads and runs every frame, on the object that the .tgo was placed as.
//
// Variables are the .tgo's properties, as in the editor preview: properties flagged dynamic
// are read and written by scripts (and per-instance values from the scene file override the
// .tgo's defaults), the rest are read-only. A parent object definition is not followed yet.
//
// Not yet: collision events, spawning.

namespace
{
	using namespace Ag;

	// What a script sees of its object: the model instance's transform, and the physics
	// body when the object has one that is currently simulated.
	class ObjectScriptContext final : public GameScriptContext
	{
	public:
		ObjectScriptContext(GameWorld::Impl& world, size_t instance) : myWorld(world), myInstance(instance) {}

		Vector3f GetLocation() const override
		{
			return myWorld.GetInstanceTransform(myInstance).GetPosition();
		}

		void SetLocation(const Vector3f& location) override
		{
			if (const GameWorld::Impl::SceneCharacterObject* character = FindCharacter())
				myWorld.physics.SetCharacterPosition(character->id, { location.x, location.y, location.z });
			if (const GameWorld::Impl::ScenePhysicsObject* object = FindBody())
			{
				// A simulated body is the truth; the next physics sync would undo a model-only move.
				Ag::PhysicsVec3 position;
				Ag::PhysicsQuat rotation;
				if (myWorld.physics.GetTransform(object->body, position, rotation))
				{
					myWorld.physics.SetTransform(object->body, { location.x, location.y, location.z }, rotation);
					myWorld.physics.SetLinearVelocity(object->body, {});
				}
			}
			Matrix4x4f transform = myWorld.GetInstanceTransform(myInstance);
			transform.SetPosition(location);
			myWorld.SetInstanceTransform(myInstance, transform);
		}

		void SetRotation(const Vector3f& eulerDegrees) override
		{
			// Keep the object's scale: rebuild it the way the scene loader does, scale then rotation.
			const Matrix4x4f current = myWorld.GetInstanceTransform(myInstance);
			Vector3f position, scale;
			Quaternionf ignored;
			current.DecomposeMatrix(position, ignored, scale);

			const Quaternionf rotation(eulerDegrees);
			Matrix4x4f transform = Matrix4x4f::CreateFromScale(scale) * Matrix4x4f::CreateFromRotation(rotation);
			transform.SetPosition(position);
			myWorld.SetInstanceTransform(myInstance, transform);

			if (const GameWorld::Impl::ScenePhysicsObject* object = FindBody())
				myWorld.physics.SetTransform(object->body, { position.x, position.y, position.z }, { rotation.X, rotation.Y, rotation.Z, rotation.W });
		}

		Vector3f GetForward() const override { return myWorld.GetInstanceTransform(myInstance).GetForward(); }
		Vector3f GetRight() const override { return myWorld.GetInstanceTransform(myInstance).GetRight(); }
		Vector3f GetUp() const override { return myWorld.GetInstanceTransform(myInstance).GetUp(); }

		bool HasCharacter() const override { return FindCharacter() != nullptr; }

		void MoveCharacter(const Vector3f& velocity) override
		{
			if (const GameWorld::Impl::SceneCharacterObject* character = FindCharacter())
				myWorld.physics.SetCharacterMove(character->id, { velocity.x, 0.f, velocity.z });
		}

		void JumpCharacter(float speed) override
		{
			if (const GameWorld::Impl::SceneCharacterObject* character = FindCharacter())
				myWorld.physics.CharacterJump(character->id, speed);
		}

		bool IsCharacterOnGround() const override
		{
			const GameWorld::Impl::SceneCharacterObject* character = FindCharacter();
			return character && myWorld.physics.IsCharacterOnGround(character->id);
		}

		bool HasCamera() const override { return FindCamera() >= 0; }

		void SetCameraActive(bool active) override
		{
			const int index = FindCamera();
			if (index < 0)
				return;
			if (active)
				myWorld.SetSceneCameraActive(index);
			else if (myWorld.activeSceneCamera == index)
				myWorld.SetSceneCameraActive(-1);
		}

		void SetCameraPitch(float degrees) override
		{
			const int index = FindCamera();
			if (index >= 0)
				myWorld.sceneCameras[index].pitch = std::clamp(degrees, -89.f, 89.f);
		}

		float GetCameraPitch() const override
		{
			const int index = FindCamera();
			return index >= 0 ? myWorld.sceneCameras[index].pitch : 0.f;
		}

		void SetCameraFov(float degrees) override
		{
			const int index = FindCamera();
			if (index < 0)
				return;
			myWorld.sceneCameras[index].fov = std::clamp(degrees, 10.f, 170.f);
			if (myWorld.activeSceneCamera == index)
				myWorld.ApplyCameraFov(myWorld.sceneCameras[index].fov);
		}

		Vector3f GetCameraForward() const override
		{
			const int index = FindCamera();
			const Vector3f forward = myWorld.GetInstanceTransform(myInstance).GetForward();
			const float yaw = GameScene::Rad2Deg(std::atan2(forward.x, forward.z));
			const float pitch = index >= 0 ? myWorld.sceneCameras[index].pitch : 0.f;
			return Matrix4x4f::CreateFromRollPitchYaw(Vector3f{ pitch, yaw, 0.f }).GetForward();
		}

		Vector3f GetVelocity() const override
		{
			if (const GameWorld::Impl::ScenePhysicsObject* object = FindBody())
			{
				const Ag::PhysicsVec3 v = myWorld.physics.GetLinearVelocity(object->body);
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

		bool FindPlayerStart(const char* tag, Vector3f& location, float& yawDegrees) const override
		{
			Matrix4x4f transform;
			if (!myWorld.FindPlayerStart(tag ? tag : "", transform))
				return false;
			location = transform.GetPosition();
			const Vector3f forward = transform.GetForward();
			yawDegrees = std::atan2(forward.x, forward.z) * 57.2957795131f;
			return true;
		}

		void OpenLevel(const char* level) override
		{
			if (level && *level) myWorld.pendingLevel = level;
			else myWorld.restartRequested = true;
		}

		void QuitGame() override { PostQuitMessage(0); }

		const char* GetGameName() const override { return myWorld.gameSettings.gameName.c_str(); }

		// ---- objects
		int GetSelfObject() const override { return (int)myInstance; }
		int GetPlayerPawn() const override { return myWorld.playerPawn; }
		bool IsObjectValid(int object) const override { return object >= 0 && (size_t)object < myWorld.sceneInstances.size(); }

		int FindObject(const char* name) const override
		{
			for (size_t i = 0; i < myWorld.sceneInstances.size(); ++i)
				if (myWorld.sceneInstances[i].name == name)
					return (int)i;
			return -1;
		}

		int FindObjectByDefinition(const char* definition, int nth) const override
		{
			int seen = 0;
			for (size_t i = 0; i < myWorld.sceneInstances.size(); ++i)
				if (myWorld.sceneInstances[i].definition == definition && seen++ == nth)
					return (int)i;
			return -1;
		}

		int CountObjects(const char* definition) const override
		{
			int count = 0;
			for (const GameWorld::Impl::SceneInstance& instance : myWorld.sceneInstances)
				if (instance.definition == definition)
					++count;
			return count;
		}

		Vector3f GetObjectLocation(int object) const override
		{
			return IsObjectValid(object) ? myWorld.GetInstanceTransform((size_t)object).GetPosition() : Vector3f(0.f, 0.f, 0.f);
		}

		void SetObjectLocation(int object, const Vector3f& location) override
		{
			if (!IsObjectValid(object))
				return;
			// The other object's own context knows how to move its body or character too.
			ObjectScriptContext other(myWorld, (size_t)object);
			other.SetLocation(location);
		}

		Vector3f GetObjectForward(int object) const override
		{
			return IsObjectValid(object) ? myWorld.GetInstanceTransform((size_t)object).GetForward() : Vector3f(0.f, 0.f, 1.f);
		}

		// ---- particles
		bool HasParticles(int object) const override { return FindParticles(object) != nullptr; }

		void SetParticlesActive(int object, bool active) override
		{
			if (Particles::SystemInstance* system = FindParticles(object))
			{
				if (active) system->Activate();
				else system->Deactivate();
			}
		}

		void ResetParticles(int object) override
		{
			if (Particles::SystemInstance* system = FindParticles(object))
				system->Reset();
		}

		void BurstParticles(int object, int count) override
		{
			if (Particles::SystemInstance* system = FindParticles(object))
				system->Burst(count);
		}

		void SetParticleFloat(int object, const char* name, float value) override
		{
			if (Particles::SystemInstance* system = FindParticles(object))
				system->SetFloat(name, value);
		}

		void SetParticleVector(int object, const char* name, const Vector3f& value) override
		{
			if (Particles::SystemInstance* system = FindParticles(object))
				system->SetVector(name, value);
		}

		void SetParticleColor(int object, const char* name, float r, float g, float b, float a) override
		{
			if (Particles::SystemInstance* system = FindParticles(object))
				system->SetColor(name, Vector4f(r, g, b, a));
		}

		// ---- sound
		void PlaySound(const char* path, float volume, bool loop) override
		{
			const StringId key = SoundKey(path);
			if (key.IsEmpty())
				return;
			Ag::Audio& audio = myWorld.GetAudio();
			if (!audio.IsLoaded(key))
				audio.Init(path, key, false, loop);
			if (!audio.IsLoaded(key))
				return;   // no such file
			audio.SetVolume(key, volume);
			audio.Play(key, true);
		}

		void StopSound(const char* path) override
		{
			const StringId key = SoundKey(path);
			if (!key.IsEmpty() && myWorld.audio && myWorld.audio->IsLoaded(key))
				myWorld.audio->Stop(key, true);
		}

		void SetSoundVolume(const char* path, float volume) override
		{
			const StringId key = SoundKey(path);
			if (!key.IsEmpty() && myWorld.audio && myWorld.audio->IsLoaded(key))
				myWorld.audio->SetVolume(key, volume);
		}

		bool IsSoundPlaying(const char* path) const override
		{
			const StringId key = SoundKey(path);
			return !key.IsEmpty() && myWorld.audio && myWorld.audio->IsLoaded(key) && myWorld.audio->IsPlaying(key);
		}

		void AddImpulse(const Vector3f& impulse) override
		{
			if (const GameWorld::Impl::ScenePhysicsObject* object = FindBody())
				myWorld.physics.AddImpulse(object->body, { impulse.x, impulse.y, impulse.z });
		}

	private:
		static StringId SoundKey(const char* path) { return path && *path ? StringRegistry::RegisterOrGetString(path) : StringId(); }

		Particles::SystemInstance* FindParticles(int object) const
		{
			const size_t instance = object < 0 ? myInstance : (size_t)object;
			for (GameWorld::Impl::SceneParticleObject& particles : myWorld.sceneParticles)
				if (particles.instance == instance)
					return particles.system.get();
			return nullptr;
		}

		const GameWorld::Impl::SceneCharacterObject* FindCharacter() const
		{
			if (!myWorld.physicsActive)
				return nullptr;
			for (const GameWorld::Impl::SceneCharacterObject& character : myWorld.sceneCharacters)
				if (character.instance == myInstance && character.id.IsValid())
					return &character;
			return nullptr;
		}

		int FindCamera() const
		{
			for (size_t i = 0; i < myWorld.sceneCameras.size(); ++i)
				if (myWorld.sceneCameras[i].instance == myInstance)
					return (int)i;
			return -1;
		}

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

static void LoadObjectDefinition(const GameScene::SceneEntry& entry, GameWorld::Impl::SceneScriptObject& object)
{
	// The property types register themselves from static initialisers; make sure they linked.
	Ag::EnsureScenePropertiesAreLoaded();
	Ag::EnsureBasePropertiesAreLoaded();

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

		// A graph whose nodes are not wired to anything (the untouched default events) does nothing.
		const Script* graph = definition.GetEventGraph();
		if (definition.HasEventGraph() && graph->GetFirstLinkId().id != ScriptLinkId::InvalidId)
		{
			object.graph = std::make_unique<ScriptRuntimeInstance>(definition.GetEventGraphSnapshot());
			object.graph->Init();
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

	SceneScriptObject object;
	object.instance = instanceIndex;
	object.name = entry.tgoPath;
	LoadObjectDefinition(entry, object);

	if (object.graph)
	{
		INFO_PRINT("script: '%s' on object %zu", entry.tgoPath.c_str(), instanceIndex);
		sceneScripts.push_back(std::move(object));
	}
}

void GameWorld::Impl::DispatchContactEvents()
{
	std::vector<Ag::PhysicsContactEvent> events;
	physics.TakeContactEvents(events);
	if (events.empty() || sceneScripts.empty())
		return;

	// Tell both sides: On Trigger Enter when a trigger volume was involved, else On Collision Enter.
	auto raise = [&](uint64_t self, uint64_t other, bool trigger)
	{
		if (self == 0)
			return;
		const size_t instance = (size_t)(self - 1);
		for (SceneScriptObject& object : sceneScripts)
		{
			if (object.instance != instance)
				continue;
			ObjectScriptContext context(*this, object.instance);
			context.deltaTime = 0.f;
			context.frameNumber = scriptFrame;
			context.dynamicProperties = &object.dynamicProperties;
			context.staticProperties = &object.staticProperties;
			context.eventOtherObject = other == 0 ? -1 : (int)(other - 1);
			object.graph->TriggerEvent(trigger ? Ag::ScriptEventKind::TriggerEnter : Ag::ScriptEventKind::CollisionEnter, context);
			return;
		}
	};
	for (const Ag::PhysicsContactEvent& event : events)
	{
		raise(event.userDataA, event.userDataB, event.trigger);
		raise(event.userDataB, event.userDataA, event.trigger);
	}
}

void GameWorld::Impl::UpdateSceneScripts(float deltaSeconds)
{
	if (!scriptsEnabled || sceneScripts.empty())
		return;

	++scriptFrame;
	for (SceneScriptObject& object : sceneScripts)
	{
		if (object.instance >= sceneInstances.size())
			continue;

		ObjectScriptContext context(*this, object.instance);
		context.deltaTime = deltaSeconds;
		context.timeSeconds = animTime;
		context.frameNumber = scriptFrame;
		context.dynamicProperties = &object.dynamicProperties;
		context.staticProperties = &object.staticProperties;

		object.graph->Update(context);
	}
}

void GameWorld::Impl::RegisterSceneCamera(const GameScene::SceneEntry& entry, size_t instanceIndex)
{
	if (!entry.camera.has)
		return;

	SceneCameraObject object;
	object.instance = instanceIndex;
	object.offset = entry.camera.offset;
	object.fov = std::clamp(entry.camera.fov, 10.f, 170.f);
	sceneCameras.push_back(object);

	if (entry.camera.activeOnStart && activeSceneCamera < 0)
		SetSceneCameraActive((int)sceneCameras.size() - 1);
}

void GameWorld::Impl::ApplyCameraFov(float fov)
{
	cameraFov = fov;
	if (cameraProjectionSize.x == 0 || cameraProjectionSize.y == 0) return;
	camera.SetPerspectiveProjection(cameraFov, { (float)cameraProjectionSize.x, (float)cameraProjectionSize.y }, 1.f, 100000.f);
}

void GameWorld::Impl::SetSceneCameraActive(int index)
{
	if (index == activeSceneCamera)
		return;
	activeSceneCamera = index;

	if (index >= 0)
	{
		ApplyCameraFov(sceneCameras[index].fov);
		// A look-around camera wants the cursor: hidden and confined to the window.
		if (input && !mouseTrapped)
		{
			input->HideMouse();
			input->CaptureMouse();
			mouseTrapped = true;
		}
	}
	else
	{
		ApplyCameraFov(90.f);
	}
}

void GameWorld::Impl::UpdateSceneCamera()
{
	if (activeSceneCamera < 0 || activeSceneCamera >= (int)sceneCameras.size())
		return;
	const SceneCameraObject& c = sceneCameras[activeSceneCamera];
	if (c.instance >= sceneInstances.size())
		return;

	const Matrix4x4f transform = GetInstanceTransform(c.instance);
	const Vector3f forward = transform.GetForward();
	const float yaw = GameScene::Rad2Deg(std::atan2(forward.x, forward.z));

	// The eye sits at the offset turned with the object's heading; pitch is the script's.
	const Matrix4x4f heading = Matrix4x4f::CreateFromRollPitchYaw(Vector3f{ 0.f, yaw, 0.f });
	camPos = transform.GetPosition() + c.offset * heading;
	camRot = { c.pitch, yaw, 0.f };
	camera.GetTransform().SetRotation(camRot);
	camera.GetTransform().SetPosition(camPos);
}

void GameWorld::Impl::RegisterSceneCharacter(const GameScene::SceneEntry& entry, const Matrix4x4f& worldTransform, size_t instanceIndex)
{
	if (!entry.character.has)
		return;

	SceneCharacterObject object;
	object.instance = instanceIndex;
	object.startTransform = worldTransform;
	const Vector3f feet = worldTransform.GetPosition();
	object.desc.position = { feet.x, feet.y, feet.z };
	object.desc.radius = entry.character.radius;
	object.desc.height = entry.character.height;
	object.desc.stepHeight = entry.character.stepHeight;
	object.desc.maxSlopeDegrees = entry.character.maxSlope;
	object.desc.mass = entry.character.mass;
	object.desc.userData = instanceIndex + 1; // 0 means "none" in contact events
	sceneCharacters.push_back(object);

	// A scene with a character is a game: the world runs from the start instead of waiting
	// for Start in the Physics tab.
	if (!physics.Init())
		return;
	physicsAutoStart = true;
	physicsIncludeBall = false;
}
