/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================================
#include "pch.h"
#include "GameBuilder.h"
#include "../Editor.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/source/imgui_stdlib.h"
#include "core/Window.h"
#include "resource/ResourceCache.h"
SP_WARNINGS_OFF
#include <SDL3/SDL_dialog.h>
SP_WARNINGS_ON
#include <filesystem>
#include <sstream>
//==============================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
namespace ui = ImGui::EditorUi;
//======================

namespace
{
    // the os dialog answers on its own thread whenever the user is done with it
    mutex folder_dialog_mutex;
    string folder_dialog_result;
    bool folder_dialog_pending = false;

    void SDLCALL on_folder_chosen(void*, const char* const* files, int)
    {
        lock_guard lock(folder_dialog_mutex);
        folder_dialog_pending = false;
        if (files && files[0])
        {
            folder_dialog_result = files[0];
        }
    }

    void open_folder_dialog(const string& current)
    {
        {
            lock_guard lock(folder_dialog_mutex);
            if (folder_dialog_pending)
            {
                return;
            }
            folder_dialog_pending = true;
        }

        // the dialog refuses to open on a folder that does not exist yet, start from the nearest one that does
        error_code ec;
        filesystem::path start(current);
        while (!start.empty() && !filesystem::is_directory(start, ec) && start != start.parent_path())
        {
            start = start.parent_path();
        }
        const string location = start.empty() ? string() : start.string();
        SDL_ShowOpenFolderDialog(on_folder_chosen, nullptr, static_cast<SDL_Window*>(Window::GetHandleSDL()), location.empty() ? nullptr : location.c_str(), false);
    }

    string format_bytes(const uint64_t bytes)
    {
        const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        double value = static_cast<double>(bytes);
        uint32_t unit = 0;
        while (value >= 1024.0 && unit < 4)
        {
            value /= 1024.0;
            unit++;
        }
        char buffer[32];
        snprintf(buffer, sizeof(buffer), unit == 0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
        return buffer;
    }

    float label_width()
    {
        return ui::scaled(132.0f);
    }

    void field_label(const char* label, const char* tooltip = nullptr)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        if (tooltip)
        {
            ImGuiSp::tooltip(tooltip);
        }
        ImGui::SameLine(label_width());
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    void hint(const char* text)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label_width());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_faint);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    void note(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_faint);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    void section(const char* label)
    {
        ImGui::Dummy(ImVec2(0.0f, ui::scaled(8.0f)));
        ui::section_rule(label);
        ImGui::Dummy(ImVec2(0.0f, ui::scaled(2.0f)));
    }

    bool input_uint(const char* id, uint32_t* value)
    {
        return ImGui::InputScalar(id, ImGuiDataType_U32, value, nullptr, nullptr, "%u");
    }

    string world_title(const string& path)
    {
        return filesystem::path(path).stem().string();
    }

    void right_aligned(const float width)
    {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + max(0.0f, ImGui::GetContentRegionAvail().x - width));
    }
}

GameBuilder::GameBuilder(Editor* editor) : Widget(editor)
{
    m_title             = "Build Game";
    m_visible           = false;
    m_size_initial      = math::Vector2(680.0f, 1040.0f);
    m_size_min          = math::Vector2(520.0f, 400.0f);
    m_flags            |= ImGuiWindowFlags_NoDocking;
    m_toolbar_order     = 12;
    m_toolbar_icon      = static_cast<int>(IconType::Exe);
}

void GameBuilder::Refresh()
{
    if (!m_loaded)
    {
        m_options = game_build::load_options();
        m_loaded  = true;
    }

    vector<string> worlds;
    for (const string& world : m_options.worlds)
    {
        const string normalized = game_build::normalize_world_path(world);
        if (!normalized.empty() && find(worlds.begin(), worlds.end(), normalized) == worlds.end())
        {
            worlds.push_back(normalized);
        }
    }
    m_options.worlds = worlds;

    m_available_worlds = game_build::find_worlds();
    for (const string& world : m_options.worlds)
    {
        if (find(m_available_worlds.begin(), m_available_worlds.end(), world) == m_available_worlds.end())
        {
            m_available_worlds.push_back(world);
        }
    }

    m_engine_binaries = game_build::find_engine_binaries();

    m_extra_folders_text.clear();
    for (const string& folder : m_options.extra_folders)
    {
        m_extra_folders_text += folder + "\n";
    }
}

void GameBuilder::OnVisible()
{
    Refresh();
}

void GameBuilder::OnInvisible()
{
    if (m_loaded && !game_build::is_running())
    {
        game_build::save_options(m_options);
    }
}

