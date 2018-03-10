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
	//= RESOLUTION ==============================================================
	#define SET_RESOLUTION(resolution)	Settings::Get().SetResolution(resolution)
	#define GET_RESOLUTION				Settings::Get().GetResolution()
	#define RESOLUTION_WIDTH			Settings::Get().GetResolutionWidth()
	#define RESOLUTION_HEIGHT			Settings::Get().GetResolutionHeight()
	//===========================================================================

	// VIEWPORT =========================================================
	#define SET_VIEWPORT(viewport)	Settings::Get().SetViewport(viewport)
	#define GET_VIEWPORT			Settings::Get().GetViewport()
	#define VIEWPORT_WIDTH			Settings::Get().GetViewportWidth()
	#define VIEWPORT_HEIGHT			Settings::Get().GetViewportHeight()
	//===================================================================

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

		//= RESOLUTION =========================================================================================
		void SetResolution(int width, int height) { m_resolution = Math::Vector2((float)width, (float)height); }
		void SetResolution(const Math::Vector2& resolution) { m_resolution = resolution; }
		const Math::Vector2& GetResolution() { return m_resolution; }
		int GetResolutionWidth() { return (int)m_resolution.x; }
		int GetResolutionHeight() { return (int)m_resolution.y; }
		//======================================================================================================

		//= VIEWPORT =========================================================================================================================
		void SetViewport(int x, int y, int width, int height) { m_viewport = Math::Vector4((float)x, (float)y, (float)width, (float)height); }
		void SetViewport(const Math::Vector4& viewport) { m_viewport = viewport; }
		const Math::Vector4& GetViewport() { return m_viewport; }
		float GetViewportWidth() { return m_viewport.z; }
		float GetViewportHeight() { return m_viewport.w; }
		//====================================================================================================================================

		//= OTHER ================================================================================
		bool IsFullScreen() { return m_isFullScreen; }
		bool IsMouseVisible() { return m_isMouseVisible; }
		VSync GetVSync() { return (VSync)m_vsync; }
		float GetScreenAspect() { return (float)GetViewportWidth() / (float)GetViewportHeight(); }
		int GetShadowMapResolution() { return m_shadowMapResolution; }
		unsigned int GetAnisotropy() { return m_anisotropy; }
		float GetMaxFPS() { return m_maxFPS;}
		//========================================================================================

		// Third party lib versions
		std::string g_versionAngelScript;
		std::string g_versionAssimp;
		std::string g_versionBullet;
		std::string g_versionFMOD;
		std::string g_versionFreeImage;
		std::string g_versionFreeType;
		std::string g_versionImGui;
		std::string g_versionPugiXML;
		std::string g_versionSDL;

	private:
		bool m_isFullScreen;
		Math::Vector2 m_resolution;
		Math::Vector4 m_viewport;
		int m_vsync;
		bool m_isMouseVisible;		
		int m_shadowMapResolution;
		unsigned int m_anisotropy;	
		float m_maxFPS;
	};
}