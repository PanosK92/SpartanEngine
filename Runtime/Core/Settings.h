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
#include "ISubsystem.h"
#include "../Math/Vector2.h"
//==========================

namespace Spartan
{
    class Context;

	class SPARTAN_CLASS Settings : public ISubsystem
	{
	public:
		Settings(Context* context);
        ~Settings();

        //= Subsystem =============
        bool Initialize() override;
        //=========================

		//= MISC =====================================================
		auto GetIsFullScreen() const	{ return m_is_fullscreen; }
		auto GetIsMouseVisible() const	{ return m_is_mouse_visible; }
		//============================================================

		// Third party lib versions
		std::string m_versionAngelScript;
		std::string m_versionAssimp;
		std::string m_versionBullet;
		std::string m_versionFMOD;
		std::string m_versionFreeImage;
		std::string m_versionFreeType;
		std::string m_versionImGui;
        std::string m_versionPugiXML;
		std::string m_versionGraphicsAPI;

    private:
        void Save() const;
		void Load();

        void Reflect();
        void Map();

		bool m_is_fullscreen				= false;
		bool m_is_mouse_visible				= true;
		uint32_t m_shadow_map_resolution	= 0;
        uint32_t m_renderer_flags           = 0;
        Math::Vector2 m_resolution          = Math::Vector2::Zero;
		uint32_t m_anisotropy				= 0;
		uint32_t m_max_thread_count			= 0;
        double m_fps_limit                  = 0;
        Context* m_context                  = nullptr;
	};
}
