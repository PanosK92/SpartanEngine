/*
Copyright(c) 2016-2018 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES =====================
#include "../Resource/IResource.h"
#include "../Math/Vector3.h"
//================================

//= FMOD FORWARD DECLARATIONS =
namespace FMOD
{
	class System;
	class Sound;
	class Channel;
}
//=============================

namespace Directus
{
	class Transform;

	enum PlayMode
	{
		Memory,
		Stream
	};

	enum Rolloff
	{
		Linear,
		Custom
	};

	class AudioClip : IResource
	{
	public:
		AudioClip(FMOD::System* fModSystem, Context* context);
		~AudioClip();

		//= IResource ==========================================================
		bool LoadFromFile(const std::string& filePath) override { return true; }
		bool SaveToFile(const std::string& filePath) override { return true; }
		//======================================================================

		bool Load(const std::string& filePath, PlayMode mode);
		bool Play();
		bool Pause();
		bool Stop();

		bool SetLoop(bool loop);

		// Set's the volume [0.0f, 1.0f]
		bool SetVolume(float volume);

		// Sets the mute state effectively silencing it or returning it to its normal volume.
		bool SetMute(bool mute);

		// Set's the priority for the channel [0, 255]
		bool SetPriority(int priority);

		// Sets the pitch value
		bool SetPitch(float pitch);

		// Sets the pan level
		bool SetPan(float pan);

		// Sets the rolloff
		bool SetRolloff(std::vector<Math::Vector3> curvePoints);
		bool SetRolloff(Rolloff rolloff);

		// Makes the audio use the 3D attributes of the transform
		void SetTransform(Transform* transform) { m_transform = transform; }

		// Should be called per frame to update the 3D attributes of the sound
		bool Update();

		bool IsPlaying();

	private:
		//= CREATION ==================================
		bool CreateSound(const std::string& filePath);
		bool CreateStream(const std::string& filePath);
		//=============================================
		int BuildSoundMode();

		Transform* m_transform;
		FMOD::System* m_fModSystem;
		int m_result;
		FMOD::Sound* m_sound;
		FMOD::Channel* m_channel;
		PlayMode m_playMode;
		int m_modeLoop;
		float m_minDistance;
		float m_maxDistance;
		int m_modeRolloff;
	};
}