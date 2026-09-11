#include <stdafx.h>

#include "ScriptNodeTypeRegistry.h"

#include <tge/stringRegistry/StringRegistry.h>

#include <cassert>
#include <sstream>

using namespace Tga;

std::unordered_map<std::string_view, ScriptNodeTypeId> ScriptNodeTypeRegistry::ourStringToTypeId;
std::vector<ScriptNodeTypeRegistry::TypeInfo> ScriptNodeTypeRegistry::ourTypeInfos;
ScriptNodeTypeRegistry::CategoryInfo ScriptNodeTypeRegistry::ourRootCategory = {};


ScriptNodeTypeId Tga::ScriptNodeTypeRegistry::GetTypeId(std::string_view typeName)
{
	assert("Type Registry is empty. You need to register nodes before starting." && !ourTypeInfos.empty());

	auto it = ourStringToTypeId.find(typeName);
	if (it != ourStringToTypeId.end())
		return it->second;

	return { 0xFFFFFFFF };
}

std::unique_ptr<ScriptNodeBase> Tga::ScriptNodeTypeRegistry::CreateNode(ScriptNodeTypeId typeId)
{
	assert("Invalid type id" && typeId.id < ourTypeInfos.size());

	return (*ourTypeInfos[typeId.id].createNodeFunctionPtr)();
}

std::string_view Tga::ScriptNodeTypeRegistry::GetNodeTypeShortName(ScriptNodeTypeId typeId)
{
	assert("Invalid type id" && typeId.id < ourTypeInfos.size());

	return ourTypeInfos[typeId.id].shortName;
}

std::string_view Tga::ScriptNodeTypeRegistry::GetNodeTypeFullName(ScriptNodeTypeId typeId)
{
	assert("Invalid type id" && typeId.id < ourTypeInfos.size());

	return ourTypeInfos[typeId.id].fullName;
}

std::string_view Tga::ScriptNodeTypeRegistry::GetNodeTooltip(ScriptNodeTypeId typeId)
{
	assert("Invalid type id" && typeId.id < ourTypeInfos.size());

	return ourTypeInfos[typeId.id].toolTip;
}

ScriptNodeTypeRegistry::TypeInfo& ScriptNodeTypeRegistry::RegisterTypeInternal(const char* fullName, const char* toolTip)
{
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

	assert("Node type name (without category) already exists. Duplicates are not allowed" && ourStringToTypeId.find(typeInfo.shortName) == ourStringToTypeId.end());
	assert("Node type name (including cagories) already exists. Duplicates are not allowed" && ourStringToTypeId.find(typeInfo.fullName) == ourStringToTypeId.end());

	ourStringToTypeId[typeInfo.shortName] = typeId;
	ourStringToTypeId[typeInfo.fullName] = typeId;

	return typeInfo;
}
