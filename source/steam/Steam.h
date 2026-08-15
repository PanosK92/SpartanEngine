/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ====
#include <cstdint>
#include <string>
#include <vector>
//===============

namespace spartan
{
    class Steam
    {
    public:
        // lifecycle
        static void Initialize();
        static void Shutdown();
        static void Tick();

        // state
        static bool IsInitialized();
        static uint64_t GetSteamId();
        static const std::string& GetPersonaName();

        // achievements and stats
        static void UnlockAchievement(const std::string& name);
        static void ClearAchievement(const std::string& name);
        static bool IsAchievementUnlocked(const std::string& name);
        static void SetStat(const std::string& name, int32_t value);
        static void SetStat(const std::string& name, float value);
        static int32_t GetStatInt(const std::string& name);
        static float GetStatFloat(const std::string& name);
        static void StoreStats();

        // rich presence
        static void SetRichPresence(const std::string& key, const std::string& value);
        static void ClearRichPresence();

        // cloud, steam remote storage
        static bool CloudWrite(const std::string& filename, const void* data, uint32_t size);
        static std::vector<uint8_t> CloudRead(const std::string& filename);
        static bool CloudFileExists(const std::string& filename);
        static bool CloudDelete(const std::string& filename);
    };
}
