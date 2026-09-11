#include <stdafx.h>
#include "SceneSerialize.h"
#include "Scene.h"

#include <fstream>

#include <tge/stringRegistry/StringRegistry.h>

#include <tge/settings/settings.h>
#include <tge/script/jsondata.h>

#include <tge/scene/SceneObjectDefinitionManager.h>

#include <nlohmann/json.hpp>
using namespace nlohmann;
using namespace Tga;


extern void Tga::SaveScene(const Scene& scene, void (*afileChangedCallback)(SceneFileChangeType, const char*))
{
	FilePathStream fullPath;
	fullPath << Tga::Settings::GameAssetRoot() << "/" << scene.GetPath();
	fullPath.NormalizePath();

	std::filesystem::path path(fullPath.GetData());
	std::filesystem::path levelDataPath = path;

	std::filesystem::path levelDataRelativePath = scene.GetPath();

	levelDataPath.replace_extension(".leveldata");
	levelDataRelativePath.replace_extension(".leveldata");

	if (path.empty())
		return;
	if (path.extension().empty())
	{
		path = path.replace_extension(".tgs");
	}

	if (std::filesystem::is_directory(path.parent_path()) == false)
	{
		fs::permissions(path.parent_path().parent_path(), fs::perms::all);
		std::filesystem::create_directories(path.parent_path());
	}

	if (std::filesystem::is_directory(levelDataPath) == false)
	{
		fs::permissions(levelDataPath.parent_path(), fs::perms::all);
		std::filesystem::create_directories(levelDataPath);
	}

	std::set<std::string> remainingFiles;

	for (const auto& entry : fs::directory_iterator(levelDataPath))
	{
		std::string filename = entry.path().stem().string();
		for (auto& c : filename)
		{
			c = (char)tolower(c);
		}

		remainingFiles.insert(filename);
	}


	for (const auto& object : scene.GetSceneObjects()) 
	{
		const char* uuid = UUIDManager::GetUUIDStringFromID(object.first);

		json transform;
		for (size_t i = 0; i < 16; i++) {
			transform.push_back(object.second->GetTransform().GetDataPtr()[i]);
		}

		Vector3f pos = object.second->GetPosition();
		Vector3f euler = object.second->GetEuler();
		Vector3f scale = object.second->GetScale();

		json jsonobj = {
			{ "name", object.second->GetName()},
			{ "path", object.second->GetPath().GetString()},
			{ "translation", {pos.x, pos.y, pos.z} },
			{ "rotation", {euler.x, euler.y, euler.z} },
			{ "scale", {scale.x, scale.y, scale.z} },
			{ "object-definition", object.second->GetSceneObjectDefinitionName().GetString() },
		};

		for (const SceneProperty& propertyDefinition : object.second->GetPropertyOverrides())
		{
			JsonData value = {};

			if (propertyDefinition.value.HasValue())
				propertyDefinition.value.WriteToJsonWithoutType(value);

			json propertyJson = {
				{ "name", propertyDefinition.name.GetString() },
				{ "type", propertyDefinition.type->GetName().GetString()},
				{ "value", value.json},
			};

			jsonobj["properties"].push_back(propertyJson);
		}
	
		std::filesystem::path objpath = levelDataPath / uuid;

		bool existed = false;
		{
			auto it = remainingFiles.find(uuid);
			if (it != remainingFiles.end())
			{
				fs::permissions(objpath, fs::perms::all);

				remainingFiles.erase(uuid);

				existed = true;
			}
		}

		std::ofstream objout(objpath.string(), std::ios::out | std::ios::trunc);
		objout << jsonobj.dump(2);

		if (afileChangedCallback != nullptr)
		{
			std::string objpathRelative = (levelDataRelativePath / uuid).string();
			std::replace(objpathRelative.begin(), objpathRelative.end(), '\\', '/');
			afileChangedCallback(existed ? SceneFileChangeType::Modify : SceneFileChangeType::Add, objpathRelative.c_str());
		}

		UUIDManager::DeallocateUUIDString(uuid);
	}

	for (const std::string& uuid : remainingFiles)
	{
		std::filesystem::path objpath = levelDataPath / uuid;

		std::filesystem::remove(objpath);

		if (afileChangedCallback != nullptr)
		{
			std::string objpathRelative = (levelDataRelativePath / uuid).string();
			std::replace(objpathRelative.begin(), objpathRelative.end(), '\\', '/');
			afileChangedCallback(SceneFileChangeType::Delete, objpathRelative.c_str());
		}
	}

	json scenejson;


	std::ofstream fout(path, std::ios::trunc);
	fs::permissions(path, fs::perms::all);
	fout << scenejson.dump(2);
}

