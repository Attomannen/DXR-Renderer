#pragma once
#include <tge/math/vector3.h>

namespace Tga
{
	class StringId;

	class Audio
	{
	public:
		Audio();
		~Audio();
		void Init(const char* aPath, Tga::StringId aKey, bool anPlayOnLoad = false, bool aIsLooping = false);
		void Play(Tga::StringId aKey, bool aResetAndPlay = false);
		void SetVolume(Tga::StringId aKey, float aVolume);
		void SetPosition(Tga::StringId aKey, Vector3f aPosition);

		// Stops playing the sample in two ways - by immediately and by
		// set loop flag to 0, therefore next loop is not come
		void Stop(Tga::StringId aKey, bool aImmediately = true);

		float GetLengthInSeconds(Tga::StringId aKey);
		bool IsPlaying(Tga::StringId aKey);

	private:
		Vector3f myPosition;
		float myVolume;
	};
}