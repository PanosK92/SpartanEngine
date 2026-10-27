/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===========
#include "Widget.h"
#include "../GameBuild.h"
//======================

class GameBuilder : public Widget
{
public:
    GameBuilder(Editor* editor);

    void OnVisible() override;
    void OnInvisible() override;
    void OnTickVisible() override;

    game_build::Options& GetOptions() { return m_options; }
    void Refresh();

private:
    void DrawGame();
    void DrawWorlds();
    void DrawDisplay();
    void DrawContent();
    void DrawSteam();
    void DrawOutput();
    void DrawBuild();
    void DrawReport(const game_build::Report& report);

    game_build::Options m_options;
    std::vector<std::string> m_available_worlds;
    std::vector<std::string> m_engine_binaries;
    std::string m_extra_folders_text;
    float m_footer_height = 0.0f;
    bool m_loaded         = false;
};
