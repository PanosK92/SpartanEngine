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
//==================

class Socket;
class ScriptEngine;
class Renderer;
class ImageLoader;
class ModelLoader;
class D3D11Device;
class Scene;
class Input;
class Timer;
class PhysicsEngine;
class MeshPool;
class MaterialPool;
class TexturePool;
class ShaderPool;

class __declspec(dllexport) Engine
{
public:
	Engine(HINSTANCE instance, HWND mainWindowHandle, HWND drawPaneHandle);
	~Engine();

	void Run();
	Socket* GetSocket();

private:
	void Initialize(HINSTANCE instance, HWND mainWindowHandle, HWND drawPaneHandle);
	void Shutdown();

	/*------------------------------------------------------------------------------
									[COMPONENTS]
	------------------------------------------------------------------------------*/
	Socket* m_engineSocket;
	ScriptEngine* m_scriptEngine;
	Renderer* m_renderer;
	ImageLoader* m_imageLoader;
	ModelLoader* m_modelLoader;
	D3D11Device* m_D3D11Device;
	Scene* m_scene;
	Input* m_input;
	Timer* m_timer;
	PhysicsEngine* m_physicsEngine;
	MeshPool* m_meshPool;
	MaterialPool* m_materialPool;
	TexturePool* m_texturePool;
	ShaderPool* m_shaderPool;
};
