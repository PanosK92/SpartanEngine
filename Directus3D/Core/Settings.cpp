//= INCLUDES ========
#include "Settings.h"
//===================

// Initialize static members
bool Settings::m_isFullScreen = false;
VSync Settings::m_vsync = Off;
bool Settings::m_isMouseVisible = true;
int Settings::m_resolutionWidth = 1920;
int Settings::m_resolutionHeight = 1080;
float Settings::m_screenAspect = float(m_resolutionWidth) / float(m_resolutionHeight);
int Settings::m_shadowMapResolution = 2048;
unsigned int Settings::m_anisotropy = 16;

void Settings::SetResolution(int width, int height)
{
	m_resolutionWidth = width;
	m_resolutionHeight = height;
	m_screenAspect = float(m_resolutionWidth) / float(m_resolutionHeight);
}