/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==============
#include <string>
#include <vector>
#include <cstdint>
#include "core/GameManifest.h"
//=========================

// packages the project into a standalone game folder: the engine binary renamed to the game, its
// runtime libraries, engine data, the chosen worlds and every asset they reference, plus a game.xml
// that makes the binary boot straight into play mode without the editor
namespace game_build
{
    enum class AssetMode : uint8_t
    {
        Referenced,   // follow references out of the worlds, the smallest correct package
        EntireProject // copy the whole project folder, larger but immune to paths built at runtime
    };

    struct Options
    {
        std::string name    = "My Game";
        std::string version = "1.0.0";
        std::string company;
        std::string executable_name;   // empty derives it from the name
        std::string output_directory;
        std::vector<std::string> worlds; // the first one boots, paths as the editor knows them
        std::string engine_binary;     // file name next to the editor, empty means the running binary
        spartan::GameWindowMode window_mode = spartan::GameWindowMode::FullscreenBorderless;
        uint32_t window_width          = 1920;
        uint32_t window_height         = 1080;
        AssetMode asset_mode           = AssetMode::Referenced;
        std::vector<std::string> extra_folders; // relative to the engine binaries
        bool include_symbols           = false;
        bool include_cache             = false;
        bool steam_enabled             = false;
        uint32_t steam_app_id          = 480;
        uint32_t steam_depot_id        = 481;
        bool steam_appid_file          = true;
        bool steam_scripts             = true;
        bool create_archive            = false;
        bool launch_when_done          = false;
    };

    enum class State : uint8_t
    {
        Idle,
        Running,
        Succeeded,
        Failed,
        Cancelled
    };

    struct Report
    {
        State state      = State::Idle;
        float fraction   = 0.0f;
        std::string step;
        std::string detail;
        std::string error;
        std::string output_directory;
        std::string executable_path;
        std::string archive_path;
        std::string steam_directory;
        std::vector<std::string> warnings;
        uint64_t files_total   = 0;
        uint64_t files_copied  = 0;
        uint64_t files_skipped = 0;
        uint64_t files_removed = 0;
        uint64_t bytes_total   = 0;
        double seconds         = 0.0;
    };

    // empty when the options can build, otherwise what has to change
    std::string validate(const Options& options);
    bool start(const Options& options);
    void cancel();
    bool is_running();
    Report get_report();
    void shutdown();

    Options load_options();
    void save_options(const Options& options);

    std::vector<std::string> find_worlds();
    // the form find_worlds lists, relative to the engine binaries, so the same world never shows up twice
    std::string normalize_world_path(const std::string& path);
    std::vector<std::string> find_engine_binaries();
    std::string current_engine_binary();
    std::string resolve_executable_name(const Options& options);

    bool launch(const std::string& executable_path);
    void open_folder(const std::string& directory);
}
