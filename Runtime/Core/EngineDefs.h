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

#define ENGINE_VERSION "v0.3 alpha"
#define WIN32_LEAN_AND_MEAN

//= DISABLED WARNINGS ===========================================================================
// identifier' : class 'type' needs to have dll-interface to be used by clients of class 'type2'
#pragma warning(disable: 4251) // https://msdn.microsoft.com/en-us/library/esew7y1w.aspx
// non – DLL-interface classkey 'identifier' used as base for DLL-interface classkey 'identifier'
#pragma warning(disable: 4275) // https://msdn.microsoft.com/en-us/library/3tdb471s.aspx
//===============================================================================================

#ifdef ENGINE_RUNTIME
#define ENGINE_CLASS __declspec(dllexport)
#elif ENGINE_EDITOR
#define ENGINE_CLASS __declspec(dllimport)
#endif

namespace Directus
{
	template <typename T>
	void SafeRelease(T& ptr)
	{
		if (ptr)
		{
			ptr->Release();
			ptr = nullptr;
		}
	}

	template <typename T>
	void SafeRelease(T* ptr)
	{
		if (ptr)
		{
			ptr->Release();
			ptr = nullptr;
		}
	}

	template <typename T>
	void SafeDelete(T& ptr)
	{
		if (ptr)
		{
			delete ptr;
			ptr = nullptr;
		}
	}
}