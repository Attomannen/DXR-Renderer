#pragma once
#include <vector>
#include <memory>
#include <string>
#include <set>
#include <map>
#include <age/math/Matrix4x4.h>


#include <age/scene/SceneObjectDefinition.h>
#include <age/scene/SceneObject.h>

#include <age/util/StringCast.h>
#include <age/uuid/UUIDManager.h>

namespace Ag
{

	class Camera;
	class SceneObject;
	class ScriptRuntimeInstance;
	struct ScriptUpdateContext;

	class Scene 
	{
	public:
		Scene();
		~Scene();

		template<typename T>
		std::pair<uint32_t, T&> CreateSceneObject()
		{
			std::shared_ptr<T> object = std::make_shared<T>();
			uint32_t id = UUIDManager::CreateUUID();
			myObjects[id] = object;

			myFolderCounts[{}]++;

			return { id, *object };
		}

		// For scene loading where we already have a uuid for the object
		template<typename T>
		std::pair<uint32_t, T&> CreateSceneObject(const char* uuid, T& object) {
			uint32_t id = UUIDManager::GetIDFromUUID(uuid);
			myFolderCounts[object.GetPath()]++;
			myObjects[id] = std::make_shared<T>(object);

			myFolderCounts[{}]++;

			return { id, *myObjects[id].get()};
		}

		const std::unordered_map<uint32_t, std::shared_ptr<SceneObject>>& GetSceneObjects() const { return myObjects; }

		SceneObject* GetFirstSceneObject(std::string_view aName) const
		{
			for (auto& pair : myObjects)
			{
				if (pair.second->GetName() == aName)
					return pair.second.get();
			}

			return nullptr;
		}

		SceneObject* GetSceneObject(uint32_t anId) const
		{
			auto it = myObjects.find(anId);
			if (it != myObjects.end())
			{
				return it->second.get();
			}

			return nullptr;
		}

		std::shared_ptr<SceneObject> GetSceneObjectSharedPtr(uint32_t anId)
		{
			auto it = myObjects.find(anId);
			if (it != myObjects.end())
			{
				return it->second;
			}
			return nullptr;
		}

		void DeleteSceneObject(uint32_t anId);
		void AddSceneObject(uint32_t anId, std::shared_ptr<SceneObject> anObject);

		void ClearScene() 
		{
			myObjects.clear();
			myObjectPathCache.clear();
			myFolderCounts.clear();
		}

		void SetName(const char* name) { myName = name; }
		const char* GetName() const { return myName.c_str(); }
		void SetPath(const char* path) { myPath = path; myObjectPathCache.clear(); }
		const char* GetPath() const { return myPath.c_str(); }

		StringId GetObjectFilePath(uint32_t anId) const;

		void UpdateFolderCounts(StringId aOldFolder, StringId aNewFolder);
		void GetAllFolderNames(std::vector<StringId>& outFolderNames);

		// Scene-wide directional sun + ambient fill. Genuinely part of the
		// scene's authored lighting setup (not just an editor preview), so
		// this lives on Scene itself rather than in editor-only state --
		// DefaultSceneEditorGraphics feeds these straight into
		// DeferredRenderer/GraphicsStateStack, and the hierarchy panel
		// exposes them as two fixed pseudo-entries (see SceneLightSelection).
		float mySunYaw = 45.f;
		float mySunPitch = -45.f;
		float mySunColor[3] = { 0.9f, 0.7f, 0.5f };
		float mySunIntensity = 1.4f;
		float myAmbientColor[3] = { 0.25f, 0.28f, 0.35f };

		float GetSunYaw() const { return mySunYaw; }
		void SetSunYaw(float v) { mySunYaw = v; }
		float GetSunPitch() const { return mySunPitch; }
		void SetSunPitch(float v) { mySunPitch = v; }
		float* GetSunColor() { return mySunColor; }
		const float* GetSunColor() const { return mySunColor; }
		float GetSunIntensity() const { return mySunIntensity; }
		void SetSunIntensity(float v) { mySunIntensity = v; }
		float* GetAmbientColor() { return myAmbientColor; }
		const float* GetAmbientColor() const { return myAmbientColor; }

		// Project-relative path (see Settings::GameAssetRoot()) to a .hdr
		// equirectangular panorama or a .dds cubemap used as the scene's IBL
		// environment/backdrop. Empty = no environment, falls back to the
		// uniform ambient color above. Lives here for the same reason the sun/
		// ambient fields do: it's authored scene state, not editor-only.
		std::string myEnvironmentTexturePath;
		const std::string& GetEnvironmentTexturePath() const { return myEnvironmentTexturePath; }

		// The Game Mode this level uses (a .tgo relative to the asset root); empty uses the project's default.
		std::string myGameMode;
		const std::string& GetGameMode() const { return myGameMode; }
		void SetGameMode(const std::string& path) { myGameMode = path; }
		void SetEnvironmentTexturePath(const std::string& path) { myEnvironmentTexturePath = path; }

	private:
		std::string myPath;
		std::string myName;

		mutable std::unordered_map<uint32_t, StringId> myObjectPathCache;
		std::unordered_map<uint32_t, std::shared_ptr<SceneObject>> myObjects;
		std::map<StringId, int> myFolderCounts; // todo: this needs to include parent folders also
	};
}
