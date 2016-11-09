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
#include <windows.h>
#include "Context.h"
//==================

class Timer;
class Input;
class PhysicsWorld;
class Scene;
class Renderer;
class Stopwatch;

class __declspec(dllexport) Engine : public Object
{
public:
	Engine(Context* context);
	~Engine();

	// Initializes the engine with a draw handle, an input handle and window instance
	void Initialize(HINSTANCE instance, HWND mainWindowHandle, HWND drawPaneHandle);
	// Performs a complete simulation cycle (used to run your game)
	void Update();
	// Updates and propagates data through the engine's subsystems (used for standalone updates by the editor)
	void LightUpdate();
	// Returns whether the engine is running a full simulation (Update) or not (LightUpdate)
	bool IsSimulating() { return m_isSimulating; }
	// Returns the current context
	Context* GetContext();
	// Shuts down the engine
	void Shutdown();

private:
	Timer* m_timer;
	Input* m_input;
	PhysicsWorld* m_physicsWorld;
	Scene* m_scene;
	Renderer* m_renderer;
	bool m_isSimulating;
};
