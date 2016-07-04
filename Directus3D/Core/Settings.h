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

#define ENGINE_MODE Settings::GetInstance().GetEngineMode()
#define RESOLUTION_WIDTH Settings::GetInstance().GetResolutionWidth()
#define RESOLUTION Settings::GetInstance().GetResolution()
#define RESOLUTION_HEIGHT Settings::GetInstance().GetResolutionHeight()
#define SHADOWMAP_RESOLUTION Settings::GetInstance().GetShadowMapResolution()

enum EngineMode
{
	Editor_Debug,
	Editor_Play,
	Standalone,
};

enum VSync
{
	Off,
	Every_VBlank,
	Every_Second_VBlank
};

class Settings
{
public:
	Settings();
	~Settings();

	static Settings& GetInstance()
	{
		static Settings instance;
		return instance;
	}

	EngineMode GetEngineMode();
	void SetEngineMode(EngineMode mode);
	bool IsFullScreen();
	bool IsMouseVisible();
	VSync GetVSync();
	void SetResolution(int width, int height);
	Directus::Math::Vector2 GetResolution();
	int GetResolutionWidth();
	int GetResolutionHeight();
	float GetScreenAspect();
	int GetShadowMapResolution();
	unsigned int GetAnisotropy();

private:
	EngineMode m_engineMode;
	bool m_fullScreen;
	VSync m_vsync;
	bool m_mouseVisible;
	int m_resolutionWidth;
	int m_resolutionHeight;
	float m_screenAspect;
	int m_shadowMapResolution;
	unsigned int m_anisotropy;
};

inline Settings::Settings()
{
	m_engineMode = Editor_Debug;
	m_fullScreen = false;
	m_resolutionWidth = 1920;
	m_resolutionHeight = 1080;
	m_screenAspect = float(m_resolutionWidth) / m_resolutionHeight;
	m_shadowMapResolution = 2048;
	m_anisotropy = 16;
	m_vsync = Off;
	m_mouseVisible = true;
}

inline Settings::~Settings()
{
}

inline EngineMode Settings::GetEngineMode()
{
	return m_engineMode;
}

inline void Settings::SetEngineMode(EngineMode mode)
{
	m_engineMode = mode;
}

inline bool Settings::IsFullScreen()
{
	return m_fullScreen;
}

inline bool Settings::IsMouseVisible()
{
	return m_mouseVisible;
}

inline VSync Settings::GetVSync()
{
	return m_vsync;
}

inline void Settings::SetResolution(int width, int height)
{
	m_resolutionWidth = width;
	m_resolutionHeight = height;
	m_screenAspect = float(m_resolutionWidth) / m_resolutionHeight;
}

inline Directus::Math::Vector2 Settings::GetResolution()
{
	return Directus::Math::Vector2(GetResolutionWidth(), GetResolutionHeight());
}

inline int Settings::GetResolutionWidth()
{
	return m_resolutionWidth;
}

inline int Settings::GetResolutionHeight()
{
	return m_resolutionHeight;
}

inline float Settings::GetScreenAspect()
{
	return m_screenAspect;
}

inline int Settings::GetShadowMapResolution()
{
	return m_shadowMapResolution;
}

inline unsigned int Settings::GetAnisotropy()
{
	return m_anisotropy;
}
