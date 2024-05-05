/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES ===========
#include "Definitions.h"
//======================

namespace Spartan
{
    struct third_party_lib
    {
        third_party_lib(const std::string& name, const std::string& version, const std::string& url)
        {
            this->name    = name;
            this->version = version;
            this->url     = url;
        }

        std::string name;
        std::string version;
        std::string url;
    };

    class SP_CLASS Settings
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void RegisterThirdPartyLib(const std::string& name, const std::string& version, const std::string& url);
        static const std::vector<third_party_lib>& GetThirdPartyLibs();
        static bool HasLoadedUserSettingsFromFile();
    };
}
