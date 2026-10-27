/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====
#include <string>
#include <cstdint>
//================

namespace spartan
{
    enum class GameWindowMode : uint8_t
    {
        FullscreenBorderless,
        Windowed,
        Maximized
    };

    struct GameManifestData
    {
        std::string name           = "Spartan Game";
        std::string version        = "1.0.0";
        std::string company;
        std::string start_world;   // relative to the executable
        GameWindowMode window_mode = GameWindowMode::FullscreenBorderless;
        uint32_t window_width      = 1920;
        uint32_t window_height     = 1080;
        bool steam_enabled         = false;
        uint32_t steam_app_id      = 0;
    };

    // a game.xml next to the executable turns the binary into a shipped game: no editor ui, no mcp
    // server, no asset downloads, the start world loads straight into play mode
    class GameManifest
    {
    public:
        static constexpr const char* file_name = "game.xml";

        static void Load();
        static bool Save(const std::string& file_path, const GameManifestData& data);
        static bool Read(const std::string& file_path, GameManifestData& data);

        static bool IsPackaged();
        static const GameManifestData& Get();

        static const char* WindowModeToString(GameWindowMode mode);
        static GameWindowMode WindowModeFromString(const std::string& mode);
    };
}