void GameBuilder::OnTickVisible()
{
    if (!m_loaded)
    {
        Refresh();
    }

    {
        lock_guard lock(folder_dialog_mutex);
        if (!folder_dialog_result.empty())
        {
            m_options.output_directory = filesystem::path(folder_dialog_result).generic_string();
            folder_dialog_result.clear();
        }
    }

    ui::panel_header("Build Game", "Package the project into a standalone game that runs without the editor, ready to zip up or upload to Steam.", Editor::font_bold);

    // the footer grows with the report, last frame's height keeps the build button pinned to the bottom
    const bool running = game_build::is_running();
    const float footer = max(m_footer_height, ImGui::GetFrameHeightWithSpacing() * 2.0f + ui::scaled(36.0f));
    ImGui::BeginChild("##game_build_settings", ImVec2(0.0f, -footer), ImGuiChildFlags_None);
    ImGui::BeginDisabled(running);
    DrawGame();
    DrawWorlds();
    DrawDisplay();
    DrawContent();
    DrawSteam();
    DrawOutput();
    ImGui::EndDisabled();
    ImGui::EndChild();

    const float footer_start = ImGui::GetCursorPosY();
    ImGui::Separator();
    DrawBuild();
    m_footer_height = ImGui::GetCursorPosY() - footer_start + ImGui::GetStyle().WindowPadding.y;
}

void GameBuilder::DrawGame()
{
    section("Game");

    field_label("Name", "Shown in the window title and used for the executable");
    ImGui::InputText("##name", &m_options.name);

    field_label("Version");
    ImGui::InputText("##version", &m_options.version);

    field_label("Company");
    ImGui::InputTextWithHint("##company", "optional", &m_options.company);

    field_label("Executable", "File name of the game binary, derived from the name when left empty");
    const string derived = game_build::resolve_executable_name({ .name = m_options.name });
    ImGui::InputTextWithHint("##executable", derived.c_str(), &m_options.executable_name);
}

