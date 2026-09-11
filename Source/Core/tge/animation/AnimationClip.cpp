#include <stdafx.h>
#include "AnimationClip.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include <tge/settings/settings.h>

using namespace Tga;


static std::unordered_map<StringId, AnimationClip> locLoadedClips;

AnimationClip* Tga::GetAnimationClip(StringId path)
{
	if (path.IsEmpty())
		return nullptr;

	auto it = locLoadedClips.find(path);
	if (it != locLoadedClips.end())
		return &it->second;

	FilePathStream filePath;

	filePath << Tga::Settings::GameAssetRoot() << "/" << path.GetString();
	if (!fs::exists(filePath.GetStringView()))
		return nullptr;

	AnimationClip& clip = locLoadedClips[path];

	std::ifstream file(filePath.GetStringView().data(), std::ios::in);
	nlohmann::json json;
	file >> json;
	file.close();

	clip.animationSourcePath = StringRegistry::RegisterOrGetString(json.value("animation_source_path", ""));
	clip.previewModelPath = StringRegistry::RegisterOrGetString(json.value("preview_model_path", ""));

	clip.startTime = json.value("start_time", 0.f);
	clip.endTime =json.value("end_time", 0.f);
	clip.playbackRate = json.value("playback_rate", 1.f);
	clip.isLooping = json.value("is_looping", false);

	clip.isSyncronized = json.value("is_sync", false);
	clip.cycleOffsetPercentage = json.value("cycle_offset", 0.f);
	clip.cycleCount = json.value("cycle_count", 1.f);

	return &clip;
}

AnimationClip* Tga::GetOrCreateAnimationClip(StringId path)
{
	AnimationClip* clip = GetAnimationClip(path);

	if (clip == nullptr)
		clip = &locLoadedClips[path];

	return clip;
}


void Tga::SaveAnimationClip(StringId path)
{

	FilePathStream filePath;

	filePath << Tga::Settings::GameAssetRoot() << "/" << path.GetString();

	AnimationClip& clip = locLoadedClips[path];

	nlohmann::json json = {
	{ "animation_source_path", clip.animationSourcePath.GetString()},
	{ "preview_model_path", clip.previewModelPath.GetString()},
	{ "start_time", clip.startTime},
	{ "end_time", clip.endTime},
	{ "playback_rate", clip.playbackRate},

	{ "is_looping", clip.isLooping},
	{ "is_sync", clip.isSyncronized},
	{ "cycle_offset", clip.cycleOffsetPercentage},
	{ "cycle_count", clip.cycleCount},


	};

	std::ofstream fout(filePath.GetStringView().data(), std::ios::trunc);
	fs::permissions(filePath.GetStringView().data(), fs::perms::all);
	fout << json.dump(2);
}