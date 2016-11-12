/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ==============
#include "Audio.h"
#include "fmod_errors.h"
#include "../Logging/Log.h"
//=========================

Audio::Audio(Context* context) : Object(context)
{
	m_fmodSystem = nullptr;
	m_maxChannels = 32;
}

Audio::~Audio()
{

}

bool Audio::Initialize()
{
	m_result = m_fmodSystem->init(m_maxChannels, FMOD_INIT_NORMAL, nullptr);

	if (m_result != FMOD_OK)
		LOG_ERROR(FMOD_ErrorString(m_result));

	return true;
}

void Audio::Update()
{
	if (!m_fmodSystem)
		return;

	m_result = m_fmodSystem->update();

	if (m_result != FMOD_OK)
		LOG_ERROR(FMOD_ErrorString(m_result));
}
