//= INCLUDES ========
#include "Settings.h"
//===================

// Initialize static members
EngineMode Settings::m_engineMode = Editor_Stop;
bool Settings::m_fullScreen = false;
VSync Settings::m_vsync = Off;
bool Settings::m_mouseVisible = true;
int Settings::m_resolutionWidth = 1920;
int Settings::m_resolutionHeight = 1080;
float Settings::m_screenAspect = float(m_resolutionWidth) / float(m_resolutionHeight);
int Settings::m_shadowMapResolution = 2048;
unsigned int Settings::m_anisotropy = 16;

void Settings::SetEngineMode(EngineMode mode)
{
	m_engineMode = mode;
}

EngineMode Settings::GetEngineMode()
{
	return m_engineMode;
}

bool Settings::IsFullScreen()
{
	return m_fullScreen;
}

bool Settings::IsMouseVisible()
{
	return m_mouseVisible;
}

VSync Settings::GetVSync()
{
	return m_vsync;
}

void Settings::SetResolution(int width, int height)
{
	m_resolutionWidth = width;
	m_resolutionHeight = height;
	m_screenAspect = float(m_resolutionWidth) / float(m_resolutionHeight);
}

Directus::Math::Vector2 Settings::GetResolution()
{
	return Directus::Math::Vector2(GetResolutionWidth(), GetResolutionHeight());
}

int Settings::GetResolutionWidth()
{
	return m_resolutionWidth;
}

int Settings::GetResolutionHeight()
{
	return m_resolutionHeight;
}

float Settings::GetScreenAspect()
{
	return m_screenAspect;
}

int Settings::GetShadowMapResolution()
{
	return m_shadowMapResolution;
}

unsigned Settings::GetAnisotropy()
{
	return m_anisotropy;
}
