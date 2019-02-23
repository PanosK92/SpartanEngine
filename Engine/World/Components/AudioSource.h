/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ==========
#include "IComponent.h"
#include <memory>
#include <string>
//=====================

namespace Directus
{
	class AudioClip;

	class ENGINE_CLASS AudioSource : public IComponent
	{
	public:
		AudioSource(Context* context, Entity* entity, Transform* transform);
		~AudioSource();

		//= INTERFACE ================================
		void OnInitialize() override;
		void OnStart() override;
		void OnStop() override;
		void OnRemove() override;
		void OnTick() override;
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//============================================

		//= PROPERTIES =========================================================
		void SetAudioClip(const std::shared_ptr<AudioClip>& audioClip);
		const std::string& GetAudioClipName();

		bool Play();
		bool Stop();

		bool GetMute() { return m_mute; }
		void SetMute(bool mute);

		bool GetPlayOnStart()					{ return m_playOnStart; }
		void SetPlayOnStart(bool playOnStart)	{ m_playOnStart = playOnStart; }

		bool GetLoop()			{ return m_loop; }
		void SetLoop(bool loop) { m_loop = loop; }

		int GetPriority() { return m_priority; }
		void SetPriority(int priority);

		float GetVolume() { return m_volume; }
		void SetVolume(float volume);

		float GetPitch() { return m_pitch; }
		void SetPitch(float pitch);

		float GetPan() { return m_pan; }
		void SetPan(float pan);
		//======================================================================

	private:
		std::shared_ptr<AudioClip> m_audioClip;
		std::string m_filePath;
		bool m_mute;
		bool m_playOnStart;
		bool m_loop;
		int m_priority;
		float m_volume;
		float m_pitch;
		float m_pan;
		bool m_audioClipLoaded;
	};
}