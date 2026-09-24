/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
