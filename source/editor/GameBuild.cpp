/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ======================
#include "pch.h"
#include "GameBuild.h"
#include "world/World.h"
#include "resource/ResourceCache.h"
SP_WARNINGS_OFF
#include "io/pugixml.hpp"
#include <SDL3/SDL_misc.h>
SP_WARNINGS_ON
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <cctype>
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif
//=================================

//= NAMESPACES ============
using namespace std;
namespace fs = std::filesystem;
//=========================

namespace game_build
{
    namespace
    {
        const char* options_file_name = "game_build.xml";

        // text formats that name other assets, binary formats (meshes, textures, audio) never do
        const char* scannable_extensions[] = { ".world", ".xml", ".lua", ".car", ".json", ".prefab", ".particle", ".txt" };
        constexpr uintmax_t scan_size_limit = 64ull * 1024ull * 1024ull;

        // assets the engine loads by hardcoded path when a world uses the matching component
        struct DefaultAssetRule
        {
            const char* component_tag;
            vector<const char*> paths;
        };

        const vector<DefaultAssetRule>& default_asset_rules()
        {
            static const vector<DefaultAssetRule> rules =
            {
                { "<terrain",      { "project/materials", "project/models/forest", "project/models/rock_2", "project/models/island_biomes" } },
                { "<pedestrians",  { "project/models/mannequiny" } },
                { "<traffic",      { "project/cars/ferrari_laferrari.car", "project/music/cars" } },
                { "<race_driver",  { "project/cars/ferrari_laferrari.car", "project/music/cars" } },
                { "<route_driver", { "project/cars/ferrari_laferrari.car", "project/music/cars" } },
                { "<prefab",       { "project/cars/ferrari_laferrari.car", "project/music/cars" } }
            };
            return rules;
        }

        mutex report_mutex;
        Report report;
        atomic<bool> cancel_requested = false;
        atomic<bool> running          = false;
        thread worker;

        struct BuildCancelled
        {
        };

        struct BuildFailed
        {
            string message;
        };

        struct Plan
        {
            fs::path engine_directory;
            fs::path root_directory; // the repository, worlds live next to the engine binaries
            fs::path output_directory;
            map<string, fs::path> files;                        // destination inside the package -> source
            map<string, vector<pair<string, string>>> rewrites; // destination -> text replacements
            set<string> scanned;
            set<string> directories;
            set<string> warned_prefixes;
            set<string> component_tags;
            vector<pair<fs::path, string>> scan_queue;
            string start_world;
            bool include_cache = false;
        };

        void set_step(const string& step, const float fraction)
        {
            lock_guard lock(report_mutex);
            report.step     = step;
            report.detail.clear();
            report.fraction = fraction;
        }

        void set_detail(const string& detail, const float fraction)
        {
            lock_guard lock(report_mutex);
            report.detail   = detail;
            report.fraction = fraction;
        }

        void warn(const string& message)
        {
            SP_LOG_WARNING("Game build: %s", message.c_str());
            lock_guard lock(report_mutex);
            report.warnings.push_back(message);
        }

        void check_cancel()
        {
            if (cancel_requested.load(memory_order_relaxed))
            {
                throw BuildCancelled{};
            }
        }

        string to_lower(string value)
        {
            transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
            return value;
        }

