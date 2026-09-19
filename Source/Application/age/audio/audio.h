#pragma once
#include <age/math/vector3.h>

namespace Ag
{
	class StringId;

	class Audio
	{
	public:
		Audio();
		~Audio();
		void Init(const char* aPath, Ag::StringId aKey, bool anPlayOnLoad = false, bool aIsLooping = false);
		void Play(Ag::StringId aKey, bool aResetAndPlay = false);
		void SetVolume(Ag::StringId aKey, float aVolume);
		void SetPosition(Ag::StringId aKey, Vector3f aPosition);

		// Stops playing the sample in two ways - by immediately and by
		// set loop flag to 0, therefore next loop is not come
		void Stop(Ag::StringId aKey, bool aImmediately = true);

		float GetLengthInSeconds(Ag::StringId aKey);
		bool IsPlaying(Ag::StringId aKey);

	private:
		Vector3f myPosition;
		float myVolume;
	};
}