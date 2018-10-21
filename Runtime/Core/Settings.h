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
#include <vector>
//==========================

namespace Directus
{
	enum VSync
	{
		Off,
		Every_VBlank,
		Every_Second_VBlank
	};

	struct DisplayMode
	{
		DisplayMode(unsigned int width, unsigned int height, unsigned int refreshRateNumerator, unsigned int refreshRateDenominator)
		{
			this->width						= width;
			this->height					= height;
			this->refreshRateNumerator		= refreshRateNumerator;
			this->refreshRateDenominator	= refreshRateDenominator;
			this->refreshRate				= (float)refreshRateNumerator / (float)refreshRateDenominator;
		}

		unsigned int width					= 0;
		unsigned int height					= 0;
		unsigned int refreshRateNumerator	= 0;
		unsigned int refreshRateDenominator = 0;
		float refreshRate					= 0;
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

		//= VIEWPORT ===========================================================================================================
		unsigned int Viewport_GetWidth()							{ return (unsigned int)m_viewport.x; }
		unsigned int Viewport_GetHeight()							{ return (unsigned int)m_viewport.y; }
		const Math::Vector2& Viewport_Get()							{ return m_viewport; }
		void Viewport_Set(unsigned int width, unsigned int height)	{ m_viewport = Math::Vector2((float)width, (float)height); }
		//======================================================================================================================

		//= RESOLUTION =================================================================================================================
		void Resolution_Set(int width, int height)				{ m_resolution = Math::Vector2((float)width, (float)height); }
		void Resolution_Set(const Math::Vector2& resolution)	{ m_resolution = resolution; }
		const Math::Vector2& Resolution_Get()					{ return m_resolution; }
		unsigned int Resolution_GetWidth()						{ return (unsigned int)m_resolution.x; }
		unsigned int Resolution_GetHeight()						{ return (unsigned int)m_resolution.y; }
		float AspectRatio_Get()									{ return (float)Resolution_GetWidth() / (float)Resolution_GetHeight(); }
		//==============================================================================================================================

		//= DISPLAY ==========================================================================================================================
		void DisplayMode_Add(unsigned int width, unsigned int height, unsigned int refreshRateNumerator, unsigned int refreshRateDenominator);
		const DisplayMode& DisplayMode_GetFastest();
		//====================================================================================================================================

		//= MISC =========================================================================================
		bool FullScreen_Get()										{ return m_isFullScreen; }
		bool MousVisible_Get()										{ return m_isMouseVisible; }
		VSync VSync_Get()											{ return (VSync)m_vsync; }	
		unsigned int Shadows_GetResolution()						{ return m_shadowMapResolution; }
		unsigned int Anisotropy_Get()								{ return m_anisotropy; }
		float MaxFps_GetGame()										{ return m_maxFPS_game;}
		float MaxFps_GetEditor()									{ return m_maxFPS_editor; }
		void ThreadCountMax_Set(unsigned int maxThreadCount)		{ m_maxThreadCount = maxThreadCount; }
		unsigned int ThreadCountMax_Get()							{ return m_maxThreadCount; }	
		const std::string& Gpu_GetName()							{ return m_gpuName; }
		unsigned int Gpu_GetMemory()								{ return m_gpuMemory; }
		void Gpu_Set(const std::string& name, unsigned int memory);
		//================================================================================================

		// Third party lib versions
		std::string m_versionAngelScript;
		std::string m_versionAssimp;
		std::string m_versionBullet;
		std::string m_versionFMOD;
		std::string m_versionFreeImage;
		std::string m_versionFreeType;
		std::string m_versionImGui;
		std::string m_versionPugiXML = "1.90";
		std::string m_versionVulkan;

	private:
		Math::Vector2 m_viewport;
		Math::Vector2 m_resolution			= Math::Vector2(1920, 1080);
		unsigned int m_vsync				= (int)Off;
		bool m_isFullScreen					= false;
		bool m_isMouseVisible				= true;
		unsigned int m_shadowMapResolution	= 2048;
		unsigned int m_anisotropy			= 16;
		float m_maxFPS_game					= FLT_MAX;
		float m_maxFPS_editor				= 165.0f;
		unsigned int m_maxThreadCount		= 0;
		std::string m_gpuName				= "Unknown";
		unsigned int m_gpuMemory			= 0;
		std::vector<DisplayMode> m_displayModes;
	};
}