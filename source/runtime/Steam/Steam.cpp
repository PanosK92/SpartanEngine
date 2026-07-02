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

//= INCLUDES =============================================
#include "pch.h"
#include "Steam.h"
// the steamworks sdk is vendored locally, compile to no-ops when it is absent
#if __has_include("steam/steam_api.h")
    #define SP_STEAM_ENABLED 1
    SP_WARNINGS_OFF
    #include "steam/steam_api.h"
    SP_WARNINGS_ON
#else
    #define SP_STEAM_ENABLED 0
#endif
//========================================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        bool     m_initialized = false;
        uint64_t m_steam_id    = 0;
        string   m_persona_name;

#if SP_STEAM_ENABLED
        // must match steam_appid.txt and the appid registered on the steam partner site
        // 480 is valve's spacewar test appid, replace with the real one before shipping
        constexpr uint32_t app_id = 480;

        // steamworks callbacks require a live c++ instance for their lifetime
        struct SteamListener
        {
            STEAM_CALLBACK(SteamListener, OnUserStatsReceived, UserStatsReceived_t);
        };

        void SteamListener::OnUserStatsReceived(UserStatsReceived_t* callback)
        {
            if (callback->m_eResult != k_EResultOK)
            {
                SP_LOG_WARNING("failed to receive user stats, result %d", static_cast<int>(callback->m_eResult));
            }
        }

        SteamListener* m_listener = nullptr;
#endif
    }

    void Steam::Initialize()
    {
#if SP_STEAM_ENABLED
        if (!SteamAPI_Init())
        {
            SP_LOG_WARNING("steam is not running or the appid is missing, steam features are disabled");
            return;
        }

        m_initialized  = true;
        m_steam_id     = SteamUser()->GetSteamID().ConvertToUint64();
        m_persona_name = SteamFriends()->GetPersonaName();

        uint32_t running_app_id = SteamUtils()->GetAppID();
        if (running_app_id != app_id)
        {
            SP_LOG_WARNING("steam appid mismatch, expected %u but running as %u", app_id, running_app_id);
        }

        m_listener = new SteamListener();
        SteamUserStats()->RequestCurrentStats();

        SP_LOG_INFO("steam initialized for user %s", m_persona_name.c_str());
#else
        SP_LOG_INFO("steam sdk not present, steam features are disabled");
#endif
    }

    void Steam::Shutdown()
    {
        if (!m_initialized)
        {
            return;
        }

#if SP_STEAM_ENABLED
        delete m_listener;
        m_listener = nullptr;

        SteamAPI_Shutdown();
#endif
        m_initialized = false;
    }

    void Steam::Tick()
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            SteamAPI_RunCallbacks();
        }
#endif
    }

    bool Steam::IsInitialized()
    {
        return m_initialized;
    }

    uint64_t Steam::GetSteamId()
    {
        return m_steam_id;
    }

    const string& Steam::GetPersonaName()
    {
        return m_persona_name;
    }

    void Steam::UnlockAchievement(const string& name)
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            SteamUserStats()->SetAchievement(name.c_str());
            SteamUserStats()->StoreStats();
        }
#endif
    }

    void Steam::ClearAchievement(const string& name)
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            SteamUserStats()->ClearAchievement(name.c_str());
            SteamUserStats()->StoreStats();
        }
#endif
    }

    bool Steam::IsAchievementUnlocked(const string& name)
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            bool unlocked = false;
            SteamUserStats()->GetAchievement(name.c_str(), &unlocked);
            return unlocked;
        }
#endif
        return false;
    }

    void Steam::SetStat(const string& name, int32_t value)
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            SteamUserStats()->SetStat(name.c_str(), value);
        }
#endif
    }

    void Steam::SetStat(const string& name, float value)
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            SteamUserStats()->SetStat(name.c_str(), value);
        }
#endif
    }

    int32_t Steam::GetStatInt(const string& name)
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            int32_t value = 0;
            SteamUserStats()->GetStat(name.c_str(), &value);
            return value;
        }
#endif
        return 0;
    }

    float Steam::GetStatFloat(const string& name)
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            float value = 0.0f;
            SteamUserStats()->GetStat(name.c_str(), &value);
            return value;
        }
#endif
        return 0.0f;
    }

    void Steam::StoreStats()
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            SteamUserStats()->StoreStats();
        }
#endif
    }

    void Steam::SetRichPresence(const string& key, const string& value)
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            SteamFriends()->SetRichPresence(key.c_str(), value.c_str());
        }
#endif
    }

    void Steam::ClearRichPresence()
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            SteamFriends()->ClearRichPresence();
        }
#endif
    }

    bool Steam::CloudWrite(const string& filename, const void* data, uint32_t size)
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            return SteamRemoteStorage()->FileWrite(filename.c_str(), data, static_cast<int32>(size));
        }
#endif
        return false;
    }

    vector<uint8_t> Steam::CloudRead(const string& filename)
    {
#if SP_STEAM_ENABLED
        if (m_initialized && SteamRemoteStorage()->FileExists(filename.c_str()))
        {
            int32 size = SteamRemoteStorage()->GetFileSize(filename.c_str());
            if (size > 0)
            {
                vector<uint8_t> buffer(static_cast<size_t>(size));
                int32 read = SteamRemoteStorage()->FileRead(filename.c_str(), buffer.data(), size);
                buffer.resize(static_cast<size_t>(read < 0 ? 0 : read));
                return buffer;
            }
        }
#endif
        return {};
    }

    bool Steam::CloudFileExists(const string& filename)
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            return SteamRemoteStorage()->FileExists(filename.c_str());
        }
#endif
        return false;
    }

    bool Steam::CloudDelete(const string& filename)
    {
#if SP_STEAM_ENABLED
        if (m_initialized)
        {
            return SteamRemoteStorage()->FileDelete(filename.c_str());
        }
#endif
        return false;
    }
}
