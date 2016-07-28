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

#define GET_ENGINE_MODE Settings::GetInstance().GetEngineMode()
#define SET_ENGINE_MODE(mode) Settings::GetInstance().SetEngineMode(mode)
#define RESOLUTION_WIDTH Settings::GetInstance().GetResolutionWidth()
#define RESOLUTION Settings::GetInstance().GetResolution()
#define RESOLUTION_HEIGHT Settings::GetInstance().GetResolutionHeight()
#define ASPECT_RATIO Settings::GetInstance().GetScreenAspect()
#define SHADOWMAP_RESOLUTION Settings::GetInstance().GetShadowMapResolution()

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

	void SetEngineMode(EngineMode mode);
	EngineMode GetEngineMode() const;
	bool IsFullScreen() const;
	bool IsMouseVisible() const;
	VSync GetVSync() const;
	void SetResolution(int width, int height);
	Directus::Math::Vector2 GetResolution() const;
	int GetResolutionWidth() const;
	int GetResolutionHeight() const;
	float GetScreenAspect() const;
	int GetShadowMapResolution() const;
	unsigned int GetAnisotropy() const;

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
	m_fullScreen = false;
	m_resolutionWidth = 1920;
	m_resolutionHeight = 1080;
	m_screenAspect = float(m_resolutionWidth) / float(m_resolutionHeight);
	m_shadowMapResolution = 2048;
	m_anisotropy = 16;
	m_vsync = Off;
	m_mouseVisible = true;
	m_engineMode = Editor_Stop;
}

inline Settings::~Settings()
{
}

inline void Settings::SetEngineMode(EngineMode mode)
{
	m_engineMode = mode;
}

inline EngineMode Settings::GetEngineMode() const
{
	return m_engineMode;
}

inline bool Settings::IsFullScreen() const
{
	return m_fullScreen;
}

inline bool Settings::IsMouseVisible() const
{
	return m_mouseVisible;
}

inline VSync Settings::GetVSync() const
{
	return m_vsync;
}

inline void Settings::SetResolution(int width, int height)
{
	m_resolutionWidth = width;
	m_resolutionHeight = height;
	m_screenAspect = float(m_resolutionWidth) / float(m_resolutionHeight);
}

inline Directus::Math::Vector2 Settings::GetResolution() const
{
	return Directus::Math::Vector2(GetResolutionWidth(), GetResolutionHeight());
}

inline int Settings::GetResolutionWidth() const
{
	return m_resolutionWidth;
}

inline int Settings::GetResolutionHeight() const
{
	return m_resolutionHeight;
}

inline float Settings::GetScreenAspect() const
{
	return m_screenAspect;
}

inline int Settings::GetShadowMapResolution() const
{
	return m_shadowMapResolution;
}

inline unsigned int Settings::GetAnisotropy() const
{
	return m_anisotropy;
}
