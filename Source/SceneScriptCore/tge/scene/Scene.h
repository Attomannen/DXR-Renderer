#pragma once
#include <vector>
#include <memory>
#include <string>
#include <set>
#include <map>
#include <tge/math/Matrix4x4.h>


#include <tge/scene/SceneObjectDefinition.h>
#include <tge/scene/SceneObject.h>

#include <tge/util/StringCast.h>
#include <tge/uuid/UUIDManager.h>

namespace Tga
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
	private:
		std::string myPath;
		std::string myName;

		mutable std::unordered_map<uint32_t, StringId> myObjectPathCache;
		std::unordered_map<uint32_t, std::shared_ptr<SceneObject>> myObjects;
		std::map<StringId, int> myFolderCounts; // todo: this needs to include parent folders also
	};
}