        string trim(const string& value)
        {
            const size_t first = value.find_first_not_of(" \t\r\n");
            if (first == string::npos)
            {
                return "";
            }
            const size_t last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        fs::path canonical_path(const fs::path& path)
        {
            error_code ec;
            fs::path result = fs::weakly_canonical(path, ec);
            return ec ? path.lexically_normal() : result;
        }

        fs::path engine_directory()
        {
            return canonical_path(fs::path(spartan::FileSystem::GetExecutableDirectory()));
        }

        // equal paths count as within, both sides are expected to be canonical
        bool is_within(const fs::path& path, const fs::path& directory)
        {
            const fs::path relative = path.lexically_relative(directory);
            return !relative.empty() && *relative.begin() != "..";
        }

        size_t depth(const fs::path& relative)
        {
            size_t count = 0;
            for (const fs::path& part : relative)
            {
                if (!part.empty() && part != ".")
                {
                    count++;
                }
            }
            return count;
        }

        fs::path resolve(const fs::path& engine, const string& path)
        {
            const fs::path value(path);
            return canonical_path(value.is_absolute() ? value : engine / value);
        }

        // files outside the engine folder keep their place relative to the repository, so ../worlds/x becomes worlds/x
        bool destination_for(const Plan& plan, const fs::path& source, string& destination)
        {
            if (is_within(source, plan.engine_directory))
            {
                destination = source.lexically_relative(plan.engine_directory).generic_string();
                return true;
            }

            if (is_within(source, plan.root_directory))
            {
                destination = source.lexically_relative(plan.root_directory).generic_string();
                return true;
            }

            return false;
        }

        bool is_scannable(const fs::path& source)
        {
            const string extension = to_lower(source.extension().string());
            for (const char* candidate : scannable_extensions)
            {
                if (extension == candidate)
                {
                    error_code ec;
                    const uintmax_t size = fs::file_size(source, ec);
                    return !ec && size <= scan_size_limit;
                }
            }
            return false;
        }

        bool add_file(Plan& plan, const fs::path& source_in, const bool scan = true)
        {
            const fs::path source = canonical_path(source_in);
            if (is_within(source, plan.output_directory))
            {
                return false;
            }

            string destination;
            if (!destination_for(plan, source, destination))
            {
                warn("Skipped a file outside the project: " + source.generic_string());
                return false;
            }

            plan.files.emplace(destination, source);
            if (scan && is_scannable(source) && plan.scanned.insert(source.generic_string()).second)
            {
                plan.scan_queue.emplace_back(source, destination);
            }
            return true;
        }

        void add_directory(Plan& plan, const fs::path& source_in, const bool scan)
        {
            const fs::path source = canonical_path(source_in);
            if (!plan.directories.insert(source.generic_string()).second)
            {
                return;
            }

            error_code ec;
            uint32_t visited = 0;
            for (fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
            {
                if (ec)
                {
                    break;
                }

                if ((++visited & 255) == 0)
                {
                    check_cancel();
                }

                // bake caches are keyed by content hash and rebuilt on a miss, the editor's copy also holds
                // entries for every asset any world ever loaded, so it only ships when asked for
                if (it->is_directory(ec))
                {
                    if (!plan.include_cache && it->path().filename() == "generated_cache")
                    {
                        it.disable_recursion_pending();
                    }
                    continue;
                }

                if (it->is_regular_file(ec))
                {
                    add_file(plan, it->path(), scan);
                }
            }
        }

        // models keep textures and buffers beside them, a model in its own folder takes the folder along
        void add_model_dependencies(Plan& plan, const fs::path& source)
        {
            string destination;
            if (!destination_for(plan, source, destination))
            {
                return;
            }

            const fs::path parent = source.parent_path();
            if (depth(fs::path(destination).parent_path()) >= 3)
            {
                add_directory(plan, parent, false);
                return;
            }

            error_code ec;
            for (const fs::directory_entry& entry : fs::directory_iterator(parent, ec))
            {
                if (entry.is_regular_file(ec) && entry.path().stem() == source.stem())
                {
                    add_file(plan, entry.path(), false);
                }
            }
        }

        bool is_token_boundary(const char c)
        {
            return c == '"' || c == '\'' || c == '=' || c == ' ' || c == '(' || c == ',' || c == ';' || c == '>' || c == '\t' || c == '\n' || c == '\r' || c == '[' || c == '{';
        }

        bool is_token_end(const char c)
        {
            return c == '"' || c == '\'' || c == '<' || c == '>' || c == '\r' || c == '\n' || c == '\t' || c == ';' || c == '|' || c == '*' || c == '?' || c == '\0';
        }

        void handle_token(Plan& plan, const string& token, const fs::path& referrer, vector<pair<string, string>>& rewrites)
        {
            string path = token;
            while (!path.empty() && (path.back() == ' ' || path.back() == ')' || path.back() == ',' || path.back() == ']'))
            {
                path.pop_back();
            }
            const string as_written = path;
            if (path.size() > 2 && path.compare(0, 2, "./") == 0)
            {
                path = path.substr(2);
            }
            if (path.empty())
            {
                return;
            }

            const fs::path source = canonical_path(plan.engine_directory / path);
            error_code ec;
            if (!fs::exists(source, ec))
            {
                // several paths in one attribute, separated by spaces
                const size_t space = path.find(' ');
                if (space != string::npos)
                {
                    stringstream pieces(path);
                    string piece;
                    while (pieces >> piece)
                    {
                        handle_token(plan, piece, referrer, rewrites);
                    }
                }
                return;
            }

            const bool is_directory = fs::is_directory(source, ec);
            string destination;
            if (!destination_for(plan, source, destination))
            {
                return;
            }

            // a path that climbs out of the engine folder would climb out of the package too
            if (as_written.compare(0, 3, "../") == 0)
            {
                string replacement = destination;
                if (is_directory && as_written.back() == '/' && (replacement.empty() || replacement.back() != '/'))
                {
                    replacement += '/';
                }
                rewrites.emplace_back(as_written, replacement);
            }

            if (is_directory)
            {
                // a bare root like project/models/ is a prefix a script completes at runtime, copying it would drag gigabytes along
                if (depth(fs::path(destination)) <= 2)
                {
                    if (plan.warned_prefixes.insert(destination).second)
                    {
                        warn("'" + as_written + "' in " + referrer.filename().string() + " is completed at runtime, only the files named explicitly were packaged. Add the folders it needs under extra folders.");
                    }
                    return;
                }

                add_directory(plan, source, true);
                return;
            }

            add_file(plan, source);
            if (spartan::FileSystem::IsSupportedModelFile(source.string()))
            {
                add_model_dependencies(plan, source);
            }
        }

        void scan_file(Plan& plan, const fs::path& source, const string& destination)
        {
            string content;
            if (!spartan::FileSystem::ReadFile(source.string(), content))
            {
                warn("Could not read " + source.generic_string());
                return;
            }

            if (to_lower(source.extension().string()) == ".world")
            {
                for (const DefaultAssetRule& rule : default_asset_rules())
                {
                    if (content.find(rule.component_tag) != string::npos)
                    {
                        plan.component_tags.insert(rule.component_tag);
                    }
                }
            }

            vector<pair<string, string>> rewrites;
            const size_t size = content.size();
            for (size_t i = 0; i < size; ++i)
            {
                const char c = content[i];
                if (c != 'p' && c != 'd' && c != '.')
                {
                    continue;
                }

                const bool match =
                    content.compare(i, 8, "project/") == 0 ||
                    content.compare(i, 5, "data/") == 0 ||
                    content.compare(i, 3, "../") == 0;
                if (!match)
                {
                    continue;
                }

                size_t start = i;
                if (c != '.' && start >= 2 && content[start - 1] == '/' && content[start - 2] == '.')
                {
                    start -= 2;
                }
                if (start > 0 && !is_token_boundary(content[start - 1]))
                {
                    continue;
                }

                size_t end = start;
                while (end < size && !is_token_end(content[end]))
                {
                    end++;
                }

                handle_token(plan, content.substr(start, end - start), source, rewrites);
                i = end;
            }

            if (!rewrites.empty())
            {
                sort(rewrites.begin(), rewrites.end());
                rewrites.erase(unique(rewrites.begin(), rewrites.end()), rewrites.end());
                plan.rewrites[destination] = move(rewrites);
            }
        }

        void process_scan_queue(Plan& plan)
        {
            while (!plan.scan_queue.empty())
            {
                check_cancel();
                const auto [source, destination] = plan.scan_queue.back();
                plan.scan_queue.pop_back();
                set_detail(destination, 0.05f);
                scan_file(plan, source, destination);
            }
        }

        void add_path(Plan& plan, const string& path, const bool required)
        {
            const fs::path source = resolve(plan.engine_directory, path);
            error_code ec;
            if (!fs::exists(source, ec))
            {
                if (required)
                {
                    warn("Extra folder not found: " + path);
                }
                return;
            }

            if (fs::is_directory(source, ec))
            {
                add_directory(plan, source, true);
            }
            else
            {
                add_file(plan, source);
            }
        }

        bool is_runtime_library(const fs::path& path, const bool include_symbols)
        {
            const string name = to_lower(path.filename().string());
#ifdef _WIN32
            if (to_lower(path.extension().string()) != ".dll")
            {
                return false;
            }
            // the d3d12 debug layer only matters when someone is debugging the game
            return include_symbols || name != "d3d12sdklayers.dll";
#else
            (void)include_symbols;
            return name.find(".so") != string::npos;
#endif
        }

        void collect(Plan& plan, const Options& options, const string& executable_file)
        {
            set_step("Collecting the engine", 0.01f);
            const fs::path binary = plan.engine_directory / (options.engine_binary.empty() ? current_engine_binary() : options.engine_binary);
            plan.files[executable_file] = binary;

            error_code ec;
            for (const fs::directory_entry& entry : fs::directory_iterator(plan.engine_directory, ec))
            {
                if (entry.is_regular_file(ec) && is_runtime_library(entry.path(), options.include_symbols))
                {
                    plan.files[entry.path().filename().string()] = entry.path();
                }
            }

            if (options.include_symbols)
            {
                fs::path symbols = binary;
                symbols.replace_extension(".pdb");
                if (fs::exists(symbols, ec))
                {
                    plan.files[symbols.filename().string()] = symbols;
                }
            }

            // development builds compile shaders from the source tree, the copy in data/ can be stale and
            // no longer match the engine's constant buffers, so ship the directory the engine actually reads
            const fs::path data_directory    = plan.engine_directory / "data";
            const fs::path shaders_in_data   = canonical_path(data_directory / "shaders");
            const fs::path shaders_in_use    = canonical_path(resolve(plan.engine_directory, spartan::ResourceCache::GetResourceDirectory(spartan::ResourceDirectory::Shaders)));
            const bool shaders_from_source   = shaders_in_use != shaders_in_data && fs::is_directory(shaders_in_use, ec);
            for (const fs::directory_entry& entry : fs::directory_iterator(data_directory, ec))
            {
                if (shaders_from_source && canonical_path(entry.path()) == shaders_in_data)
                {
                    continue;
                }
                if (entry.is_directory(ec))
                {
                    add_directory(plan, entry.path(), false);
                }
                else if (entry.is_regular_file(ec))
                {
                    add_file(plan, entry.path(), false);
                }
            }
            if (shaders_from_source)
            {
                error_code walk_ec;
                for (fs::recursive_directory_iterator it(shaders_in_use, fs::directory_options::skip_permission_denied, walk_ec), end; it != end; it.increment(walk_ec))
                {
                    if (walk_ec)
                    {
                        break;
                    }
                    if (it->is_regular_file(walk_ec))
                    {
                        plan.files["data/shaders/" + fs::relative(it->path(), shaders_in_use, walk_ec).generic_string()] = it->path();
                    }
                }
            }

            set_step("Collecting worlds", 0.03f);
            for (size_t i = 0; i < options.worlds.size(); ++i)
            {
                const fs::path world = resolve(plan.engine_directory, options.worlds[i]);
                string destination;
                if (!destination_for(plan, world, destination))
                {
                    throw BuildFailed{ "The world has to live inside the repository: " + options.worlds[i] };
                }
                add_file(plan, world);
                if (i == 0)
                {
                    plan.start_world = destination;
                }

                // a <world>.lua beside the world runs on every load without being named anywhere
                fs::path companion = world;
                companion.replace_extension(".lua");
                if (fs::is_regular_file(companion, ec))
                {
                    add_file(plan, companion);
                }

                // materials and textures here are found by the scan, but nothing names the folder's other files
                const fs::path resources = resolve(plan.engine_directory, spartan::World::GetResourceDirectory(options.worlds[i]));
                if (fs::is_directory(resources, ec))
                {
                    add_directory(plan, resources, true);
                }
            }

            // shared scripts are small and may dofile each other, but only the ones a world names are followed,
            // the rest belong to other worlds and would drag their assets along
            const fs::path scripts = plan.engine_directory / "project" / "scripts";
            if (fs::is_directory(scripts, ec))
            {
                add_directory(plan, scripts, false);
            }
            if (options.asset_mode == AssetMode::EntireProject)
            {
                set_step("Collecting the project folder", 0.04f);
                add_directory(plan, plan.engine_directory / "project", true);
            }
            for (const string& folder : options.extra_folders)
            {
                if (!trim(folder).empty())
                {
                    add_path(plan, trim(folder), true);
                }
            }

            set_step("Following asset references", 0.05f);
            process_scan_queue(plan);

            for (const DefaultAssetRule& rule : default_asset_rules())
            {
                if (plan.component_tags.count(rule.component_tag))
                {
                    for (const char* path : rule.paths)
                    {
                        add_path(plan, path, false);
                    }
                }
            }
            process_scan_queue(plan);
        }

        void apply_rewrites(string& content, const vector<pair<string, string>>& rewrites)
        {
            for (const auto& [from, to] : rewrites)
            {
                size_t position = 0;
                while ((position = content.find(from, position)) != string::npos)
                {
                    content.replace(position, from.size(), to);
                    position += to.size();
                }
            }
        }

        bool is_up_to_date(const fs::path& source, const fs::path& target)
        {
            error_code ec;
            if (!fs::exists(target, ec) || fs::file_size(source, ec) != fs::file_size(target, ec))
            {
                return false;
            }
            return fs::last_write_time(target, ec) >= fs::last_write_time(source, ec);
        }

        void copy_files(Plan& plan)
        {
            uint64_t bytes_total = 0;
            error_code ec;
            for (const auto& [destination, source] : plan.files)
            {
                bytes_total += fs::file_size(source, ec);
            }
            {
                lock_guard lock(report_mutex);
                report.files_total = plan.files.size();
                report.bytes_total = bytes_total;
            }

            set_step("Copying files", 0.1f);
            uint64_t bytes_done = 0;
            for (const auto& [destination, source] : plan.files)
            {
                check_cancel();
                const fs::path target = plan.output_directory / fs::path(destination);
                fs::create_directories(target.parent_path(), ec);

                bool copied = false;
                const auto rewrite = plan.rewrites.find(destination);
                if (rewrite != plan.rewrites.end())
                {
                    string content;
                    string existing;
                    if (!spartan::FileSystem::ReadFile(source.string(), content))
                    {
                        throw BuildFailed{ "Could not read " + source.generic_string() };
                    }
                    apply_rewrites(content, rewrite->second);
                    if (!spartan::FileSystem::ReadFile(target.string(), existing) || existing != content)
                    {
                        if (!spartan::FileSystem::WriteFile(target.string(), content))
                        {
                            throw BuildFailed{ "Could not write " + target.generic_string() };
                        }
                        copied = true;
                    }
                }
                else if (!is_up_to_date(source, target))
                {
                    if (!fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec))
                    {
                        throw BuildFailed{ "Could not copy " + source.generic_string() + ": " + ec.message() };
                    }
                    copied = true;
                }

                bytes_done += fs::file_size(source, ec);
                {
                    lock_guard lock(report_mutex);
                    copied ? report.files_copied++ : report.files_skipped++;
                    report.detail   = destination;
                    report.fraction = 0.1f + 0.8f * (bytes_total ? static_cast<float>(static_cast<double>(bytes_done) / bytes_total) : 1.0f);
                }
            }
        }

        // the output is exactly the plan, files from older builds (or a test run's logs and settings) go
        void remove_stale_files(const Plan& plan, const set<string>& generated)
        {
            set_step("Removing stale files", 0.91f);
            error_code ec;
            vector<fs::path> stale;
            vector<fs::path> directories;
            for (fs::recursive_directory_iterator it(plan.output_directory, ec), end; it != end; it.increment(ec))
            {
                if (ec)
                {
                    break;
                }

                const string relative = it->path().lexically_relative(plan.output_directory).generic_string();
                if (it->is_directory(ec))
                {
                    directories.push_back(it->path());
                }
                else if (!plan.files.count(relative) && !generated.count(relative))
                {
                    stale.push_back(it->path());
                }
            }

            for (const fs::path& path : stale)
            {
                if (fs::remove(path, ec))
                {
                    lock_guard lock(report_mutex);
                    report.files_removed++;
                }
            }

            // deepest first so parents empty out after their children
            sort(directories.begin(), directories.end(), [](const fs::path& a, const fs::path& b) { return a.generic_string().size() > b.generic_string().size(); });
            for (const fs::path& directory : directories)
            {
                if (fs::is_empty(directory, ec))
                {
                    fs::remove(directory, ec);
                }
            }
        }

        void write_steam_scripts(const Plan& plan, const Options& options, const string& executable_file)
        {
            set_step("Writing SteamPipe scripts", 0.93f);
            const fs::path directory = plan.output_directory.parent_path() / (plan.output_directory.filename().string() + "_steampipe");
            error_code ec;
            fs::create_directories(directory, ec);

            const string app   = to_string(options.steam_app_id);
            const string depot = to_string(options.steam_depot_id);
            const string vdf   = "app_build_" + app + ".vdf";

            string app_build;
            app_build += "\"AppBuild\"\n{\n";
            app_build += "\t\"AppID\" \"" + app + "\"\n";
            app_build += "\t\"Desc\" \"" + options.name + " " + options.version + "\"\n";
            app_build += "\t\"BuildOutput\" \"output/\"\n";
            app_build += "\t\"ContentRoot\" \"" + plan.output_directory.generic_string() + "/\"\n";
            app_build += "\t\"SetLive\" \"\"\n";
            app_build += "\t\"Depots\"\n\t{\n";
            app_build += "\t\t\"" + depot + "\"\n\t\t{\n";
            app_build += "\t\t\t\"FileMapping\"\n\t\t\t{\n";
            app_build += "\t\t\t\t\"LocalPath\" \"*\"\n";
            app_build += "\t\t\t\t\"DepotPath\" \".\"\n";
            app_build += "\t\t\t\t\"recursive\" \"1\"\n";
            app_build += "\t\t\t}\n";
            app_build += "\t\t\t\"FileExclusion\" \"*.pdb\"\n";
            app_build += "\t\t\t\"FileExclusion\" \"steam_appid.txt\"\n";
            app_build += "\t\t}\n\t}\n}\n";
            spartan::FileSystem::WriteFile((directory / vdf).string(), app_build);

            string upload_bat;
            upload_bat += "@echo off\r\n";
            upload_bat += "rem uploads the build to steam with steamcmd, https://developer.valvesoftware.com/wiki/SteamCMD\r\n";
            upload_bat += "set STEAMCMD=steamcmd\r\n";
            upload_bat += "if exist \"%~dp0steamcmd\\steamcmd.exe\" set STEAMCMD=\"%~dp0steamcmd\\steamcmd.exe\"\r\n";
            upload_bat += "set /p STEAM_USER=\"Steamworks account: \"\r\n";
            upload_bat += "%STEAMCMD% +login %STEAM_USER% +run_app_build \"%~dp0" + vdf + "\" +quit\r\n";
            upload_bat += "pause\r\n";
            spartan::FileSystem::WriteFile((directory / "upload.bat").string(), upload_bat);

            string upload_sh;
            upload_sh += "#!/bin/sh\n";
            upload_sh += "# uploads the build to steam with steamcmd, https://developer.valvesoftware.com/wiki/SteamCMD\n";
            upload_sh += "cd \"$(dirname \"$0\")\"\n";
            upload_sh += "printf \"Steamworks account: \" && read STEAM_USER\n";
            upload_sh += "steamcmd +login \"$STEAM_USER\" +run_app_build \"$(pwd)/" + vdf + "\" +quit\n";
            spartan::FileSystem::WriteFile((directory / "upload.sh").string(), upload_sh);

            string readme;
            readme += options.name + " " + options.version + " - SteamPipe upload\n\n";
            readme += "1. In Steamworks, open App " + app + " > Installation > General and add a launch option:\n";
            readme += "   Executable: " + executable_file + "\n";
            readme += "   Working directory: (leave empty)\n";
            readme += "2. Make sure depot " + depot + " exists under SteamPipe > Depots.\n";
            readme += "3. Run upload.bat (Windows) or upload.sh, log in with a Steamworks account that may upload builds.\n";
            readme += "4. In Steamworks, open SteamPipe > Builds and set the uploaded build live on a branch.\n\n";
            readme += "The content root is " + plan.output_directory.generic_string() + "\n";
            readme += "Debug symbols (*.pdb) and steam_appid.txt are excluded from the upload.\n";
            spartan::FileSystem::WriteFile((directory / "README.txt").string(), readme);

            lock_guard lock(report_mutex);
            report.steam_directory = directory.generic_string();
        }

#ifdef _WIN32
        bool run_process(const wstring& command_line, const fs::path& working_directory, const bool wait)
        {
            STARTUPINFOW startup_info = {};
            startup_info.cb           = sizeof(startup_info);
            PROCESS_INFORMATION process = {};
            vector<wchar_t> buffer(command_line.begin(), command_line.end());
            buffer.push_back(L'\0');
            const DWORD flags = wait ? CREATE_NO_WINDOW : 0;
            if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, flags, nullptr, working_directory.wstring().c_str(), &startup_info, &process))
            {
                return false;
            }

