#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "tge/script/Property.h"
#include "tge/stringRegistry/StringRegistry.h"

namespace Tga
{
	class SceneObject;

	class SceneObjectList 
	{
	public:
		void Draw();
		void SetSceneDirty();

	private:
		void SearchAndFilterBar(const std::unordered_map<uint32_t, std::shared_ptr<SceneObject>>& aAllObjects);
		void BuildObjectList(const std::unordered_map<uint32_t, std::shared_ptr<SceneObject>>& aAllObjects, bool aHasSearch, bool aHasFilter);

	private:
		std::vector<std::pair<uint32_t, SceneObject*>> mySortedObjects;
		std::unordered_map<StringId, std::vector<StringId>> myFolderPaths;
	
		bool mySceneDirty{ true };

		char mySearchBuffer[128] = "";
		std::string myLastSearch;

		std::vector<PropertyTypeId> myRequiredPropertyTypeIds;
		int mySelectedPropertyTypeIndex = -1;
	};
}
