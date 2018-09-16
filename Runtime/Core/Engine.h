/* Copyright (c) <2016> <Panos Karabelas>

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE. */

#pragma once

//= INCLUDES =========
#include "Context.h"
#include "SubSystem.h"
//====================

namespace Directus
{
	enum Engine_Mode : unsigned long
	{
		Engine_Update	= 1UL << 0,	// Should the engine update?
		Engine_Physics	= 1UL << 1, // Should the physics update?	
		Engine_Render	= 1UL << 2,	// Should the engine render?
		Engine_Game		= 1UL << 3,	// Is the engine running in game or editor mode?
	};

	class Timer;

	class ENGINE_CLASS Engine : public Subsystem
	{
	public:
		Engine(Context* context);
		~Engine() { Shutdown(); }

		//= SUBSYSTEM =============
		bool Initialize() override;
		//=========================

		// Performs a complete simulation cycle
		void Tick();
		// Shuts down the engine
		void Shutdown();

		//= ENGINE MODE FLAGS  =====================================================================================================
		// Returns all engine mode flags
		static unsigned long EngineMode_GetAll()			{ return m_flags; }
		// Set's all engine mode flags
		static void EngineMode_SetAll(unsigned long flags)	{ m_flags = flags; }
		// Enables an engine mode flag
		static void EngineMode_Enable(Engine_Mode flag)		{ m_flags |= flag; }
		// Removes an engine mode flag
		static void EngineMode_Disable(Engine_Mode flag)	{ m_flags &= ~flag; }
		// Toggles an engine mode flag
		static void EngineMode_Toggle(Engine_Mode flag)		{ m_flags = !EngineMode_IsSet(flag) ? m_flags | flag : m_flags & ~flag;}
		// Returns whether engine mode flag is set
		static bool EngineMode_IsSet(Engine_Mode flag)		{ return m_flags & flag; }
		//==========================================================================================================================

		//= WINDOW ========================================================================
		static void SetHandles(void* drawHandle, void* windowHandle, void* windowInstance);
		static void* GetWindowHandle() { return m_windowHandle; }
		static void* GetWindowInstance() { return m_windowInstance; }
		//=================================================================================

		float GetDeltaTime();

		// Returns the engine's context
		Context* GetContext() { return m_context; }

	private:
		static void* m_drawHandle;	
		static void* m_windowHandle;
		static void* m_windowInstance;
		static unsigned long m_flags;
		Timer* m_timer;
	};
}