            DWORD exit_code = 0;
            if (wait)
            {
                WaitForSingleObject(process.hProcess, INFINITE);
                GetExitCodeProcess(process.hProcess, &exit_code);
            }
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return exit_code == 0;
        }
#endif

        void create_archive(const Plan& plan, const Options& options, const string& executable_name)
        {
            set_step("Compressing the archive", 0.95f);
            const string base_name = executable_name + "_" + options.version;
#ifdef _WIN32
            const fs::path archive = plan.output_directory.parent_path() / (base_name + ".zip");
            const wstring command  = L"tar.exe -a -c -f \"" + archive.wstring() + L"\" -C \"" + plan.output_directory.wstring() + L"\" .";
            error_code ec;
            fs::remove(archive, ec);
            const bool success = run_process(command, plan.output_directory.parent_path(), true);
#else
            const fs::path archive = plan.output_directory.parent_path() / (base_name + ".tar.gz");
            const string command   = "tar -czf \"" + archive.string() + "\" -C \"" + plan.output_directory.string() + "\" .";
            const bool success     = std::system(command.c_str()) == 0;
#endif
            if (!success)
            {
                warn("The archive could not be created, the build folder is complete");
                return;
            }

            lock_guard lock(report_mutex);
            report.archive_path = archive.generic_string();
        }

