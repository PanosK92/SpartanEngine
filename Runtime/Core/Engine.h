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

//= INCLUDES =======
#include "Context.h"
//==================

#define ENGINE_VERSION "v0.3 alpha"
#define WIN32_LEAN_AND_MEAN

namespace Directus
{
	enum EngineFlags
	{
		Engine_Physics,
		Engine_Update,
		Engine_Render
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

		// Performs a complete simulation cycle (used to run your game)
		void Update();
		// Shuts down the engine
		void Shutdown();

		//= MODE FLAGS  ======================================
		int GetFlags() { return m_flags; }
		void SetFlags(int flags) { m_flags = flags; }
		bool IsUpdating() { return m_flags & Engine_Update; }
		bool IsRendering() { return m_flags & Engine_Render; }
		//====================================================

		//= WINDOW ========================================================================
		static void SetHandles(void* drawHandle, void* windowHandle, void* windowInstance);
		static void* GetWindowHandle() { return m_windowHandle; }
		static void* GetWindowInstance() { return m_windowInstance; }
		//=================================================================================

		// Returns the engine's context
		Context* GetContext() { return m_context; }

	private:
		static void* m_drawHandle;	
		static void* m_windowHandle;
		static void* m_windowInstance;
		
		int m_flags;
		Timer* m_timer;
	};
}