void GameBuilder::DrawWorlds()
{
    section("Worlds");
    note("Tick the worlds to ship. The one marked start is where the game begins.");

    const float row_height = ImGui::GetFrameHeightWithSpacing();
    const float height     = row_height * static_cast<float>(clamp<size_t>(m_available_worlds.size(), 2, 7)) + ImGui::GetStyle().WindowPadding.y * 2.0f;
    ImGui::BeginChild("##worlds", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
    for (const string& world : m_available_worlds)
    {
        auto it              = find(m_options.worlds.begin(), m_options.worlds.end(), world);
        bool included        = it != m_options.worlds.end();
        const bool is_start  = included && it == m_options.worlds.begin();

        ImGui::PushID(world.c_str());
        if (ImGui::Checkbox("##include", &included))
        {
            if (included)
            {
                m_options.worlds.push_back(world);
            }
            else
            {
                m_options.worlds.erase(find(m_options.worlds.begin(), m_options.worlds.end(), world));
            }
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(world_title(world).c_str());
        ImGui::SameLine();

        // the path gives way to the start button, the tooltip keeps the whole of it
        const float start_width = ImGui::CalcTextSize("Set as start").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float path_width  = ImGui::GetContentRegionAvail().x - start_width - ImGui::GetStyle().ItemSpacing.x;
        if (path_width > ui::scaled(24.0f))
        {
            const ImVec2 position = ImGui::GetCursorScreenPos();
            const float y         = position.y + ImGui::GetStyle().FramePadding.y;
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_faint);
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(position.x, y), ImVec2(position.x + path_width, y + ImGui::GetTextLineHeight()), position.x + path_width, world.c_str(), nullptr, nullptr);
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(path_width, ImGui::GetFrameHeight()));
            ImGuiSp::tooltip(world.c_str());
        }

        right_aligned(start_width);
        if (is_start)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + start_width - ImGui::CalcTextSize("START").x - ui::scaled(14.0f));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ui::scaled(1.0f));
            ui::draw_chip("START", ui::alpha(ImGui::Style::color_accent_1, 0.14f), ImGui::Style::color_accent_1);
        }
        else if (ImGui::SmallButton("Set as start"))
        {
            auto existing = find(m_options.worlds.begin(), m_options.worlds.end(), world);
            if (existing != m_options.worlds.end())
            {
                m_options.worlds.erase(existing);
            }
            m_options.worlds.insert(m_options.worlds.begin(), world);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void GameBuilder::DrawDisplay()
{
    section("Display");

    field_label("Window");
    const char* modes[] = { "Fullscreen (borderless)", "Windowed", "Maximized window" };
    int mode = static_cast<int>(m_options.window_mode);
    if (ImGui::Combo("##window_mode", &mode, modes, IM_ARRAYSIZE(modes)))
    {
        m_options.window_mode = static_cast<GameWindowMode>(mode);
    }

    if (m_options.window_mode == GameWindowMode::Windowed)
    {
        field_label("Size");
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::SetNextItemWidth(half);
        input_uint("##width", &m_options.window_width);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(half);
        input_uint("##height", &m_options.window_height);
    }
    hint("Players can still toggle fullscreen with Right Alt + Enter.");
}

void GameBuilder::DrawContent()
{
    section("Content");

    field_label("Assets", "Referenced follows paths out of the worlds, materials, cars and scripts. Entire project copies everything under project/.");
    const char* modes[] = { "Referenced by the worlds (recommended)", "Entire project folder" };
    int mode = static_cast<int>(m_options.asset_mode);
    if (ImGui::Combo("##asset_mode", &mode, modes, IM_ARRAYSIZE(modes)))
    {
        m_options.asset_mode = static_cast<game_build::AssetMode>(mode);
    }
    if (m_options.asset_mode == game_build::AssetMode::EntireProject)
    {
        hint("Everything under project/ ships, including other worlds and generated mcp content. Expect tens of gigabytes.");
    }

    field_label("Extra folders", "Folders or files to always ship, one per line, relative to the engine binaries. Use this for assets that scripts load by building paths at runtime.");
    if (ImGui::InputTextMultiline("##extra_folders", &m_extra_folders_text, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.6f)))
    {
        m_options.extra_folders.clear();
        stringstream lines(m_extra_folders_text);
        string line;
        while (getline(lines, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (!line.empty())
            {
                m_options.extra_folders.push_back(line);
            }
        }
    }

    field_label("Engine binary", "The binary that becomes the game executable");
    const string current = game_build::current_engine_binary();
    const string preview = m_options.engine_binary.empty() ? current + " (running)" : m_options.engine_binary;
    if (ImGui::BeginCombo("##engine_binary", preview.c_str()))
    {
        if (ImGui::Selectable((current + " (running)").c_str(), m_options.engine_binary.empty()))
        {
            m_options.engine_binary.clear();
        }
        for (const string& binary : m_engine_binaries)
        {
            if (binary != current && ImGui::Selectable(binary.c_str(), m_options.engine_binary == binary))
            {
                m_options.engine_binary = binary;
            }
        }
        ImGui::EndCombo();
    }
    const string selected = m_options.engine_binary.empty() ? current : m_options.engine_binary;
    if (selected.find("_development") != string::npos || selected.find("_debug") != string::npos)
    {
        hint("This is a development binary. A release build (msbuild /p:Configuration=release) is smaller and faster to ship.");
    }

    field_label("Debug symbols");
    ImGui::Checkbox("Include .pdb files for crash investigation##symbols", &m_options.include_symbols);

    field_label("Bake cache");
    ImGui::Checkbox("Ship the generated cache for a faster first load##cache", &m_options.include_cache);
    hint("Meshlets, LODs, mips and terrain bakes are rebuilt on a player's first run when left out. The editor's cache also holds entries for assets other worlds loaded, so it can be large.");
}

void GameBuilder::DrawSteam()
{
    section("Steam");

    field_label("Steamworks");
    ImGui::Checkbox("Ship with Steam (achievements, overlay, rich presence)##steam", &m_options.steam_enabled);
    if (!m_options.steam_enabled)
    {
        return;
    }

    field_label("App ID", "From the Steamworks partner site, 480 is Valve's Spacewar test app");
    input_uint("##app_id", &m_options.steam_app_id);

    field_label("Depot ID", "The depot that holds the Windows build, usually the App ID + 1");
    input_uint("##depot_id", &m_options.steam_depot_id);

    field_label("Testing");
    ImGui::Checkbox("Write steam_appid.txt so the game runs outside of Steam##appid_file", &m_options.steam_appid_file);

    field_label("Upload");
    ImGui::Checkbox("Generate SteamPipe scripts for steamcmd##steam_scripts", &m_options.steam_scripts);
}

void GameBuilder::DrawOutput()
{
    section("Output");

    field_label("Folder", "An empty folder, or one that holds a previous build of this game. Rebuilds only copy what changed.");
    const float button_width = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button_width - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##output", "C:/builds/my_game", &m_options.output_directory);
    ImGui::SameLine();
    if (ImGuiSp::button("Browse..."))
    {
        open_folder_dialog(m_options.output_directory);
    }

    field_label("Archive");
    ImGui::Checkbox("Create a .zip next to the build folder##archive", &m_options.create_archive);

    field_label("When done");
    ImGui::Checkbox("Launch the game##launch", &m_options.launch_when_done);
}

void GameBuilder::DrawBuild()
{
    const game_build::Report report = game_build::get_report();
    const bool running              = game_build::is_running();
    const string problem            = running ? string() : game_build::validate(m_options);
    const float width               = ImGui::GetContentRegionAvail().x;

    if (running)
    {
        ImGui::TextUnformatted(report.step.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_faint);
        ImGui::TextUnformatted(report.detail.empty() ? " " : report.detail.c_str());
        ImGui::PopStyleColor();

        char overlay[96];
        if (report.bytes_total > 0)
        {
            snprintf(overlay, sizeof(overlay), "%.0f%%  of %s", report.fraction * 100.0f, format_bytes(report.bytes_total).c_str());
        }
        else
        {
            snprintf(overlay, sizeof(overlay), "%.0f%%", report.fraction * 100.0f);
        }
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::Style::color_accent_1);
        ImGui::ProgressBar(report.fraction, ImVec2(width, ui::scaled(6.0f)), "");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
        ImGui::TextUnformatted(overlay);
        ImGui::PopStyleColor();

        if (ImGuiSp::button("Cancel", ImVec2(width, ui::scaled(36.0f))))
        {
            game_build::cancel();
        }
        return;
    }

    DrawReport(report);

    if (!problem.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_warning);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(problem.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_faint);
        const string target = (filesystem::path(m_options.output_directory) / game_build::resolve_executable_name(m_options)).generic_string();
        ImGui::Text("Builds %s", target.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::BeginDisabled(!problem.empty());
    ui::push_primary_button();
    ImGui::PushFont(Editor::font_bold, 0.0f);
    if (ImGui::Button("Build Game", ImVec2(width, ui::scaled(36.0f))))
    {
        game_build::start(m_options);
    }
    ImGui::PopFont();
    ui::pop_primary_button();
    ImGui::EndDisabled();
}

void GameBuilder::DrawReport(const game_build::Report& report)
{
    if (report.state == game_build::State::Idle)
    {
        return;
    }

    const ImVec4 tint =
        report.state == game_build::State::Succeeded ? ImGui::Style::color_ok :
        report.state == game_build::State::Cancelled ? ImGui::Style::color_warning :
        ImGui::Style::color_error;

    const float line_height = ImGui::GetTextLineHeight();
    const ImVec2 position   = ImGui::GetCursorScreenPos();
    ui::status_dot(ImGui::GetWindowDrawList(), ImVec2(position.x + ui::scaled(5.0f), position.y + line_height * 0.5f), ui::scaled(3.5f), tint);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ui::scaled(16.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, tint);
    ImGui::TextUnformatted(report.step.c_str());
    ImGui::PopStyleColor();

    if (report.state == game_build::State::Succeeded)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
        ImGui::Text("%llu files, %s in %.1f s  (%llu copied, %llu unchanged, %llu removed)",
            static_cast<unsigned long long>(report.files_total), format_bytes(report.bytes_total).c_str(), report.seconds,
            static_cast<unsigned long long>(report.files_copied), static_cast<unsigned long long>(report.files_skipped), static_cast<unsigned long long>(report.files_removed));
        ImGui::PopStyleColor();

        if (ImGuiSp::button("Play"))
        {
            game_build::launch(report.executable_path);
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Open folder"))
        {
            game_build::open_folder(report.output_directory);
        }
        if (!report.steam_directory.empty())
        {
            ImGui::SameLine();
            if (ImGuiSp::button("Open SteamPipe scripts"))
            {
                game_build::open_folder(report.steam_directory);
            }
        }
        if (!report.archive_path.empty())
        {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", filesystem::path(report.archive_path).filename().string().c_str());
            ImGuiSp::tooltip(report.archive_path.c_str());
        }
    }
    else if (!report.error.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_error);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(report.error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    if (!report.warnings.empty())
    {
        const string label = to_string(report.warnings.size()) + (report.warnings.size() == 1 ? " warning" : " warnings") + "##build_warnings";
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_warning);
        const bool open = ImGui::TreeNode(label.c_str());
        ImGui::PopStyleColor();
        if (open)
        {
            ImGui::PushTextWrapPos(0.0f);
            for (const string& warning : report.warnings)
            {
                ImGui::BulletText("%s", warning.c_str());
            }
            ImGui::PopTextWrapPos();
            ImGui::TreePop();
        }
    }
    ImGui::Dummy(ImVec2(0.0f, ui::scaled(4.0f)));
}
