#include <stdafx.h>
#include "GameSettings.h"

#include <age/settings/settings.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Ag
{
	namespace
	{
		// Paths are stored with forward slashes and, for levels, without the extension.
		std::string Normalise(std::string path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			return path;
		}
	}

	std::string GameSettings::FilePath()
	{
		return (std::filesystem::path(Settings::GameAssetRoot()) / "Game.tgsettings").string();
	}

	bool GameSettings::Load()
	{
		std::ifstream in(FilePath());
		if (!in.is_open())
			return false;
		nlohmann::json j;
		try { in >> j; }
		catch (const std::exception&) { return false; }

		gameName = j.value("gameName", gameName);
		defaultLevel = Normalise(j.value("defaultLevel", std::string()));
		if (defaultLevel.size() > 4 && defaultLevel.compare(defaultLevel.size() - 4, 4, ".tgs") == 0)
			defaultLevel.resize(defaultLevel.size() - 4);
		defaultGameMode = Normalise(j.value("defaultGameMode", std::string()));
		return true;
	}

	bool GameSettings::Save() const
	{
		nlohmann::json j;
		j["gameName"] = gameName;
		j["defaultLevel"] = defaultLevel;
		j["defaultGameMode"] = defaultGameMode;

		std::ofstream out(FilePath());
		if (!out.is_open())
			return false;
		out << j.dump(2) << "\n";
		return true;
	}
}
