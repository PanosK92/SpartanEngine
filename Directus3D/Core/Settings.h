/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ===============
#include "../Math/Vector2.h"
//==========================

#define GET_ENGINE_MODE Settings::GetEngineMode()
#define SET_ENGINE_MODE(mode) Settings::SetEngineMode(mode)
#define SET_RESOLUTION(x,y) Settings::SetResolution(x,y)
#define GET_RESOLUTION Settings::GetResolution()
#define RESOLUTION_WIDTH Settings::GetResolutionWidth()
#define RESOLUTION_HEIGHT Settings::GetResolutionHeight()
#define ASPECT_RATIO Settings::GetScreenAspect()
#define SHADOWMAP_RESOLUTION Settings::GetShadowMapResolution()
#define VSYNC Settings::GetVSync()
#define FULLSCREEN Settings::IsFullScreen()
#define ANISOTROPY Settings::GetAnisotropy()

enum EngineMode
{
	Editor_Play,
	Editor_Stop,
	Editor_Pause,
	Build_Developer,
	Build_Release
};

enum VSync
{
	Off,
	Every_VBlank,
	Every_Second_VBlank
};

class __declspec(dllexport) Settings
{
public:
	static void SetEngineMode(EngineMode mode);
	static EngineMode GetEngineMode();
	static bool IsFullScreen();
	static bool IsMouseVisible();
	static VSync GetVSync();
	static void SetResolution(int width, int height);
	static Directus::Math::Vector2 GetResolution();
	static int GetResolutionWidth();
	static int GetResolutionHeight();
	static float GetScreenAspect();
	static int GetShadowMapResolution();
	static unsigned int GetAnisotropy();

private:
	static EngineMode m_engineMode;
	static bool m_fullScreen;
	static VSync m_vsync;
	static bool m_mouseVisible;
	static int m_resolutionWidth;
	static int m_resolutionHeight;
	static float m_screenAspect;
	static int m_shadowMapResolution;
	static unsigned int m_anisotropy;
};