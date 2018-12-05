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
	#define REVERSE_Z 1

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

	struct DisplayAdapter
	{
		DisplayAdapter(const std::string& name, unsigned int memory, unsigned int vendorID, void* data)
		{
			this->name		= name;
			this->memory	= memory;
			this->vendorID	= vendorID;
			this->data		= data;
		}

		std::string name		= "Unknown";
		unsigned int vendorID	= 0;
		unsigned int memory		= 0;
		void* data				= nullptr;

		//Nvidia: 0x10DE
		//AMD	: 0x1002, 0x1022
		//Intel : 0x163C, 0x8086, 0x8087
	};

	enum FPS_Policy
	{
		FPS_Unlocked,
		FPS_Locked,
		FPS_MonitorMatch
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

		//= VIEWPORT =================================================================================
		float Viewport_GetWidth()						{ return m_viewport.x; }
		float Viewport_GetHeight()						{ return m_viewport.y; }
		const Math::Vector2& Viewport_Get()				{ return m_viewport; }
		void Viewport_Set(float width, float height)	{ m_viewport = Math::Vector2(width, height); }
		//============================================================================================

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

		//= ADAPTERS ===============================================================================================
		void DisplayAdapter_Add(const std::string& name, unsigned int memory, unsigned int vendorID, void* adapter);
		void DisplayAdapter_SetPrimary(const DisplayAdapter* primary);
		const std::vector<DisplayAdapter>& DisplayAdapters_Get() { return m_displayAdapters; }
		//==========================================================================================================

		//= FPS =====================================
		void FPS_SetLimit(float fps);
		float FPS_GetLimit() { return m_fpsLimit; }
		float FPS_GetTarget() { return m_fpsTarget; }
		//===========================================

		//= MISC =========================================================================================
		bool FullScreen_Get()										{ return m_isFullScreen; }
		bool MousVisible_Get()										{ return m_isMouseVisible; }
		VSync VSync_Get()											{ return (VSync)m_vsync; }	
		unsigned int Shadows_GetResolution()						{ return m_shadowMapResolution; }
		unsigned int Anisotropy_Get()								{ return m_anisotropy; }
		void ThreadCountMax_Set(unsigned int maxThreadCount)		{ m_maxThreadCount = maxThreadCount; }
		unsigned int ThreadCountMax_Get()							{ return m_maxThreadCount; }	
		const std::string& Gpu_GetName()							{ return m_primaryAdapter->name; }
		unsigned int Gpu_GetMemory()								{ return m_primaryAdapter->memory; }
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
		Math::Vector2 m_resolution				= Math::Vector2(1920, 1080);
		unsigned int m_vsync					= (int)Off;
		bool m_isFullScreen						= false;
		bool m_isMouseVisible					= true;
		unsigned int m_shadowMapResolution		= 4096;
		unsigned int m_anisotropy				= 16;
		unsigned int m_maxThreadCount			= 0;
		float m_fpsLimit						= -1.0f;
		float m_fpsTarget						= 165.0f;
		FPS_Policy m_fpsPolicy					= FPS_MonitorMatch;

		const DisplayAdapter* m_primaryAdapter	= nullptr;
		std::vector<DisplayMode> m_displayModes;
		std::vector<DisplayAdapter> m_displayAdapters;	
	};
}