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

//= INCLUDES =================
#include "../Core/SubSystem.h"
//============================

//= FORWARD DECLARATIONS =
namespace FMOD
{
	class System;
}
//========================

namespace Directus
{
	class Transform;

	class Audio : public Subsystem
	{
	public:
		Audio(Context* context);
		~Audio();

		// SUBSYSTEM =============
		bool Initialize() override;
		//========================

		bool Update();
		FMOD::System* GetSystemFMOD() { return m_systemFMOD; }
		void SetListenerTransform(Transform* transform);

	private:
		void LogErrorFMOD(int error);

		int m_resultFMOD;
		FMOD::System* m_systemFMOD;
		int m_maxChannels;
		float m_distanceFactor;
		bool m_initialized;
		Transform* m_listener;
	};
}