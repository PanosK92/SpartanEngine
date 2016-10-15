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
#include <memory>
//==================

class Socket;
class ScriptEngine;
class Renderer;
class ModelImporter;
class Graphics;
class Scene;
class Input;
class Timer;
class PhysicsWorld;
class MeshPool;
class MaterialPool;
class TexturePool;
class ShaderPool;
class Stopwatch;
class ThreadPool;

class __declspec(dllexport) Engine
{
public:
	Engine();
	~Engine();

	void Initialize(HINSTANCE instance, HWND mainWindowHandle, HWND drawPaneHandle);
	void Update();
	void Shutdown();
	std::shared_ptr<Socket> GetSocket();

private:
	/*------------------------------------------------------------------------------
									[COMPONENTS]
	------------------------------------------------------------------------------*/
	std::shared_ptr<Socket> m_engineSocket;
	std::shared_ptr<ScriptEngine> m_scriptEngine;
	std::shared_ptr<Renderer> m_renderer;
	std::shared_ptr<ModelImporter> m_modelLoader;
	std::shared_ptr<Graphics> m_graphics;
	std::shared_ptr<Scene> m_scene;
	std::shared_ptr<Input> m_input;
	std::shared_ptr<Timer> m_timer;
	std::shared_ptr<PhysicsWorld> m_physicsWorld;
	std::shared_ptr<MeshPool> m_meshPool;
	std::shared_ptr<MaterialPool> m_materialPool;
	std::shared_ptr<TexturePool> m_texturePool;
	std::shared_ptr<ShaderPool> m_shaderPool;
	std::shared_ptr<ThreadPool> m_threadPool;
};