        void run(Options options)
        {
            const auto time_start = chrono::steady_clock::now();
            Plan plan;
            plan.engine_directory = engine_directory();
            plan.root_directory   = plan.engine_directory.parent_path();
            plan.output_directory = canonical_path(fs::path(options.output_directory));
            plan.include_cache    = options.include_cache;

            const string executable_name = resolve_executable_name(options);
#ifdef _WIN32
            const string executable_file = executable_name + ".exe";
#else
            const string executable_file = executable_name;
#endif

            State state = State::Succeeded;
            string error;
            try
            {
                error_code ec;
                fs::create_directories(plan.output_directory, ec);
                if (!fs::is_directory(plan.output_directory, ec))
                {
                    throw BuildFailed{ "Could not create the output folder " + plan.output_directory.generic_string() };
                }

                collect(plan, options, executable_file);
                check_cancel();

                // written first, it is also what marks the folder as a build that may be cleaned next time
                spartan::GameManifestData manifest;
                manifest.name          = options.name;
                manifest.version       = options.version;
                manifest.company       = options.company;
                manifest.start_world   = plan.start_world;
                manifest.window_mode   = options.window_mode;
                manifest.window_width  = options.window_width;
                manifest.window_height = options.window_height;
                manifest.steam_enabled = options.steam_enabled;
                manifest.steam_app_id  = options.steam_enabled ? options.steam_app_id : 0;
                if (!spartan::GameManifest::Save((plan.output_directory / spartan::GameManifest::file_name).string(), manifest))
                {
                    throw BuildFailed{ "Could not write the game manifest" };
                }

                set<string> generated = { spartan::GameManifest::file_name };
                if (options.steam_enabled && options.steam_appid_file)
                {
                    // lets the game talk to a running steam client when started outside of steam, steampipe skips it
                    spartan::FileSystem::WriteFile((plan.output_directory / "steam_appid.txt").string(), to_string(options.steam_app_id));
                    generated.insert("steam_appid.txt");
                }

                copy_files(plan);
                remove_stale_files(plan, generated);

                if (options.steam_enabled && options.steam_scripts)
                {
                    write_steam_scripts(plan, options, executable_file);
                }

                if (options.create_archive)
                {
                    create_archive(plan, options, executable_name);
                }
            }
            catch (const BuildCancelled&)
            {
                state = State::Cancelled;
            }
            catch (const BuildFailed& failure)
            {
                state = State::Failed;
                error = failure.message;
            }
            catch (const exception& exception)
            {
                state = State::Failed;
                error = exception.what();
            }

            const double seconds = chrono::duration<double>(chrono::steady_clock::now() - time_start).count();
            {
                lock_guard lock(report_mutex);
                report.state           = state;
                report.error           = error;
                report.seconds         = seconds;
                report.fraction        = 1.0f;
                report.detail.clear();
                report.executable_path = (plan.output_directory / executable_file).generic_string();
                report.step            = state == State::Succeeded ? "Build complete" : state == State::Cancelled ? "Build cancelled" : "Build failed";
            }

            if (state == State::Succeeded)
            {
                SP_LOG_INFO("Game build: %s %s packaged to %s in %.1f s", options.name.c_str(), options.version.c_str(), plan.output_directory.generic_string().c_str(), seconds);
                if (options.launch_when_done)
                {
                    launch((plan.output_directory / executable_file).generic_string());
                }
            }
            else if (state == State::Failed)
            {
                SP_LOG_ERROR("Game build failed: %s", error.c_str());
            }

            running.store(false, memory_order_release);
        }

