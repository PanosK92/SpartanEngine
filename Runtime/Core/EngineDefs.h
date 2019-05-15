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

// Version
#define ENGINE_VERSION "v0.31 WIP"

// APIs
//#define API_GRAPHICS_D3D11
#define API_GRAPHICS_VULKAN
#define API_INPUT_WINDOWS

// Class
#define SPARTAN_CLASS
#if SPARTAN_RUNTIME_SHARED == 1
	#ifdef SPARTAN_RUNTIME
	#define SPARTAN_CLASS __declspec(dllexport)
	#else
	#define SPARTAN_CLASS __declspec(dllimport)
	#endif
#endif

// Windows
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX 

//= DISABLED WARNINGS ==============================================================================================================================
// identifier' : class 'type' needs to have dll-interface to be used by clients of class 'type2'
#pragma warning(disable: 4251) // https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-2-c4275?view=vs-2019
// non – DLL-interface classkey 'identifier' used as base for DLL-interface classkey 'identifier'
#pragma warning(disable: 4275) // https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-2-c4275?view=vs-2019
// no definition for inline function 'function'
#pragma warning(disable: 4506) // https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-1-c4506?view=vs-2017
//==================================================================================================================================================

//= INCLUDES ======
#include <assert.h>
//=================

namespace Spartan
{
	template <typename T>
	constexpr void safe_delete(T& ptr)
	{
		if (ptr)
		{
			delete ptr;
			ptr = nullptr;
		}
	}

	template <typename T>
	constexpr void safe_release(T& ptr)
	{
		if (ptr)
		{
			ptr->Release();
			ptr = nullptr;
		}
	}

	template <typename T>
	constexpr void safe_release(T* ptr)
	{
		if (ptr)
		{
			ptr->Release();
			ptr = nullptr;
		}
	}
}

#define SPARTAN_ASSERT(expression) assert(expression)