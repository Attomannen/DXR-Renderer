#include "stdafx.h"
#include <tge/audio/audio.h>

#pragma warning(push)
#pragma warning(disable : 4244)
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio/miniaudio.h>
#pragma warning(pop)

#include <tge/stringRegistry/StringRegistry.h>
#include <unordered_map>

using namespace Tga;

	// TODO: if we want to give access to more of the low level interface have a look at (1.1. Low Level API)  https://miniaud.io/docs/manual/index.html#Introduction I focused on providing bindings to the highlevel api with the ma_engine implementation (1.2. High Level API)
	// struct ma_device device; 

#define MAX_SOUNDS 8

struct AudioData {
	struct ma_engine engine;
	std::unordered_map<Tga::StringId, struct ma_sound> soundCache;
} static locAudioData;

Audio::Audio() : myVolume(1.0f)
{
	myPosition = {0.f, 0.f, 0.f};

	ma_result result;
	result = ma_engine_init(NULL, &locAudioData.engine);
	if (result != MA_SUCCESS)
	{
		// TODO: handle errors!
}
}
Audio::~Audio()
{
	ma_engine_uninit(&locAudioData.engine);
}

/// <summary>
/// Initializes a sound from file.
/// </summary>
/// <param name="aPath">Relative path to file (searching EngineAssets, Project's data-folder and cooked assets. If not found the absolute path is searched.</param>
/// <param name="aKey">A key to be used to identify the sound when calling Play, Stop, SetVolume and so on.</param>
/// <param name="anPlayOnLoad"> If the sound should play on load or not. (default: false)</param>
/// <param name="aIsLooping">If the sound should loop (default: false)</param>
void Audio::Init(const char * aPath, Tga::StringId aKey, bool anPlayOnLoad, bool aIsLooping)
{
	ma_result result;

	if(locAudioData.soundCache.find(aKey) == locAudioData.soundCache.end())
{
		FilePathStream resolvedPath;
		if (!Tga::Settings::ResolveAssetPath(aPath, resolvedPath))
			return;

		struct ma_sound* sound = &locAudioData.soundCache[aKey];
		result = ma_sound_init_from_file(&locAudioData.engine, resolvedPath.GetData(), /*ma_uint32 flags*/ 0, /*ma_sound_group*/ NULL, /*ma_fence*/ NULL, sound);
		if (result != MA_SUCCESS)
	{
			// TODO: Handle error!
			return;
	}
		ma_sound_set_looping(sound, aIsLooping);
	
		if (anPlayOnLoad)
		{
			Play(aKey);
		}
}
}

void Audio::Play(Tga::StringId aKey, bool aResetAndPlay)
{
	ma_sound* sound = &locAudioData.soundCache[aKey];

	if(aResetAndPlay)
	{
		ma_sound_seek_to_pcm_frame(sound, 0);
	}
	ma_sound_start(sound);
}

float Audio::GetLengthInSeconds(Tga::StringId aKey)
{
	float len;
	ma_sound* sound = &locAudioData.soundCache[aKey];
	ma_sound_get_length_in_seconds(sound, &len);
	return len;
}

bool Audio::IsPlaying(Tga::StringId aKey)
	{
	return (bool)ma_sound_is_playing(&locAudioData.soundCache[aKey]);
	}

void Tga::Audio::SetVolume(Tga::StringId aKey, float aVolume)
{
	ma_sound* sound = &locAudioData.soundCache[aKey];
	myVolume = aVolume;
	ma_sound_set_volume(sound, myVolume);
}

void Tga::Audio::SetPosition(Tga::StringId aKey, Vector3f aPosition)
{
	ma_sound* sound = &locAudioData.soundCache[aKey];
	myPosition = aPosition;
	ma_sound_set_position(sound, aPosition.x, aPosition.y, 0.f);
}

void Audio::Stop(Tga::StringId aKey, bool aImmediately)
{
	ma_sound* sound = &locAudioData.soundCache[aKey];
	if (aImmediately)
	{
		ma_sound_stop(sound);
	}
	else {
		ma_sound_set_looping(sound, false);
	}
}
