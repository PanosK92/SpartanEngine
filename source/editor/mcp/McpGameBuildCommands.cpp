/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =====================
#include "pch.h"
#include "EditorMcpCommands.h"
#include "../Editor.h"
#include "../GameBuild.h"
#include "../widgets/GameBuilder.h"
#include <sstream>
//================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
//======================

namespace editor_mcp
{
    namespace
    {
        vector<string> split_list(const string& value)
        {
            vector<string> items;
            string item;
            stringstream stream(value);
            while (getline(stream, item, value.find(';') != string::npos ? ';' : ','))
            {
                const size_t first = item.find_first_not_of(" \t");
                const size_t last  = item.find_last_not_of(" \t");
                if (first != string::npos)
                {
                    items.push_back(item.substr(first, last - first + 1));
                }
            }
            return items;
        }

        const char* state_name(const game_build::State state)
        {
            switch (state)
            {
                case game_build::State::Running:   return "running";
                case game_build::State::Succeeded: return "succeeded";
                case game_build::State::Failed:    return "failed";
                case game_build::State::Cancelled: return "cancelled";
                default:                           return "idle";
            }
        }

        string report_json(const game_build::Report& report)
        {
            string warnings;
            for (const string& warning : report.warnings)
            {
                warnings += (warnings.empty() ? "" : ",") + quote(warning);
            }

            string json = "{\"ok\":true";
            json += ",\"state\":" + quote(state_name(report.state));
            json += ",\"step\":" + quote(report.step);
            json += ",\"detail\":" + quote(report.detail);
            json += ",\"progress\":" + to_string(report.fraction);
            json += ",\"error\":" + quote(report.error);
            json += ",\"output_directory\":" + quote(report.output_directory);
            json += ",\"executable\":" + quote(report.executable_path);
            json += ",\"archive\":" + quote(report.archive_path);
            json += ",\"steampipe\":" + quote(report.steam_directory);
            json += ",\"files_total\":" + to_string(report.files_total);
            json += ",\"files_copied\":" + to_string(report.files_copied);
            json += ",\"files_unchanged\":" + to_string(report.files_skipped);
            json += ",\"files_removed\":" + to_string(report.files_removed);
            json += ",\"bytes_total\":" + to_string(report.bytes_total);
            json += ",\"seconds\":" + to_string(report.seconds);
            json += ",\"warnings\":[" + warnings + "]}";
            return json;
        }

        void apply_arguments(const McpRequest& request, game_build::Options& options)
        {
            if (const string* value = find_any(request, { "output", "output_directory", "folder" }))
            {
                options.output_directory = *value;
            }
            if (const string* value = find_any(request, { "worlds", "world" }))
            {
                options.worlds = split_list(*value);
            }
            if (const string* value = find(request, "name"))
            {
                options.name = *value;
            }
            if (const string* value = find(request, "version"))
            {
                options.version = *value;
            }
            if (const string* value = find(request, "company"))
            {
                options.company = *value;
            }
            if (const string* value = find_any(request, { "executable", "executable_name" }))
            {
                options.executable_name = *value;
            }
            if (const string* value = find(request, "engine_binary"))
            {
                options.engine_binary = *value;
            }
            if (const string* value = find(request, "window_mode"))
            {
                options.window_mode = GameManifest::WindowModeFromString(*value);
            }
            if (const optional<uint64_t> value = as_uint(find(request, "width")))
            {
                options.window_width = static_cast<uint32_t>(*value);
            }
            if (const optional<uint64_t> value = as_uint(find(request, "height")))
            {
                options.window_height = static_cast<uint32_t>(*value);
            }
            if (const string* value = find(request, "asset_mode"))
            {
                options.asset_mode = to_lower(*value) == "project" ? game_build::AssetMode::EntireProject : game_build::AssetMode::Referenced;
            }
            if (const string* value = find(request, "extra_folders"))
            {
                options.extra_folders = split_list(*value);
            }
            options.include_symbols  = as_bool(find(request, "symbols")).value_or(options.include_symbols);
            options.include_cache    = as_bool(find(request, "cache")).value_or(options.include_cache);
            options.steam_enabled    = as_bool(find(request, "steam")).value_or(options.steam_enabled);
            options.steam_app_id     = static_cast<uint32_t>(as_uint(find(request, "app_id")).value_or(options.steam_app_id));
            options.steam_depot_id   = static_cast<uint32_t>(as_uint(find(request, "depot_id")).value_or(options.steam_depot_id));
            options.steam_appid_file = as_bool(find(request, "steam_appid_file")).value_or(options.steam_appid_file);
            options.steam_scripts    = as_bool(find(request, "steam_scripts")).value_or(options.steam_scripts);
            options.create_archive   = as_bool(find(request, "archive")).value_or(options.create_archive);
            options.launch_when_done = as_bool(find(request, "launch")).value_or(options.launch_when_done);
        }
    }

    void register_game_build(Editor* editor)
    {
        // action: start (default), status, cancel, validate or launch. start and validate take the build window's
        // settings and override them with: output, worlds (comma separated, first boots), name, version, company,
        // executable, engine_binary, window_mode (fullscreen|windowed|maximized), width, height, asset_mode
        // (referenced|project), extra_folders, symbols, steam, app_id, depot_id, steam_appid_file, steam_scripts, archive, launch
        add(
            "game_build",
            [editor](const McpRequest& request) -> string
            {
                const string* action_value = find(request, "action");
                const string action        = action_value ? to_lower(*action_value) : "start";

                if (action == "status")
                {
                    return report_json(game_build::get_report());
                }

                if (action == "cancel")
                {
                    game_build::cancel();
                    return report_json(game_build::get_report());
                }

                if (action == "launch")
                {
                    const game_build::Report report = game_build::get_report();
                    if (report.state != game_build::State::Succeeded)
                    {
                        return failure("there is no finished build to launch");
                    }
                    return game_build::launch(report.executable_path) ? report_json(report) : failure("could not start " + report.executable_path);
                }

                GameBuilder* builder = editor->GetWidget<GameBuilder>();
                if (builder)
                {
                    builder->Refresh();
                }
                game_build::Options options = builder ? builder->GetOptions() : game_build::load_options();
                apply_arguments(request, options);
                if (builder)
                {
                    builder->GetOptions() = options;
                }

                const string problem = game_build::validate(options);
                if (action == "validate")
                {
                    return problem.empty() ? "{\"ok\":true,\"valid\":true}" : "{\"ok\":true,\"valid\":false,\"problem\":" + quote(problem) + "}";
                }

                if (game_build::is_running())
                {
                    return failure("a build is already running, poll with action=status or stop it with action=cancel");
                }
                if (!problem.empty())
                {
                    return failure(problem);
                }
                if (!game_build::start(options))
                {
                    return failure("the build could not start");
                }
                return report_json(game_build::get_report());
            }
        );
    }
}