extern bool Tga::LoadScene(const char* filepath, Scene& scene)
{
	FilePathStream fullPath;
	fullPath << Tga::Settings::GameAssetRoot() << "/" << filepath;
	fullPath.NormalizePath();

	std::filesystem::path resolvedTgsPath(fullPath.GetData());

	scene.SetPath(filepath);
	std::string stem = resolvedTgsPath.stem().string();
	scene.SetName(stem.c_str());
	scene.ClearScene();

	std::filesystem::path dataPath = resolvedTgsPath;
	dataPath.replace_extension(".leveldata");

	if (!fs::exists(dataPath)) // empty scenes can miss the leveldata folder
		return true;

	// directory will be same name but with extension: .leveldata
	for (fs::directory_entry sceneItem : fs::directory_iterator(dataPath))
	{
		if (sceneItem.path().filename().has_extension()) { continue; }
		std::ifstream obj_in(sceneItem.path(), std::ios::in);
		assert(obj_in);
		json item;
		obj_in >> item;
		obj_in.close();

		SceneObject object;
		object.SetName(item.value("name", "unknown").c_str());
		object.SetPath(nullptr, StringRegistry::RegisterOrGetString(item.value("path", "").c_str()));
		
		// Deprecated. Kept around in loading code to support scenes stored with the old format 
		if (item.contains("transform"))
		{
			object.SetTransform(Matrix4x4f(item["transform"].get<std::array<float, 16>>().data()));
		}

		if (item.contains("translation"))
		{
			std::array<float, 3> translation = item["translation"].get<std::array<float, 3>>();
			object.GetTRS().translation = { translation[0], translation[1], translation[2] };
		}

		if (item.contains("rotation"))
		{
			std::array<float, 3> rotation = item["rotation"].get<std::array<float, 3>>();
			object.GetTRS().rotation = { rotation[0], rotation[1], rotation[2] };
		}

		if (item.contains("scale"))
		{
			std::array<float, 3> scale = item["scale"].get<std::array<float, 3>>();
			object.GetTRS().scale = { scale[0], scale[1], scale[2] };
		}

		if (item.contains("object-definition"))
		{
			object.SetSceneObjectDefinitionName(StringRegistry::RegisterOrGetString(item["object-definition"].get<std::string>()));
		}

		std::vector<SceneProperty>& properties = object.EditPropertyOverrides();

		if (item.contains("properties"))
		{
			for (auto& propertyJson : item["properties"])
			{
				SceneProperty propertyDefinition = {};
				propertyDefinition.name = StringRegistry::RegisterOrGetString(propertyJson["name"]);

				// todo: needs error handling if type doesn't exist!
				propertyDefinition.type = PropertyTypeRegistry::GetPropertyType(StringRegistry::RegisterOrGetString(propertyJson["type"]));

				JsonData valueJson = { propertyJson["value"] };
				propertyDefinition.value = Property::CreateFromJson(propertyDefinition.type, valueJson);

				properties.push_back(propertyDefinition);
			}
		}
		scene.CreateSceneObject(sceneItem.path().stem().string().c_str(), object);
	}
	return true;
}
