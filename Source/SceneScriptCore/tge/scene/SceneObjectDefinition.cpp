#include <stdafx.h>
#include "SceneObjectDefinition.h"
#include <tge/script/JsonData.h>
#include <tge/settings/settings.h>
#include "ScenePropertyTypes.h"

#include <fstream>
#include <filesystem>

#include <nlohmann/json.hpp>

using namespace nlohmann;
using namespace Tga;

void SceneObjectDefinition::Save()
{
	FilePathStream fullPath;
	fullPath << Tga::Settings::GameAssetRoot() << "/" << GetPath();
	fullPath.NormalizePath();

	std::filesystem::path path(fullPath.GetData());

	if (path.empty())
		return;

	if (path.extension().empty())
	{
		path = path.replace_extension(".tgo");
	}

	if (std::filesystem::exists(path.parent_path()) == false)
	{
		std::filesystem::create_directories(path.parent_path());
	}

	json jsonData;
	
	jsonData["parent-object-definition"] = myParent.GetString();

	for (const ScenePropertyDefinition& propertyDefinition : myProperties) 
	{
		JsonData value = {};

		if (propertyDefinition.value.HasValue())
			propertyDefinition.value.WriteToJsonWithoutType(value);

		json propertyJson = {
			{ "name", propertyDefinition.name.GetString() },
			{ "group", propertyDefinition.groupName.GetString()},
			{ "description", propertyDefinition.description.GetString()},
			{ "is-dynamic", (propertyDefinition.flags & ScenePropertyFlags::IsDynamic) != ScenePropertyFlags::None},
			{ "is-per-instance", (propertyDefinition.flags & ScenePropertyFlags::IsPerInstance) != ScenePropertyFlags::None},
			{ "type", propertyDefinition.type->GetName().GetString()},
			{ "value", value.json},
		};

		jsonData["properties"].push_back(propertyJson);
	}

	std::ofstream fout(path, std::ios::trunc);
	fs::permissions(path, fs::perms::all);

	fout << jsonData.dump(2);
}

void SceneObjectDefinition::Load(const char* aPath)
{
	SetPath(aPath);

	std::filesystem::path path = aPath;
	std::string filename = path.stem().string();

	SetName(StringRegistry::RegisterOrGetString(filename.c_str()));

	json jsonData;

	{
		FilePathStream fullPath;
		fullPath << Tga::Settings::GameAssetRoot() << "/" << aPath;
		fullPath.NormalizePath();

		std::ifstream ifs(fullPath.GetData());
		ifs >> jsonData;
		ifs.close();
	}

	myParent = StringRegistry::RegisterOrGetString(jsonData["parent-object-definition"]);

	for (auto& propertyJson : jsonData["properties"])
	{
		ScenePropertyDefinition propertyDefinition = {};
		propertyDefinition.name = StringRegistry::RegisterOrGetString(propertyJson["name"]);
		propertyDefinition.groupName = StringRegistry::RegisterOrGetString(propertyJson.value("group", ""));
		propertyDefinition.description = StringRegistry::RegisterOrGetString(propertyJson.value("description", ""));
		propertyDefinition.flags |= propertyJson["is-dynamic"].get<bool>() ? ScenePropertyFlags::IsDynamic : ScenePropertyFlags::None;
		propertyDefinition.flags |= propertyJson["is-per-instance"].get<bool>() ? ScenePropertyFlags::IsPerInstance : ScenePropertyFlags::None;

		// todo: needs error handling if type doesn't exist!
		propertyDefinition.type = PropertyTypeRegistry::GetPropertyType(StringRegistry::RegisterOrGetString(propertyJson["type"]));

		JsonData valueJson = { propertyJson["value"] };
		propertyDefinition.value = Property::CreateFromJson(propertyDefinition.type, valueJson);

		myProperties.push_back(propertyDefinition);
	}
}
