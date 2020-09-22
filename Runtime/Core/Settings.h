/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include <vector>
//==========================

namespace Spartan
{
    class Context;

    struct ThirdPartyLib
    {
        ThirdPartyLib(const std::string& name, const std::string& version, const std::string& url)
        {
            this->name      = name;
            this->version   = version;
            this->url       = url;
        }

        std::string name;
        std::string version;
        std::string url;
    };

    class SPARTAN_CLASS Settings : public ISubsystem
    {
    public:
        Settings(Context* context);
        ~Settings();

        //= Subsystem =============
        bool Initialize() override;
        //=========================

        //= MISC =======================================================
        bool GetIsFullScreen()      const { return m_is_fullscreen; }
        bool GetIsMouseVisible()    const { return m_is_mouse_visible; }
        bool Loaded()            const { return m_loaded; }
        //==============================================================

        void RegisterThirdPartyLib(const std::string& name, const std::string& version, const std::string& url);
        const auto& GetThirdPartyLibs() const { return m_third_party_libs; }

    private:
        void Save() const;
        void Load();

        void Reflect();
        void Map() const;

        bool m_is_fullscreen                = false;
        bool m_is_mouse_visible             = true;
        uint32_t m_shadow_map_resolution    = 0;
        uint64_t m_renderer_flags           = 0;
        Math::Vector2 m_resolution          = Math::Vector2::Zero;
        uint32_t m_anisotropy               = 0;
        uint32_t m_max_thread_count         = 0;
        double m_fps_limit                  = 0;
        bool m_loaded                       = false;
        Context* m_context                  = nullptr;
        std::vector<ThirdPartyLib> m_third_party_libs;
    };
}
