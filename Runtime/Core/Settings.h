/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "EngineDefs.h"
#include "../Math/Vector2.h"
#include "../Math/Vector4.h"
//==========================

namespace Directus
{
	//= DISPLAY =========================================================================
	#define SET_DISPLAY_SIZE(width, height)	Settings::Get().SetDisplaySize(width, height)
	#define GET_DISPLAY_SIZE				Settings::Get().GetDisplaySize()
	#define DISPLAY_WIDTH					Settings::Get().GetDisplayWidth()
	#define DISPLAY_HEIGHT					Settings::Get().GetDisplayHeight()
	//===================================================================================

	//= RESOLUTION ==============================================================
	#define SET_RESOLUTION(resolution)	Settings::Get().SetResolution(resolution)
	#define GET_RESOLUTION				Settings::Get().GetResolution()
	#define RESOLUTION_WIDTH			Settings::Get().GetResolutionWidth()
	#define RESOLUTION_HEIGHT			Settings::Get().GetResolutionHeight()
	//===========================================================================

	//= OTHER ==============================================================
	#define ASPECT_RATIO			Settings::Get().GetScreenAspect()
	#define SHADOWMAP_RESOLUTION	Settings::Get().GetShadowMapResolution()
	#define VSYNC					Settings::Get().GetVSync()
	#define FULLSCREEN_ENABLED		Settings::Get().IsFullScreen()
	#define ANISOTROPY_LEVEL		Settings::Get().GetAnisotropy()
	//======================================================================

	enum VSync
	{
		Off,
		Every_VBlank,
		Every_Second_VBlank
	};

	class ENGINE_CLASS Settings
	{
	public:
		static Settings& Get()
		{
			static Settings instance;
			return instance;
		}

		Settings();

		void Initialize();

		//= DISPLAY ==============================================================================================
		int GetDisplayWidth() { return (int)m_displaySize.x; }
		int GetDisplayHeight() { return (int)m_displaySize.y; }
		const Math::Vector2& GetDisplaySize() { return m_displaySize; }
		void SetDisplaySize(int width, int height) { m_displaySize = Math::Vector2((float)width, (float)height); }
		//========================================================================================================

		//= RESOLUTION =========================================================================================
		void SetResolution(int width, int height) { m_resolution = Math::Vector2((float)width, (float)height); }
		void SetResolution(const Math::Vector2& resolution) { m_resolution = resolution; }
		const Math::Vector2& GetResolution() { return m_resolution; }
		int GetResolutionWidth() { return (int)m_resolution.x; }
		int GetResolutionHeight() { return (int)m_resolution.y; }
		//======================================================================================================
	

		//= OTHER ====================================================================================
		bool IsFullScreen() { return m_isFullScreen; }
		bool IsMouseVisible() { return m_isMouseVisible; }
		VSync GetVSync() { return (VSync)m_vsync; }
		float GetScreenAspect() { return (float)GetResolutionWidth() / (float)GetResolutionHeight(); }
		int GetShadowMapResolution() { return m_shadowMapResolution; }
		unsigned int GetAnisotropy() { return m_anisotropy; }
		float GetMaxFPS() { return m_maxFPS;}
		//============================================================================================

		// Third party lib versions
		std::string m_versionAngelScript;
		std::string m_versionAssimp;
		std::string m_versionBullet;
		std::string m_versionFMOD;
		std::string m_versionFreeImage;
		std::string m_versionFreeType;
		std::string m_versionImGui;
		std::string m_versionPugiXML;

	private:
		bool m_isFullScreen;
		// The display size in pixels
		Math::Vector2 m_displaySize;
		// The rendering resolution
		Math::Vector2 m_resolution;	
		int m_vsync;
		bool m_isMouseVisible;		
		int m_shadowMapResolution;
		unsigned int m_anisotropy;	
		float m_maxFPS;
	};
}