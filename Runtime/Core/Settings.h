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
//==========================

namespace Spartan
{
    class Context;

	enum FPS_Policy
	{
		Fps_Unlocked,
		Fps_Fixed,
		Fps_FixedMonitor
	};

	class SPARTAN_CLASS Settings
	{
	public:
		static Settings& Get()
		{
			static Settings instance;
			return instance;
		}

		Settings();
        ~Settings() { Save(); };

		void Initialize();

		//= FPS ============================================
		void SetFpsLimit(float fps);
		auto GetFpsLimit() const	{ return m_fps_limit; }
		auto GetFpsTarget() const	{ return m_fps_target; }
		auto GetFpsPolicy() const	{ return m_fps_policy; }
		//==================================================

		//= MISC ==============================================================
		auto GetIsFullScreen() const		{ return m_is_fullscreen; }
		auto GetIsMouseVisible() const		{ return m_is_mouse_visible; }
		auto GetShadowResolution() const	{ return m_shadow_map_resolution; }
		auto GetAnisotropy() const			{ return m_anisotropy; }
		auto GetMaxThreadCount() const		{ return m_max_thread_count; }	
		auto GetReverseZ() const			{ return m_reverse_z; }
		//=====================================================================

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
        void Save() const;
		void Load();

		bool m_is_fullscreen				= false;
		bool m_is_mouse_visible				= true;
		uint32_t m_shadow_map_resolution	= 4096;
        Math::Vector2 m_resolution          = Math::Vector2::Zero;
		uint32_t m_anisotropy				= 16;
		uint32_t m_max_thread_count			= 0;
		float m_fps_limit					= 0.0f;
		float m_fps_target					= 165.0f;
		FPS_Policy m_fps_policy				= Fps_Unlocked;
		bool m_reverse_z					= true;      
	};
}
