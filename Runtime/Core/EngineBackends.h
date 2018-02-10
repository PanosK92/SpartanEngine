/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= RENDERING ===
#define API_D3D11
//===============

//= INPUT ============
#if defined(API_D3D11)
#define API_DInput
#endif
//====================

namespace Directus
{
//= RENDERING ====================================
#if defined(API_D3D11)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#include "../Graphics/D3D11/D3D11GraphicsDevice.h"
	class D3D11GraphicsDevice;
	typedef D3D11GraphicsDevice Graphics;
#elif defined(API_VULKAN)
#include "VULKAN/VulkanGraphicsDevice.h"
	class VULKANGraphicsDevice;
	typedef VULKANGraphicsDevice Graphics;
#endif
//================================================

//= INPUT =========================
#if defined(API_DInput)
#pragma comment(lib, "dinput8.lib")
#define DIRECTINPUT_VERSION 0x0800
#endif
//=================================
}