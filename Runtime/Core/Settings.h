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

//= INCLUDES ===============
#include "EngineDefs.h"
#include "../Math/Vector2.h"
#include "../Math/Vector4.h"
//==========================

namespace Directus
{
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

		//= WINDOW ===================================================================
		void SetHandles(void* draw_handle, void* window_handle, void* window_instance)
		{
			m_drawHandle		= draw_handle;
			m_windowHandle		= window_handle;
			m_windowInstance	= window_instance;
		}
		void* GetWindowHandle() const	{ return m_windowHandle; }
		void* GetWindowInstance() const { return m_windowInstance; }
		//============================================================================

		//= WINDOW SIZE ================================================================================================================================================
		void SetWindowSize(const unsigned int width, const unsigned int height)	{ m_windowSize = Math::Vector2(static_cast<float>(width), static_cast<float>(height)); }
		void SetWindowSize(const Math::Vector2& size)							{ m_windowSize = size; }
		const Math::Vector2& GetWindowSize() const								{ return m_windowSize; }
		unsigned int GetWindowWidth() const										{ return static_cast<unsigned int>(m_windowSize.x); }
		unsigned int GetWindowHeight() const									{ return static_cast<unsigned int>(m_windowSize.y); }
		//==============================================================================================================================================================

		//= FPS ===========================================
		void SetFpsLimit(float fps);
		float GetFpsLimit() const	{ return m_fpsLimit; }
		float GetFpsTarget() const	{ return m_fpsTarget; }
		FPS_Policy GetFpsPolicy()	{ return m_fpsPolicy; }
		//=================================================

		//= MISC =================================================================================
		bool GetIsFullScreen() const						{ return m_isFullScreen; }
		bool GetIsMouseVisible() const						{ return m_isMouseVisible; }
		unsigned int GetShadowResolution() const			{ return m_shadowMapResolution; }
		unsigned int GetAnisotropy() const					{ return m_anisotropy; }
		void SetMaxThreadCount(unsigned int maxThreadCount)	{ m_maxThreadCount = maxThreadCount; }
		unsigned int GetMaxThreadCount() const				{ return m_maxThreadCount; }	
		bool GetReverseZ() const							{ return m_reverseZ; }
		//========================================================================================

		// Third party lib versions
		std::string m_versionAngelScript;
		std::string m_versionAssimp;
		std::string m_versionBullet;
		std::string m_versionFMOD;
		std::string m_versionFreeImage;
		std::string m_versionFreeType;
		std::string m_versionImGui;
		std::string m_versionPugiXML = "1.90";
		std::string m_versionGraphicsAPI;

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
		bool m_reverseZ						= true;
	};
}