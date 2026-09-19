#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "age/script/Property.h"
#include "age/stringRegistry/StringRegistry.h"

namespace Ag
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
		// IDs remain valid as hierarchy cache entries across scene mutations.
		// Raw SceneObject pointers do not: deleting/reloading an asset could leave
		// the previous frame's hierarchy holding dangling pointers.
		std::vector<uint32_t> mySortedObjects;
		std::unordered_map<StringId, std::vector<StringId>> myFolderPaths;
	
		bool mySceneDirty{ true };

		char mySearchBuffer[128] = "";
		std::string myLastSearch;

		std::vector<PropertyTypeId> myRequiredPropertyTypeIds;
		int mySelectedPropertyTypeIndex = -1;
		// The filter dropdown's contents: every distinct property type across
		// all objects. Used to recompute one std::vector<ScenePropertyDefinition>
		// per object -- unconditionally, every single Draw() -- just to
		// populate a combo box that only changes when the object set does.
		// Rebuilt only when mySceneDirty, the same flag BuildObjectList
		// already gates its own rebuild on.
		std::vector<const PropertyTypeBase*> myAvailablePropertyTypes;

		// Inline rename mirrors established hierarchy behaviour: F2, context-menu
		// Rename, Enter to commit and Escape to cancel.
		uint32_t myRenameObject = 0;
		char myRenameBuffer[256]{};
		bool myFocusRename = false;
	};
}
