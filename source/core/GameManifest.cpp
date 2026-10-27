/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================
#include "pch.h"
#include "GameManifest.h"
SP_WARNINGS_OFF
#include "../io/pugixml.hpp"
SP_WARNINGS_ON
//============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        GameManifestData manifest;
        bool is_packaged = false;
    }

    void GameManifest::Load()
    {
        const string path = FileSystem::GetExecutableDirectory() + "/" + file_name;
        if (!FileSystem::Exists(path))
        {
            return;
        }

        if (!Read(path, manifest))
        {
            SP_LOG_ERROR("Failed to read game manifest: %s", path.c_str());
            return;
        }

        is_packaged = true;
        SP_LOG_INFO("Packaged game: %s %s, start world: %s", manifest.name.c_str(), manifest.version.c_str(), manifest.start_world.c_str());
    }

    bool GameManifest::Read(const string& file_path, GameManifestData& data)
    {
        pugi::xml_document document;
        if (!document.load_file(file_path.c_str()))
        {
            return false;
        }

        pugi::xml_node game = document.child("Game");
        if (!game)
        {
            return false;
        }

        data.name        = game.attribute("name").as_string(data.name.c_str());
        data.version     = game.attribute("version").as_string(data.version.c_str());
        data.company     = game.attribute("company").as_string(data.company.c_str());
        data.start_world = game.attribute("start_world").as_string();

        if (pugi::xml_node window = game.child("Window"))
        {
            data.window_mode   = WindowModeFromString(window.attribute("mode").as_string());
            data.window_width  = window.attribute("width").as_uint(data.window_width);
            data.window_height = window.attribute("height").as_uint(data.window_height);
        }

        if (pugi::xml_node steam = game.child("Steam"))
        {
            data.steam_enabled = steam.attribute("enabled").as_bool(false);
            data.steam_app_id  = steam.attribute("app_id").as_uint(0);
        }

        return !data.start_world.empty();
    }

    bool GameManifest::Save(const string& file_path, const GameManifestData& data)
    {
        pugi::xml_document document;
        pugi::xml_node game = document.append_child("Game");
        game.append_attribute("name")        = data.name.c_str();
        game.append_attribute("version")     = data.version.c_str();
        game.append_attribute("company")     = data.company.c_str();
        game.append_attribute("start_world") = data.start_world.c_str();
        game.append_attribute("engine")      = version::c_str();

        pugi::xml_node window = game.append_child("Window");
        window.append_attribute("mode")   = WindowModeToString(data.window_mode);
        window.append_attribute("width")  = data.window_width;
        window.append_attribute("height") = data.window_height;

        pugi::xml_node steam = game.append_child("Steam");
        steam.append_attribute("enabled") = data.steam_enabled;
        steam.append_attribute("app_id")  = data.steam_app_id;

        return document.save_file(file_path.c_str(), "  ");
    }

    bool GameManifest::IsPackaged()
    {
        return is_packaged;
    }

    const GameManifestData& GameManifest::Get()
    {
        return manifest;
    }

    const char* GameManifest::WindowModeToString(const GameWindowMode mode)
    {
        switch (mode)
        {
            case GameWindowMode::Windowed:  return "windowed";
            case GameWindowMode::Maximized: return "maximized";
            default:                        return "fullscreen";
        }
    }

    GameWindowMode GameManifest::WindowModeFromString(const string& mode)
    {
        if (mode == "windowed")
        {
            return GameWindowMode::Windowed;
        }

        if (mode == "maximized")
        {
            return GameWindowMode::Maximized;
        }

        return GameWindowMode::FullscreenBorderless;
    }
}