        string read_child(const pugi::xml_node& node, const char* name, const string& fallback)
        {
            const pugi::xml_node child = node.child(name);
            return child ? string(child.text().as_string()) : fallback;
        }
    }

    string validate(const Options& options)
    {
        if (trim(options.name).empty())
        {
            return "Give the game a name.";
        }

        if (options.worlds.empty())
        {
            return "Pick at least one world, the first one is where the game starts.";
        }

        const fs::path engine = engine_directory();
        error_code ec;
        for (const string& world : options.worlds)
        {
            if (!fs::is_regular_file(resolve(engine, world), ec))
            {
                return "World not found: " + world;
            }
        }

        if (trim(options.output_directory).empty())
        {
            return "Choose a folder for the build.";
        }

        const fs::path output = canonical_path(fs::path(options.output_directory));
        if (!output.is_absolute())
        {
            return "The output folder has to be an absolute path.";
        }

        if (is_within(output, engine) || is_within(engine, output))
        {
            return "The output folder can't be inside the engine folder or contain it.";
        }

        if (fs::exists(output, ec))
        {
            if (!fs::is_directory(output, ec))
            {
                return "The output path is a file, choose a folder.";
            }

            if (!fs::is_empty(output, ec) && !fs::exists(output / spartan::GameManifest::file_name, ec))
            {
                return "The output folder has other files in it. Choose an empty folder or one that holds a previous build.";
            }
        }

        const string binary = options.engine_binary.empty() ? current_engine_binary() : options.engine_binary;
        if (!fs::is_regular_file(engine / binary, ec))
        {
            return "Engine binary not found: " + binary;
        }

        if (options.steam_enabled && options.steam_app_id == 0)
        {
            return "Enter the Steam App ID.";
        }

        if (options.window_mode == spartan::GameWindowMode::Windowed && (options.window_width < 640 || options.window_height < 360))
        {
            return "The window has to be at least 640x360.";
        }

        return "";
    }

    bool start(const Options& options)
    {
        if (running.load(memory_order_acquire) || !validate(options).empty())
        {
            return false;
        }

        if (worker.joinable())
        {
            worker.join();
        }

        {
            lock_guard lock(report_mutex);
            report                  = Report{};
            report.state            = State::Running;
            report.step             = "Starting";
            report.output_directory = canonical_path(fs::path(options.output_directory)).generic_string();
        }

        save_options(options);
        cancel_requested.store(false, memory_order_relaxed);
        running.store(true, memory_order_release);
        worker = thread(run, options);
        return true;
    }

    void cancel()
    {
        cancel_requested.store(true, memory_order_relaxed);
    }

    bool is_running()
    {
        return running.load(memory_order_acquire);
    }

    Report get_report()
    {
        lock_guard lock(report_mutex);
        return report;
    }

    void shutdown()
    {
        cancel();
        if (worker.joinable())
        {
            worker.join();
        }
    }

    Options load_options()
    {
        Options options;
        const fs::path engine = engine_directory();
        options.output_directory = (engine.parent_path().parent_path() / "spartan_game").generic_string();

        pugi::xml_document document;
        if (!document.load_file((engine / options_file_name).string().c_str()))
        {
            const string& world = spartan::World::GetFilePath();
            if (!world.empty())
            {
                options.worlds.push_back(normalize_world_path(world));
            }
            return options;
        }

        const pugi::xml_node root = document.child("GameBuild");
        options.name             = read_child(root, "name", options.name);
        options.version          = read_child(root, "version", options.version);
        options.company          = read_child(root, "company", options.company);
        options.executable_name  = read_child(root, "executable_name", options.executable_name);
        options.output_directory = read_child(root, "output_directory", options.output_directory);
        options.engine_binary    = read_child(root, "engine_binary", options.engine_binary);
        options.window_mode      = spartan::GameManifest::WindowModeFromString(read_child(root, "window_mode", "fullscreen"));
        options.window_width     = root.child("window_width").text().as_uint(options.window_width);
        options.window_height    = root.child("window_height").text().as_uint(options.window_height);
        options.asset_mode       = read_child(root, "asset_mode", "referenced") == "project" ? AssetMode::EntireProject : AssetMode::Referenced;
        options.include_symbols  = root.child("include_symbols").text().as_bool(options.include_symbols);
        options.include_cache    = root.child("include_cache").text().as_bool(options.include_cache);
        options.steam_enabled    = root.child("steam_enabled").text().as_bool(options.steam_enabled);
        options.steam_app_id     = root.child("steam_app_id").text().as_uint(options.steam_app_id);
        options.steam_depot_id   = root.child("steam_depot_id").text().as_uint(options.steam_depot_id);
        options.steam_appid_file = root.child("steam_appid_file").text().as_bool(options.steam_appid_file);
        options.steam_scripts    = root.child("steam_scripts").text().as_bool(options.steam_scripts);
        options.create_archive   = root.child("create_archive").text().as_bool(options.create_archive);
        options.launch_when_done = root.child("launch_when_done").text().as_bool(options.launch_when_done);
        for (pugi::xml_node world : root.child("worlds").children("world"))
        {
            options.worlds.push_back(normalize_world_path(world.text().as_string()));
        }
        for (pugi::xml_node folder : root.child("extra_folders").children("folder"))
        {
            options.extra_folders.push_back(folder.text().as_string());
        }
        return options;
    }

    void save_options(const Options& options)
    {
        pugi::xml_document document;
        pugi::xml_node root = document.append_child("GameBuild");
        auto write = [&root](const char* name, const string& value)
        {
            root.append_child(name).text().set(value.c_str());
        };
        write("name", options.name);
        write("version", options.version);
        write("company", options.company);
        write("executable_name", options.executable_name);
        write("output_directory", options.output_directory);
        write("engine_binary", options.engine_binary);
        write("window_mode", spartan::GameManifest::WindowModeToString(options.window_mode));
        write("window_width", to_string(options.window_width));
        write("window_height", to_string(options.window_height));
        write("asset_mode", options.asset_mode == AssetMode::EntireProject ? "project" : "referenced");
        write("include_symbols", options.include_symbols ? "true" : "false");
        write("include_cache", options.include_cache ? "true" : "false");
        write("steam_enabled", options.steam_enabled ? "true" : "false");
        write("steam_app_id", to_string(options.steam_app_id));
        write("steam_depot_id", to_string(options.steam_depot_id));
        write("steam_appid_file", options.steam_appid_file ? "true" : "false");
        write("steam_scripts", options.steam_scripts ? "true" : "false");
        write("create_archive", options.create_archive ? "true" : "false");
        write("launch_when_done", options.launch_when_done ? "true" : "false");
        pugi::xml_node worlds = root.append_child("worlds");
        for (const string& world : options.worlds)
        {
            worlds.append_child("world").text().set(world.c_str());
        }
        pugi::xml_node folders = root.append_child("extra_folders");
        for (const string& folder : options.extra_folders)
        {
            folders.append_child("folder").text().set(folder.c_str());
        }
        document.save_file((engine_directory() / options_file_name).string().c_str(), "  ");
    }

    string normalize_world_path(const string& path)
    {
        if (trim(path).empty())
        {
            return "";
        }

        const fs::path engine   = engine_directory();
        const fs::path absolute = resolve(engine, trim(path));
        const fs::path relative = absolute.lexically_relative(engine);
        return relative.empty() ? absolute.generic_string() : relative.generic_string();
    }

    vector<string> find_worlds()
    {
        const fs::path engine = engine_directory();
        vector<string> worlds;
        set<string> seen;
        auto add = [&](const fs::path& path)
        {
            const string normalized = normalize_world_path(path.string());
            if (!normalized.empty() && seen.insert(to_lower(normalized)).second)
            {
                worlds.push_back(normalized);
            }
        };

        error_code ec;
        for (const fs::path& directory : { engine.parent_path() / "worlds", engine / "project" })
        {
            for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec))
            {
                if (entry.is_regular_file(ec) && to_lower(entry.path().extension().string()) == ".world")
                {
                    add(entry.path());
                }
            }
        }

        const string& current = spartan::World::GetFilePath();
        if (!current.empty())
        {
            add(resolve(engine, current));
        }

        sort(worlds.begin(), worlds.end());
        return worlds;
    }

    vector<string> find_engine_binaries()
    {
        const fs::path engine = engine_directory();
        vector<string> binaries;
        error_code ec;
        for (const fs::directory_entry& entry : fs::directory_iterator(engine, ec))
        {
            if (!entry.is_regular_file(ec))
            {
                continue;
            }

            const string name = entry.path().filename().string();
            if (to_lower(name).rfind("spartan", 0) != 0)
            {
                continue;
            }
#ifdef _WIN32
            if (to_lower(entry.path().extension().string()) == ".exe")
            {
                binaries.push_back(name);
            }
#else
            if (!entry.path().has_extension() && access(entry.path().c_str(), X_OK) == 0)
            {
                binaries.push_back(name);
            }
#endif
        }
        sort(binaries.begin(), binaries.end());
        return binaries;
    }

    string current_engine_binary()
    {
#ifdef _WIN32
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        return length == 0 || length >= MAX_PATH ? string() : fs::path(path).filename().string();
#else
        error_code ec;
        return fs::read_symlink("/proc/self/exe", ec).filename().string();
#endif
    }

    string resolve_executable_name(const Options& options)
    {
        const string source = trim(options.executable_name).empty() ? trim(options.name) : trim(options.executable_name);
        string name;
        for (const char c : source)
        {
            if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')
            {
                name += c;
            }
            else if (c == ' ' && !name.empty() && name.back() != '_')
            {
                name += '_';
            }
        }
        while (!name.empty() && (name.back() == '_' || name.back() == '.'))
        {
            name.pop_back();
        }
        return name.empty() ? "game" : name;
    }

    bool launch(const string& executable_path)
    {
        const fs::path executable(executable_path);
        error_code ec;
        if (!fs::is_regular_file(executable, ec))
        {
            SP_LOG_ERROR("Game build: nothing to launch at %s", executable_path.c_str());
            return false;
        }
#ifdef _WIN32
        return run_process(L"\"" + executable.wstring() + L"\"", executable.parent_path(), false);
#else
        const string command = "cd \"" + executable.parent_path().string() + "\" && \"./" + executable.filename().string() + "\" &";
        return std::system(command.c_str()) == 0;
#endif
    }

    void open_folder(const string& directory)
    {
        SDL_OpenURL(("file:///" + fs::path(directory).generic_string()).c_str());
    }
}
