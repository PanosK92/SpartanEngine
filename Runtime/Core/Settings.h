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

//= INCLUDES ======
#include "Helper.h"
#include <fstream>
//=================

namespace Directus
{
//= RESOLUTION =======================================================
#define SET_RESOLUTION(resolution) Settings::SetResolution(resolution)
#define GET_RESOLUTION				Settings::GetResolution()
#define RESOLUTION_WIDTH			Settings::GetResolutionWidth()
#define RESOLUTION_HEIGHT			Settings::GetResolutionHeight()
//====================================================================

// VIEWPORT ===================================================
#define SET_VIEWPORT(viewport)	Settings::SetViewport(viewport)
#define GET_VIEWPORT			Settings::GetViewport()
#define VIEWPORT_WIDTH			Settings::GetResolutionWidth()
#define VIEWPORT_HEIGHT			Settings::GetResolutionHeight()
//=============================================================

//= OTHER ========================================================
#define ASPECT_RATIO			Settings::GetScreenAspect()
#define SHADOWMAP_RESOLUTION	Settings::GetShadowMapResolution()
#define VSYNC					Settings::GetVSync()
#define FULLSCREEN_ENABLED		Settings::IsFullScreen()
#define ANISOTROPY_LEVEL		Settings::GetAnisotropy()
//================================================================

	namespace Math
	{
		class Vector2;
		class Vector4;
	}

	enum VSync
	{
		Off,
		Every_VBlank,
		Every_Second_VBlank
	};

	class ENGINE_API Settings
	{
	public:
		static void Initialize();

		//= RESOLUTION =====================================================
		static void SetResolution(int width, int height);
		static void SetResolution(const Math::Vector2& resolution);
		static const Math::Vector2& GetResolution() { return m_resolution; }
		static int GetResolutionWidth();
		static int GetResolutionHeight();
		//==================================================================

		//= VIEWPORT ===================================================
		static void SetViewport(int x, int y, int width, int height);
		static void SetViewport(const Math::Vector4& viewport);
		static const Math::Vector4& GetViewport() { return m_viewport; }
		static int GetViewportWidth();
		static int GetViewportHeight();
		//==============================================================

		//= OTHER ===========================================================
		static bool IsFullScreen() { return m_isFullScreen; }
		static bool IsMouseVisible() { return m_isMouseVisible; }
		static VSync GetVSync() { return (VSync)m_vsync; }
		static float GetScreenAspect();
		static int GetShadowMapResolution() { return m_shadowMapResolution; }
		static unsigned int GetAnisotropy() { return m_anisotropy; }
		//===================================================================

	private:
		static std::ofstream m_fout;
		static std::ifstream m_fin;
		static std::string m_settingsFileName;

		static bool m_isFullScreen;
		static Math::Vector2 m_resolution;
		static Math::Vector4 m_viewport;
		static int m_vsync;
		static bool m_isMouseVisible;		
		static int m_shadowMapResolution;
		static unsigned int m_anisotropy;	
	};
}