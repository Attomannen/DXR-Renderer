#include <stdafx.h>
#include <age/log/Log.h>

#include "ScriptNodeTypeRegistry.h"

#include <age/stringRegistry/StringRegistry.h>

#include <cassert>
#include <sstream>

using namespace Ag;

std::unordered_map<std::string_view, ScriptNodeTypeId> ScriptNodeTypeRegistry::ourStringToTypeId;
std::vector<ScriptNodeTypeRegistry::TypeInfo> ScriptNodeTypeRegistry::ourTypeInfos;
ScriptNodeTypeRegistry::CategoryInfo ScriptNodeTypeRegistry::ourRootCategory = {};


ScriptNodeTypeId Ag::ScriptNodeTypeRegistry::GetTypeId(std::string_view typeName)
{
	assert("Type Registry is empty. You need to register nodes before starting." && !ourTypeInfos.empty());

	auto it = ourStringToTypeId.find(typeName);
	if (it != ourStringToTypeId.end())
		return it->second;

	return { 0xFFFFFFFF };
}

std::unique_ptr<ScriptNodeBase> Ag::ScriptNodeTypeRegistry::CreateNode(ScriptNodeTypeId typeId)
{
	assert("Invalid type id" && typeId.id < ourTypeInfos.size());

	return (*ourTypeInfos[typeId.id].createNodeFunctionPtr)();
}

std::string_view Ag::ScriptNodeTypeRegistry::GetNodeTypeShortName(ScriptNodeTypeId typeId)
{
	assert("Invalid type id" && typeId.id < ourTypeInfos.size());

	return ourTypeInfos[typeId.id].shortName;
}

std::string_view Ag::ScriptNodeTypeRegistry::GetNodeTypeFullName(ScriptNodeTypeId typeId)
{
	assert("Invalid type id" && typeId.id < ourTypeInfos.size());

	return ourTypeInfos[typeId.id].fullName;
}

std::string_view Ag::ScriptNodeTypeRegistry::GetNodeTooltip(ScriptNodeTypeId typeId)
{
	assert("Invalid type id" && typeId.id < ourTypeInfos.size());

	return ourTypeInfos[typeId.id].toolTip;
}

ScriptNodeTypeRegistry::TypeInfo& ScriptNodeTypeRegistry::RegisterTypeInternal(const char* fullName, const char* toolTip)
{
	// Registering the same full name twice is benign and now expected: the
	// editor hosting an in-viewport play session links both the editor's and
	// the game's node translation units into one process, and they share this
	// registry. Hand back the existing entry instead of adding a second one.
	//
	// A SHORT name colliding while the full name differs is still an authoring
	// mistake -- two different nodes that cannot be told apart in a search box --
	// and still asserts below.
	if (const auto existing = ourStringToTypeId.find(fullName); existing != ourStringToTypeId.end())
		return ourTypeInfos[existing->second.id];

	ScriptNodeTypeId typeId = { (unsigned int)ourTypeInfos.size() };

	TypeInfo& typeInfo = ourTypeInfos.emplace_back();

	typeInfo.fullName = fullName;

	size_t pos = strlen(fullName);
	while(--pos > 0)
	{
		if (fullName[pos] == '/' && fullName[pos+1] != '\0') { break; }
	}

	const char* shortName = &fullName[pos];
	if (shortName == nullptr)
	{
		shortName = fullName;
	}
	else
	{
		shortName += 1; // skip the '/'
	}
	typeInfo.shortName = shortName;

	std::istringstream f(fullName);
	std::string s;

	CategoryInfo* category = &ourRootCategory;
	while (std::getline(f, s, '/'))
	{
		StringId stringId = StringRegistry::RegisterOrGetString(s.c_str());
		typeInfo.path.push_back(stringId);
	}

	for (int pathIndex = 0; pathIndex + 1 < typeInfo.path.size(); pathIndex++)
	{
		StringId stringId = typeInfo.path[pathIndex];
		int childCategoryIndex = -1;
		for (int i = 0; i < category->childCategories.size(); i++)
		{
			if (category->childCategories[i].name == stringId)
			{
				childCategoryIndex = i;
			}
		}

		if (childCategoryIndex == -1)
		{
			childCategoryIndex = (int)category->childCategories.size();
			CategoryInfo& childCategory = category->childCategories.emplace_back();
			childCategory.name = stringId;
		}

		category = &category->childCategories[childCategoryIndex];
	}

	category->nodeTypes.push_back(typeId);

	typeInfo.toolTip = toolTip;

	if (ourStringToTypeId.find(typeInfo.shortName) != ourStringToTypeId.end())
		ERROR_PRINT("script node '%s': short name '%s' is already registered by another node type",
			typeInfo.fullName, typeInfo.shortName);
	assert("Node type name (without category) already exists. Duplicates are not allowed" && ourStringToTypeId.find(typeInfo.shortName) == ourStringToTypeId.end());
	assert("Node type name (including cagories) already exists. Duplicates are not allowed" && ourStringToTypeId.find(typeInfo.fullName) == ourStringToTypeId.end());

	ourStringToTypeId[typeInfo.shortName] = typeId;
	ourStringToTypeId[typeInfo.fullName] = typeId;

	return typeInfo;
}
