#include "stdafx.h"

#include <fstream>

#include "ScriptManager.h"
#include "Script.h"
#include "JsonData.h"

#include <tge/settings/Settings.h>

using namespace Tga;

struct InternalScriptData
{
	std::unique_ptr<Script> script;
	std::shared_ptr<const Script> latestSnapshot;
	int latestSnapshotSequenceNumber = -1;
};

static std::unordered_map<std::string_view, InternalScriptData> myLoadedScripts;

std::shared_ptr<const Script> Tga::ScriptManager::GetScript(std::string_view name)
{
	if (!GetEditableScript(name)) {
		return nullptr;
	}

	InternalScriptData& data = myLoadedScripts[name];

	int sequenceNumber = data.script->GetSequenceNumber();
	if (data.latestSnapshotSequenceNumber == sequenceNumber) {
		return data.latestSnapshot;
	}

	std::shared_ptr<Script> newSnapshot = std::make_shared<Script>();
	JsonData json;
	data.script->WriteToJson(json);
	newSnapshot->LoadFromJson(json);
	newSnapshot->SetSequenceNumber(data.script->GetSequenceNumber());

	data.latestSnapshotSequenceNumber = sequenceNumber;
	data.latestSnapshot = newSnapshot;

	return newSnapshot;
}

Script* ScriptManager::GetEditableScript(std::string_view path) 
{
	auto it = myLoadedScripts.find(path);
	if (it != myLoadedScripts.end()) {
		return it->second.script.get();
	}

	FilePathStream fullPath;
	fullPath << Tga::Settings::GameAssetRoot() << "/" << path << ".tgscript";
	fullPath.NormalizePath();

	std::ifstream file(fullPath.GetData());
	if (!file.is_open()) 
	{
		return nullptr;
	}

	JsonData data = { nlohmann::json::parse(file) };
	file.close();

	StringId nameStringId = StringRegistry::RegisterOrGetString(path);
	std::string_view nameStringView = nameStringId.GetString();

	myLoadedScripts.insert({ nameStringView, InternalScriptData{std::make_unique<Script>(), nullptr, -1} });
	Script& script = *myLoadedScripts[nameStringView].script;

	script.LoadFromJson(data);

	return &script;
}

void ScriptManager::AddEditableScript(std::string_view name, std::unique_ptr<Script> && script)
{
	StringId nameStringId = StringRegistry::RegisterOrGetString(name);
	std::string_view nameStringView = nameStringId.GetString();

	myLoadedScripts.insert({ nameStringView, InternalScriptData {std::move(script), nullptr, -1} });
}
