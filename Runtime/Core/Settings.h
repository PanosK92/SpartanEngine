/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ===================
#include <vector>
#include "EngineDefs.h"
#include "../Math/Vector2.h"
#include "../Math/Vector4.h"
#include "../RHI/RHI_Viewport.h"
//==============================

namespace Directus
{
	#define REVERSE_Z 1

	struct DisplayMode
	{
		DisplayMode() {}
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

		//= WINDOW ================================================================
		void SetHandles(void* drawHandle, void* windowHandle, void* windowInstance)
		{
			m_drawHandle		= drawHandle;
			m_windowHandle		= windowHandle;
			m_windowInstance	= windowInstance;
		}
		void* GetWindowHandle()		{ return m_windowHandle; }
		void* GetWindowInstance()	{ return m_windowInstance; }
		//=========================================================================

		//= WINDOW SIZE ==========================================================================================================
		void SetWindowSize(unsigned int width, unsigned int height)	{ m_windowSize = Math::Vector2((float)width, (float)height); }
		void SetWindowSize(const Math::Vector2& size)				{ m_windowSize = size; }
		const Math::Vector2& GetWindowSize()						{ return m_windowSize; }
		unsigned int GetWindowWidth()								{ return (unsigned int)m_windowSize.x; }
		unsigned int GetWindowHeight()								{ return (unsigned int)m_windowSize.y; }
		//========================================================================================================================

		//= DISPLAY ==========================================================================================================================
		void DisplayMode_Add(unsigned int width, unsigned int height, unsigned int refreshRateNumerator, unsigned int refreshRateDenominator);
		bool DisplayMode_GetFastest(DisplayMode* displayMode);
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

		//= MISC ====================================================================================================================
		bool FullScreen_Get()									{ return m_isFullScreen; }
		bool MousVisible_Get()									{ return m_isMouseVisible; }
		unsigned int Shadows_GetResolution()					{ return m_shadowMapResolution; }
		unsigned int Anisotropy_Get()							{ return m_anisotropy; }
		void ThreadCountMax_Set(unsigned int maxThreadCount)	{ m_maxThreadCount = maxThreadCount; }
		unsigned int ThreadCountMax_Get()						{ return m_maxThreadCount; }	
		const std::string& Gpu_GetName()						{ return m_primaryAdapter ? m_primaryAdapter->name : m_emptyString; }
		unsigned int Gpu_GetMemory()							{ return m_primaryAdapter ? m_primaryAdapter->memory : 0; }
		//===========================================================================================================================

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
		void* m_drawHandle					= nullptr;
		void* m_windowHandle				= nullptr;
		void* m_windowInstance				= nullptr;
		Math::Vector2 m_windowSize			= Math::Vector2(1920, 1080);
		bool m_isFullScreen					= false;
		bool m_isMouseVisible				= true;
		unsigned int m_shadowMapResolution	= 4096;
		unsigned int m_anisotropy			= 16;
		unsigned int m_maxThreadCount		= 0;
		float m_fpsLimit					= -1.0f;
		float m_fpsTarget					= 165.0f;
		FPS_Policy m_fpsPolicy				= FPS_MonitorMatch;

		const DisplayAdapter* m_primaryAdapter = nullptr;
		std::vector<DisplayMode> m_displayModes;
		std::vector<DisplayAdapter> m_displayAdapters;	
		std::string m_emptyString = "N/A";
	};
}