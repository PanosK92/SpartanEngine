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
#include "EngineDefs.h"
#include <memory>
//=====================

namespace Directus
{
	class Context;

	enum Engine_Mode : int
	{
		Engine_Tick		= 1UL << 0,	// Should the engine tick?
		Engine_Physics	= 1UL << 1, // Should the physics tick?	
		Engine_Game		= 1UL << 2,	// Is the engine running in game or editor mode?
	};

	class Timer;

	class ENGINE_CLASS Engine
	{
	public:
		Engine(std::shared_ptr<Context> context);
		~Engine();

		// Performs a simulation cycle
		void Tick();

		//  Flag helpers
		static unsigned int EngineMode_GetAll()				{ return m_flags; }
		static void EngineMode_SetAll(unsigned int flags)	{ m_flags = flags; }
		static void EngineMode_Enable(Engine_Mode flag)		{ m_flags |= flag; }
		static void EngineMode_Disable(Engine_Mode flag)	{ m_flags &= ~flag; }
		static void EngineMode_Toggle(Engine_Mode flag)		{ m_flags = !EngineMode_IsSet(flag) ? m_flags | flag : m_flags & ~flag;}
		static bool EngineMode_IsSet(Engine_Mode flag)		{ return m_flags & flag; }

		Context* GetContext() { return m_context.get(); }

	private:
		static unsigned int m_flags;
		std::shared_ptr<Context> m_context;
	};
}