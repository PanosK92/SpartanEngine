/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================
#include "pch.h"
#include "Updates.h"
#include "imgui/ImGui_Extension.h"
#include "imgui/ImGui_Style.h"
#include "widgets/Viewport.h"
#include "core/ProgressTracker.h"
#include "file_system/FileSystem.h"
#include <filesystem>
#include <mutex>
SP_WARNINGS_OFF
#include "io/pugixml.hpp"
SP_WARNINGS_ON
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    Editor* editor = nullptr;

    // github, a check pins everything it reads to the latest commit so listings and downloads cannot disagree
    const char* branch_url       = "https://api.github.com/repos/PanosK92/SpartanEngine/branches/master";
    const char* worlds_list_url  = "https://api.github.com/repos/PanosK92/SpartanEngine/contents/worlds?ref=";
    const char* raw_url          = "https://raw.githubusercontent.com/PanosK92/SpartanEngine/";
    const char* releases_api_url = "https://api.github.com/repos/PanosK92/SpartanEngine/releases?per_page=1";
    const char* releases_url     = "https://github.com/PanosK92/SpartanEngine/releases";

    // engine releases
    const char* engine_skip_file = "settings/spartan_update.txt";

    // the project package is hosted on dropbox, the repository only says which archive is current
    const char* package_repository_path = "data/project_package.xml";
    const char* package_file_name       = "project_package.xml";
    const char* project_install_dir     = "project";
    const char* project_staging_dir     = "project_staging";
    const char* project_old_dir         = "project_old";
    const char* project_archive         = "project/project.7z";
    const char* project_download_dir    = "cache/project_download/";
    const char* project_manifest        = "settings/spartan_project_manifest.xml";

    // world files are versioned in the repository, not in the project package, so they are fetched file by file
    const char* worlds_staging_dir = "cache/worlds_download/";
    // the world launcher looks in ../worlds/ relative to the executable, the same layout as a cloned repository
    const char* worlds_install_dir = "../worlds/";
    const char* worlds_backup_dir  = "../worlds_backup/";
    const char* worlds_manifest    = "settings/spartan_worlds_manifest.xml";

    const float status_duration = 3.0f;

    // why the last install failed, the install worker writes it and the prompts show it, so a failure is never just "failed"
    mutex install_error_mutex;
    string install_error;

    void set_install_error(const string& error)
    {
        SP_LOG_ERROR("%s", error.c_str());
        lock_guard<mutex> lock(install_error_mutex);
        install_error = error;
    }

    // the message of a system error is localized, the code is what can be looked up
    string describe_error(const error_code& error)
    {
        return error.message() + " (error " + to_string(error.value()) + ")";
    }

    string lowercase_copy(string value)
    {
        transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        return value;
    }

    int64_t get_current_timestamp()
    {
        return static_cast<int64_t>(chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count());
    }

    string sha1_hex(const string& data)
    {
        const auto rotate_left = [](uint32_t value, int bits)
        {
            return (value << bits) | (value >> (32 - bits));
        };

        uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };

        string message           = data;
        const uint64_t bit_count = static_cast<uint64_t>(data.size()) * 8;
        message.push_back(static_cast<char>(0x80));
        while (message.size() % 64 != 56)
        {
            message.push_back('\0');
        }
        for (int i = 7; i >= 0; i--)
        {
            message.push_back(static_cast<char>((bit_count >> (i * 8)) & 0xff));
        }

        for (size_t chunk = 0; chunk < message.size(); chunk += 64)
        {
            uint32_t w[80];
            for (int i = 0; i < 16; i++)
            {
                const unsigned char* p = reinterpret_cast<const unsigned char*>(&message[chunk + i * 4]);
                w[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
            }
            for (int i = 16; i < 80; i++)
            {
                w[i] = rotate_left(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            }

            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; i++)
            {
                uint32_t f = 0;
                uint32_t k = 0;
                if (i < 20)
                {
                    f = (b & c) | (~b & d);
                    k = 0x5A827999;
                }
                else if (i < 40)
                {
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1;
                }
                else if (i < 60)
                {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDC;
                }
                else
                {
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6;
                }

                const uint32_t temp = rotate_left(a, 5) + f + e + k + w[i];
                e = d;
                d = c;
                c = rotate_left(b, 30);
                b = a;
                a = temp;
            }

            h[0] += a;
            h[1] += b;
            h[2] += c;
            h[3] += d;
            h[4] += e;
        }

        char hex[41];
        snprintf(hex, sizeof(hex), "%08x%08x%08x%08x%08x", h[0], h[1], h[2], h[3], h[4]);
        return hex;
    }

    // the same hash git and the github listing use, so local files compare without downloading anything
    string git_blob_sha1(const string& path)
    {
        string contents;
        if (!spartan::FileSystem::IsFile(path) || !spartan::FileSystem::ReadFile(path, contents))
        {
            return {};
        }

        string blob = "blob " + to_string(contents.size());
        blob.push_back('\0');
        blob += contents;
        return sha1_hex(blob);
    }

    string json_string_value(const string& json, const string& key, size_t start = 0)
    {
        const string needle = "\"" + key + "\"";
        const size_t key_pos = json.find(needle, start);
        if (key_pos == string::npos)
        {
            return {};
        }

        size_t value_start = json.find(':', key_pos + needle.size());
        if (value_start == string::npos)
        {
            return {};
        }
        value_start = json.find_first_not_of(" \t\r\n", value_start + 1);
        if (value_start == string::npos || json[value_start] != '"')
        {
            return {};
        }

        const size_t value_end = json.find('"', value_start + 1);
        if (value_end == string::npos)
        {
            return {};
        }

        return json.substr(value_start + 1, value_end - value_start - 1);
    }

    bool fetch_latest_commit(string& commit)
    {
        string branch;
        if (!spartan::FileSystem::DownloadToString(branch_url, branch))
        {
            SP_LOG_ERROR("Failed to query the latest commit on GitHub: %s", branch_url);
            return false;
        }

        commit = json_string_value(branch, "sha", branch.find("\"commit\""));
        if (commit.size() != 40)
        {
            SP_LOG_ERROR("The GitHub branch response has no commit");
            return false;
        }

        return true;
    }

    namespace engine
    {
        struct Release
        {
            string tag;
            string url;
        };

        bool dismissed_this_session = false;
        bool prompt_pending         = false;
        Release release;

        string skip_path()
        {
            return spartan::FileSystem::GetExecutableDirectory() + "/" + engine_skip_file;
        }

        string read_skipped_tag()
        {
            string tag;
            const string path = skip_path();
            if (!spartan::FileSystem::Exists(path) || !spartan::FileSystem::ReadFile(path, tag))
            {
                return {};
            }

            while (!tag.empty() && (tag.back() == '\n' || tag.back() == '\r' || tag.back() == ' '))
            {
                tag.pop_back();
            }

            return tag;
        }

        int64_t to_minutes(int year, int month, int day, int hour, int minute)
        {
            return (static_cast<int64_t>(year) * 366 + month * 31 + day) * 24 * 60 + hour * 60 + minute;
        }

        bool parse_tag(const string& tag, int& year, int& month, int& day, int& hour, int& minute)
        {
            string value = tag;
            if (!value.empty() && (value[0] == 'v' || value[0] == 'V'))
            {
                value.erase(value.begin());
            }

            year = month = day = hour = minute = 0;

            auto parse_int = [](const char*& p, int& out) -> bool
            {
                if (*p < '0' || *p > '9')
                {
                    return false;
                }

                out = 0;
                while (*p >= '0' && *p <= '9')
                {
                    out = out * 10 + (*p - '0');
                    p++;
                }

                return true;
            };

            const char* p = value.c_str();
            int* fields[] = { &year, &month, &day, &hour, &minute };
            for (int i = 0; i < 5; i++)
            {
                if (!parse_int(p, *fields[i]))
                {
                    return false;
                }

                if (i < 4)
                {
                    if (*p != '.')
                    {
                        return false;
                    }
                    p++;
                }
            }

            return *p == '\0';
        }

        // returns true and fills the release when github has a build newer than this one
        bool find_newer_release(bool manual, Release& newer)
        {
            string json;
            if (!spartan::FileSystem::DownloadToString(releases_api_url, json))
            {
                return false;
            }

            const string tag = json_string_value(json, "tag_name");
            if (tag.empty() || (!manual && tag == read_skipped_tag()))
            {
                return false;
            }

            int year = 0, month = 0, day = 0, hour = 0, minute = 0;
            if (!parse_tag(tag, year, month, day, hour, minute))
            {
                return false;
            }

            const int64_t remote_minutes = to_minutes(year, month, day, hour, minute);
            const int64_t local_minutes  = to_minutes(
                spartan::version::year(),
                spartan::version::month(),
                spartan::version::day(),
                spartan::version::hour(),
                spartan::version::minute()
            );

            // ci builds carry the exact release stamp, so any newer tag is a real update
            if (remote_minutes <= local_minutes)
            {
                return false;
            }

            newer.tag = tag;
            newer.url = json_string_value(json, "html_url");
            if (newer.url.empty())
            {
                newer.url = releases_url;
            }

            return true;
        }

        void open_releases()
        {
            spartan::FileSystem::OpenUrl(release.url.empty() ? releases_url : release.url);
            prompt_pending         = false;
            dismissed_this_session = true;
        }

        void skip()
        {
            prompt_pending         = false;
            dismissed_this_session = true;
            if (!release.tag.empty())
            {
                spartan::FileSystem::WriteFile(skip_path(), release.tag);
            }
        }

        bool window()
        {
            if (!prompt_pending || dismissed_this_session)
            {
                return false;
            }

            const float dpi = spartan::Window::GetDpiScale();
            ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);

            bool open = true;
            const ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoMove;
            if (ImGui::Begin("Engine update available", &open, flags))
            {
                float content_width = 500.0f * dpi;
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + content_width);

                ImGui::Text("A newer engine build is available.");
                ImGui::Spacing();
                ImGui::TextDisabled("You have: %s", spartan::version::c_str());
                ImGui::TextDisabled("Latest: Spartan v%s", release.tag.c_str());

                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                float button_width = 140.0f;
                float total_width  = button_width * 2.0f + ImGui::GetStyle().ItemSpacing.x;
                float offset_x     = (ImGui::GetContentRegionAvail().x - total_width) * 0.5f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);

                if (ImGui::Button("Open", ImVec2(button_width, 0.0f)))
                {
                    open_releases();
                }
                ImGui::SameLine();
                if (ImGui::Button("Skip", ImVec2(button_width, 0.0f)))
                {
                    skip();
                }
            }
            ImGui::End();

            if (!open)
            {
                skip();
            }

            return true;
        }
    }

    namespace project
    {
        struct Package
        {
            string url;
            string sha256;

            bool empty() const
            {
                return sha256.empty();
            }
        };

        // what the project folder was extracted from, and the update the user said no to
        struct Manifest
        {
            string installed;
            string skipped;
            // identifies project.7z without hashing gigabytes on every start
            uint64_t archive_size = 0;
            int64_t archive_time  = 0;
        };

        bool parse_package(const string& xml, Package& package)
        {
            pugi::xml_document doc;
            if (!doc.load_string(xml.c_str()))
            {
                return false;
            }

            pugi::xml_node node = doc.child("ProjectPackage");
            Package parsed;
            parsed.url    = node.attribute("url").as_string();
            parsed.sha256 = lowercase_copy(node.attribute("sha256").as_string());
            if (parsed.url.empty() || parsed.sha256.size() != 64)
            {
                return false;
            }

            package = parsed;
            return true;
        }

        // the copy that shipped with this build, used when github cannot be reached
        bool local_package(Package& package)
        {
            string xml;
            const string path = string(spartan::ResourceCache::GetDataDirectory()) + "/" + package_file_name;
            return spartan::FileSystem::ReadFile(path, xml) && parse_package(xml, package);
        }

        bool remote_package(const string& commit, Package& package)
        {
            string xml;
            const string url = string(raw_url) + commit + "/" + package_repository_path;
            if (!spartan::FileSystem::DownloadToString(url, xml) || !parse_package(xml, package))
            {
                SP_LOG_ERROR("Failed to read the project package from GitHub: %s", url.c_str());
                return false;
            }
            return true;
        }

        Manifest load_manifest()
        {
            Manifest manifest;
            pugi::xml_document doc;
            if (!spartan::FileSystem::Exists(project_manifest) || !doc.load_file(project_manifest))
            {
                return manifest;
            }

            pugi::xml_node root   = doc.child("ProjectManifest");
            manifest.installed    = root.attribute("installed").as_string();
            manifest.skipped      = root.attribute("skipped").as_string();
            manifest.archive_size = root.attribute("archive_size").as_ullong();
            manifest.archive_time = root.attribute("archive_time").as_llong();
            return manifest;
        }

        void save_manifest(const Manifest& manifest)
        {
            pugi::xml_document doc;
            pugi::xml_node root = doc.append_child("ProjectManifest");
            root.append_attribute("installed").set_value(manifest.installed.c_str());
            root.append_attribute("skipped").set_value(manifest.skipped.c_str());
            root.append_attribute("archive_size").set_value(static_cast<unsigned long long>(manifest.archive_size));
            root.append_attribute("archive_time").set_value(static_cast<long long>(manifest.archive_time));

            spartan::FileSystem::CreateDirectory_(spartan::FileSystem::GetDirectoryFromFilePath(project_manifest));
            if (!doc.save_file(project_manifest))
            {
                SP_LOG_ERROR("Failed to save the project manifest: %s", project_manifest);
            }
        }

        void record_archive(Manifest& manifest)
        {
            error_code size_error;
            error_code time_error;
            const uint64_t size   = filesystem::file_size(project_archive, size_error);
            const auto time       = filesystem::last_write_time(project_archive, time_error);
            const bool failed     = size_error || time_error;
            manifest.archive_size = failed ? 0 : size;
            manifest.archive_time = failed ? 0 : static_cast<int64_t>(time.time_since_epoch().count());
        }

        // the archive kept in the project is what it was extracted from, one replaced by hand (publishing a package) wins over the record
        string installed_sha(Manifest& manifest)
        {
            if (!spartan::FileSystem::IsFile(project_archive))
            {
                return manifest.installed;
            }

            Manifest current = manifest;
            record_archive(current);
            if (current.archive_size == 0 || (current.archive_size == manifest.archive_size && current.archive_time == manifest.archive_time))
            {
                return manifest.installed;
            }

            const string sha = lowercase_copy(spartan::FileSystem::ComputeFileSha256(project_archive));
            if (sha.size() != 64)
            {
                return manifest.installed;
            }

            manifest.installed    = sha;
            manifest.archive_size = current.archive_size;
            manifest.archive_time = current.archive_time;
            save_manifest(manifest);
            return sha;
        }

        // a project of unknown origin (extracted by hand, archive deleted) is left alone, it may hold someone's work
        bool find_update(const string& commit, bool manual, Package& update)
        {
            Package remote;
            if (!remote_package(commit, remote))
            {
                return false;
            }

            Manifest manifest       = load_manifest();
            const string installed = installed_sha(manifest);
            if (installed.empty() || installed == remote.sha256 || (!manual && manifest.skipped == remote.sha256))
            {
                return true;
            }

            update = remote;
            return true;
        }

        void skip(const Package& package)
        {
            Manifest manifest = load_manifest();
            manifest.skipped  = package.sha256;
            save_manifest(manifest);
        }

        bool install(const Package& package, const spartan::ProgressTask& progress)
        {
            // named after its hash, so a partial download is only ever resumed into the same package
            const string archive_name = package.sha256.substr(0, 16) + ".7z";
            const string archive      = string(project_download_dir) + archive_name;
            if (spartan::FileSystem::IsDirectory(project_download_dir))
            {
                for (const string& file : spartan::FileSystem::GetFilesInDirectory(project_download_dir))
                {
                    if (spartan::FileSystem::GetFileNameFromFilePath(file) != archive_name)
                    {
                        spartan::FileSystem::Delete(file);
                    }
                }
            }

            progress.SetStep("Downloading project", "dropbox.com");
            string reason;
            if (!spartan::FileSystem::DownloadFile(package.url, archive, [&progress](float fraction) { progress.SetFraction(fraction); }, &reason))
            {
                set_install_error("The project download failed: " + reason);
                return false;
            }

            progress.SetStep("Verifying project");
            const string archive_sha = lowercase_copy(spartan::FileSystem::ComputeFileSha256(archive));
            if (archive_sha.size() != 64)
            {
                // kept, nothing shows the download to be wrong and the next attempt verifies it again
                set_install_error("The downloaded project package could not be read back to verify it: " + archive);
                return false;
            }

            if (archive_sha != package.sha256)
            {
                SP_LOG_ERROR("The downloaded project package has sha256 %s but %s was expected", archive_sha.c_str(), package.sha256.c_str());
                spartan::FileSystem::Delete(archive);
                set_install_error("The downloaded project package is not the published one (sha256 " + archive_sha.substr(0, 16) + ", expected " + package.sha256.substr(0, 16) + "), it was deleted so a retry downloads it again");
                return false;
            }

            // extracted next to the live project, so a failure leaves the project as it was
            progress.SetStep("Extracting project");
            spartan::FileSystem::Delete(project_staging_dir);
            if (!spartan::FileSystem::ExtractArchive(archive, project_staging_dir, &reason))
            {
                spartan::FileSystem::Delete(project_staging_dir);
                set_install_error("The project extraction failed: " + reason);
                return false;
            }

            // replaced rather than extracted over, so assets removed from the package do not linger
            progress.SetStep("Replacing project");
            spartan::FileSystem::Delete(project_old_dir);
            error_code error;
            if (filesystem::exists(project_install_dir))
            {
                filesystem::rename(project_install_dir, project_old_dir, error);
                if (error)
                {
                    set_install_error("The current project folder could not be moved aside, a file in it may be open in another program: " + describe_error(error));
                    spartan::FileSystem::Delete(project_staging_dir);
                    return false;
                }
            }

            filesystem::rename(project_staging_dir, project_install_dir, error);
            if (error)
            {
                set_install_error("The extracted project could not be moved into place: " + describe_error(error));
                error_code restore_error;
                filesystem::rename(project_old_dir, project_install_dir, restore_error);
                spartan::FileSystem::Delete(project_staging_dir);
                return false;
            }

            // kept with the project like a manual install, so the archive keeps identifying what is installed
            filesystem::rename(archive, project_archive, error);
            spartan::FileSystem::Delete(project_old_dir);

            Manifest manifest  = load_manifest();
            manifest.installed = package.sha256;
            manifest.skipped.clear();
            record_archive(manifest);
            save_manifest(manifest);

            SP_LOG_INFO("Installed project package %s", package.sha256.c_str());
            return true;
        }
    }

    namespace worlds
    {
        struct RemoteFile
        {
            string name;
            string sha; // git blob sha1, what github reports for every file in a listing
        };

        struct PendingFile
        {
            RemoteFile remote;
            bool exists         = false;
            bool locally_edited = false; // differs from what was last installed, so it is backed up before being replaced
        };

        // what was installed last, so a file that differs from github can be told apart as outdated or edited by the user
        struct Manifest
        {
            unordered_map<string, string> installed; // file name to git blob sha1
            string skipped;                          // signature of the update the user said no to
        };

        bool is_clone()
        {
            return spartan::FileSystem::Exists("../.git");
        }

        // updates only manage the flat folder they install into
        bool are_installed()
        {
            if (!spartan::FileSystem::IsDirectory(worlds_install_dir))
            {
                return false;
            }

            for (const string& file : spartan::FileSystem::GetFilesInDirectory(worlds_install_dir))
            {
                if (spartan::FileSystem::IsEngineWorldFile(file))
                {
                    return true;
                }
            }

            return false;
        }

        // every listing entry starts with its name, so the text up to the next name belongs to that entry
        vector<RemoteFile> parse_listing(const string& json)
        {
            vector<RemoteFile> files;
            const string key = "\"name\"";
            size_t position  = json.find(key);
            while (position != string::npos)
            {
                const size_t next  = json.find(key, position + key.size());
                const string entry = json.substr(position, next == string::npos ? string::npos : next - position);
                position           = next;

                RemoteFile file;
                file.name = json_string_value(entry, "name");
                file.sha  = json_string_value(entry, "sha");
                if (json_string_value(entry, "type") != "file" || file.name.empty() || file.sha.size() != 40)
                {
                    continue;
                }

                const string extension = lowercase_copy(spartan::FileSystem::GetExtensionFromFilePath(file.name));
                if (extension == spartan::EXTENSION_WORLD || extension == spartan::EXTENSION_LUA || extension == ".md")
                {
                    files.push_back(file);
                }
            }

            return files;
        }

        bool fetch_listing(const string& commit, vector<RemoteFile>& files)
        {
            string listing;
            const string listing_url = string(worlds_list_url) + commit;
            if (!spartan::FileSystem::DownloadToString(listing_url, listing))
            {
                SP_LOG_ERROR("Failed to list the worlds on GitHub: %s", listing_url.c_str());
                return false;
            }

            files = parse_listing(listing);
            const bool has_world_file = any_of(files.begin(), files.end(), [](const RemoteFile& file)
            {
                return lowercase_copy(spartan::FileSystem::GetExtensionFromFilePath(file.name)) == spartan::EXTENSION_WORLD;
            });
            if (!has_world_file)
            {
                SP_LOG_ERROR("The GitHub worlds listing has no world files");
                return false;
            }

            return true;
        }

        Manifest load_manifest()
        {
            Manifest manifest;
            pugi::xml_document doc;
            if (!spartan::FileSystem::Exists(worlds_manifest) || !doc.load_file(worlds_manifest))
            {
                return manifest;
            }

            pugi::xml_node root = doc.child("WorldsManifest");
            manifest.skipped    = root.attribute("skipped").as_string();
            for (pugi::xml_node file_node = root.child("File"); file_node; file_node = file_node.next_sibling("File"))
            {
                const string name = file_node.attribute("name").as_string();
                const string sha  = file_node.attribute("sha").as_string();
                if (!name.empty() && !sha.empty())
                {
                    manifest.installed[name] = sha;
                }
            }

            return manifest;
        }

        void save_manifest(const Manifest& manifest)
        {
            pugi::xml_document doc;
            pugi::xml_node root = doc.append_child("WorldsManifest");
            root.append_attribute("skipped").set_value(manifest.skipped.c_str());
            for (const auto& [name, sha] : manifest.installed)
            {
                pugi::xml_node file_node = root.append_child("File");
                file_node.append_attribute("name").set_value(name.c_str());
                file_node.append_attribute("sha").set_value(sha.c_str());
            }

            spartan::FileSystem::CreateDirectory_(spartan::FileSystem::GetDirectoryFromFilePath(worlds_manifest));
            if (!doc.save_file(worlds_manifest))
            {
                SP_LOG_ERROR("Failed to save the worlds manifest: %s", worlds_manifest);
            }
        }

        // files that already match github are recorded as installed, so their later edits are recognized
        vector<PendingFile> plan_sync(const vector<RemoteFile>& remote_files, Manifest& manifest)
        {
            vector<PendingFile> pending;
            for (const RemoteFile& remote : remote_files)
            {
                const string local_path = string(worlds_install_dir) + remote.name;
                PendingFile file;
                file.remote = remote;
                file.exists = spartan::FileSystem::IsFile(local_path);
                if (file.exists)
                {
                    const string local_sha = git_blob_sha1(local_path);
                    if (local_sha == remote.sha)
                    {
                        manifest.installed[remote.name] = remote.sha;
                        continue;
                    }

                    // without a record of what was installed the change cannot be proven safe, so it is kept as a backup
                    const auto installed = manifest.installed.find(remote.name);
                    file.locally_edited  = installed == manifest.installed.end() || installed->second != local_sha;
                }
                pending.push_back(file);
            }

            return pending;
        }

        string update_signature(const vector<PendingFile>& files)
        {
            vector<string> parts;
            for (const PendingFile& file : files)
            {
                parts.push_back(file.remote.name + ":" + file.remote.sha);
            }
            sort(parts.begin(), parts.end());

            string signature;
            for (const string& part : parts)
            {
                signature += part + ";";
            }
            return sha1_hex(signature);
        }

        bool find_update(const string& commit, bool manual, vector<PendingFile>& update)
        {
            vector<RemoteFile> remote_files;
            if (!fetch_listing(commit, remote_files))
            {
                return false;
            }

            Manifest manifest           = load_manifest();
            vector<PendingFile> pending = plan_sync(remote_files, manifest);
            save_manifest(manifest);

            if (!pending.empty() && (manual || manifest.skipped != update_signature(pending)))
            {
                update = pending;
            }
            return true;
        }

        void skip(const vector<PendingFile>& files)
        {
            Manifest manifest = load_manifest();
            manifest.skipped  = update_signature(files);
            save_manifest(manifest);
        }

        bool install(const string& commit, const vector<PendingFile>& files, const spartan::ProgressTask& progress)
        {
            progress.SetStep("Downloading worlds", "github.com/PanosK92/SpartanEngine");

            // a clean staging folder, so a failed earlier attempt cannot be resumed into a mismatched file
            spartan::FileSystem::Delete(worlds_staging_dir);
            spartan::FileSystem::CreateDirectory_(worlds_staging_dir);

            for (size_t i = 0; i < files.size(); i++)
            {
                const RemoteFile& remote = files[i].remote;
                const string url         = string(raw_url) + commit + "/worlds/" + remote.name;
                const string staged_path = string(worlds_staging_dir) + remote.name;
                const float file_start   = static_cast<float>(i) / static_cast<float>(files.size());
                const float file_span    = 1.0f / static_cast<float>(files.size());
                string reason;
                const bool downloaded    = spartan::FileSystem::DownloadFile(url, staged_path, [&progress, file_start, file_span](float fraction)
                {
                    progress.SetFraction(file_start + fraction * file_span);
                }, &reason);

                if (!downloaded || git_blob_sha1(staged_path) != remote.sha)
                {
                    set_install_error(downloaded ? "The downloaded world file " + remote.name + " does not match GitHub's listing" : "The download of the world file " + remote.name + " failed: " + reason);
                    spartan::FileSystem::Delete(worlds_staging_dir);
                    return false;
                }
            }

            // everything is staged before anything is replaced, so a dropped connection never leaves a half updated folder
            Manifest manifest = load_manifest();
            string backup_dir;
            size_t backed_up = 0;
            bool success     = true;
            spartan::FileSystem::CreateDirectory_(worlds_install_dir);
            for (const PendingFile& file : files)
            {
                const string staged_path = string(worlds_staging_dir) + file.remote.name;
                const string destination = string(worlds_install_dir) + file.remote.name;

                if (file.exists && file.locally_edited)
                {
                    if (backup_dir.empty())
                    {
                        backup_dir = string(worlds_backup_dir) + to_string(get_current_timestamp()) + "/";
                        spartan::FileSystem::CreateDirectory_(backup_dir);
                    }

                    if (!spartan::FileSystem::CopyFileFromTo(destination, backup_dir + file.remote.name))
                    {
                        set_install_error("The world file " + destination + " could not be backed up into " + backup_dir + ", it was left untouched");
                        success = false;
                        continue;
                    }
                    backed_up++;
                }

                if (!spartan::FileSystem::CopyFileFromTo(staged_path, destination) || git_blob_sha1(destination) != file.remote.sha)
                {
                    set_install_error("The world file " + destination + " could not be written, it may be open in another program or the folder read-only");
                    success = false;
                    continue;
                }
                manifest.installed[file.remote.name] = file.remote.sha;
            }
            spartan::FileSystem::Delete(worlds_staging_dir);

            manifest.skipped.clear();
            save_manifest(manifest);

            if (backed_up > 0)
            {
                SP_LOG_INFO("Backed up %zu locally edited world files into %s", backed_up, backup_dir.c_str());
            }
            if (success)
            {
                SP_LOG_INFO("Installed %zu world files into %s", files.size(), worlds_install_dir);
            }
            return success;
        }

        // a first download only adds what is missing, files already on disk are left for the update check to judge
        bool install_missing(const string& commit, const spartan::ProgressTask& progress)
        {
            progress.SetStep("Downloading worlds", "github.com/PanosK92/SpartanEngine");

            vector<RemoteFile> remote_files;
            if (!fetch_listing(commit, remote_files))
            {
                set_install_error("The list of worlds could not be read from GitHub (api.github.com), it may be unreachable, blocked or rate limited");
                return false;
            }

            Manifest manifest           = load_manifest();
            vector<PendingFile> pending = plan_sync(remote_files, manifest);
            save_manifest(manifest);
            pending.erase(remove_if(pending.begin(), pending.end(), [](const PendingFile& file)
            {
                return file.exists;
            }), pending.end());

            return pending.empty() || install(commit, pending, progress);
        }
    }

    // project and worlds go together, the worlds reference the project's assets
    struct ContentUpdate
    {
        string commit;
        project::Package project;
        vector<worlds::PendingFile> worlds;

        bool empty() const
        {
            return project.empty() && worlds.empty();
        }
    };

    // the check worker fills this and the main thread picks it up in tick
    struct CheckResult
    {
        bool failed = false;
        bool found_engine = false;
        engine::Release engine_release;
        ContentUpdate content;
    };

    mutex check_mutex;
    CheckResult check_result;
    atomic<bool> check_running{false};
    atomic<bool> check_ready{false};
    bool check_manual                 = false;
    Updates::CheckStatus check_status = Updates::CheckStatus::Idle;
    float check_status_until          = -1.0f;

    ContentUpdate content_update;
    bool content_prompt_pending = false; // shown the next time the launcher is open
    bool content_update_failed  = false;
    bool content_prompt_visible = false;

    bool installing     = false;
    bool install_update = false; // an update reports its own failures, a first run reports them to the launcher
    atomic<int> install_outcome{static_cast<int>(Updates::InstallResult::None)};
    Updates::InstallResult launcher_result = Updates::InstallResult::None;
    string reported_install_error; // the main thread's copy of install_error, taken when the install ends

    void start_check(bool manual)
    {
        if (check_running.exchange(true))
        {
            return;
        }

        check_manual = manual;
        if (manual)
        {
            engine::dismissed_this_session = false;
        }

        const bool check_engine  = spartan::Engine::IsFlagSet(spartan::EngineMode::EditorVisible);
        const bool check_project = Updates::IsProjectInstalled();
        const bool check_worlds  = !worlds::is_clone() && worlds::are_installed();
        spartan::ThreadPool::AddTask([manual, check_engine, check_project, check_worlds]()
        {
            CheckResult result;
            if (check_engine)
            {
                result.found_engine = engine::find_newer_release(manual, result.engine_release);
            }

            if (check_project || check_worlds)
            {
                if (!fetch_latest_commit(result.content.commit))
                {
                    result.failed = true;
                }
                else
                {
                    if (check_project && !project::find_update(result.content.commit, manual, result.content.project))
                    {
                        result.failed = true;
                    }
                    if (check_worlds && !worlds::find_update(result.content.commit, manual, result.content.worlds))
                    {
                        result.failed = true;
                    }
                }
            }

            {
                lock_guard<mutex> lock(check_mutex);
                check_result = move(result);
            }
            check_ready   = true;
            check_running = false;
        });
    }

    void start_install(bool is_update, const ContentUpdate& content, bool install_project, bool install_worlds)
    {
        if (installing)
        {
            return;
        }

        installing     = true;
        install_update = is_update;
        reported_install_error.clear();
        {
            lock_guard<mutex> lock(install_error_mutex);
            install_error.clear();
        }
        spartan::ThreadPool::AddTask([content, install_project, install_worlds]()
        {
            auto progress = spartan::ProgressTracker::Begin(spartan::ProgressType::Download, "Project library", install_project ? "Downloading project" : "Downloading worlds");

            // a first run has no commit yet, an update installs exactly what its check found
            string commit         = content.commit;
            const bool has_commit = !commit.empty() || fetch_latest_commit(commit);

            if (install_project)
            {
                // the copy that shipped with this build keeps a first run working while github is unreachable or rate limited
                project::Package package = content.project;
                if (package.empty() && !(has_commit && project::remote_package(commit, package)) && !project::local_package(package))
                {
                    set_install_error(string("Where to download the project from is unknown, GitHub could not be read and the copy in data/") + package_file_name + " is missing or damaged");
                }

                if (package.empty() || !project::install(package, progress))
                {
                    install_outcome = static_cast<int>(Updates::InstallResult::ProjectFailed);
                    return;
                }
            }

            if (install_worlds)
            {
                if (!has_commit)
                {
                    set_install_error("The latest commit could not be read from GitHub (api.github.com), it may be unreachable, blocked or rate limited, so the worlds cannot be listed");
                }
                const bool success = has_commit && (content.worlds.empty() ? worlds::install_missing(commit, progress) : worlds::install(commit, content.worlds, progress));
                if (!success)
                {
                    install_outcome = static_cast<int>(Updates::InstallResult::WorldsFailed);
                    return;
                }
            }

            install_outcome = static_cast<int>(Updates::InstallResult::Installed);
        });
    }

    void start_content_update()
    {
        content_prompt_pending = false;
        content_update_failed  = false;
        start_install(true, content_update, !content_update.project.empty(), !content_update.worlds.empty());
    }

    void skip_content_update()
    {
        content_prompt_pending = false;
        content_update_failed  = false;
        if (!content_update.project.empty())
        {
            project::skip(content_update.project);
        }
        if (!content_update.worlds.empty())
        {
            worlds::skip(content_update.worlds);
        }
    }

    void prompt_text_centered(const string& text)
    {
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(text.c_str()).x) * 0.5f);
        ImGui::TextDisabled("%s", text.c_str());
    }

    void content_prompt_window()
    {
        size_t added  = 0;
        size_t edited = 0;
        for (const worlds::PendingFile& file : content_update.worlds)
        {
            added  += file.exists ? 0 : 1;
            edited += file.locally_edited ? 1 : 0;
        }
        const size_t changed     = content_update.worlds.size() - added;
        const string worlds_text = to_string(added) + " new and " + to_string(changed) + " changed world files";

        vector<string> lines;
        string reason; // shown in full under the first line, a failed update always says what failed
        if (content_update_failed)
        {
            lines.push_back("The update failed.");
            lines.push_back("Nothing was replaced unless it downloaded fully. The full log is logs/log.txt.");
            reason = reported_install_error.empty() ? "No reason was recorded, see logs/log.txt." : reported_install_error;
        }
        else
        {
            const bool has_project = !content_update.project.empty();
            const bool has_worlds  = !content_update.worlds.empty();
            lines.push_back(has_project && has_worlds ? "GitHub has a newer project package and " + worlds_text + "." : has_project ? "GitHub has a newer project package." : "GitHub has " + worlds_text + ".");
            if (has_project)
            {
                lines.push_back("The project folder is deleted and installed fresh, files you added to it are lost.");
            }
            if (has_worlds)
            {
                lines.push_back(edited > 0 ? to_string(edited) + " world files have local edits, those get backed up to ../worlds_backup." : "World files you created yourself are never touched.");
            }
        }

        const float dpi           = spartan::Window::GetDpiScale();
        const float reason_margin = 28.0f * dpi;
        const float reason_gap    = 4.0f * dpi;
        const float reason_width  = 600.0f * dpi - reason_margin * 2.0f;
        const float reason_height = reason.empty() ? 0.0f : ImGui::CalcTextSize(reason.c_str(), nullptr, false, reason_width).y + ImGui::GetStyle().ItemSpacing.y + reason_gap * 2.0f;
        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(600.0f * dpi, (114.0f + 18.0f * static_cast<float>(lines.size())) * dpi + reason_height), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * dpi);
        bool open = true;
        if (ImGui::Begin("Updates available###content_update", &open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize))
        {
            const ImVec4 bg_1     = ImGui::Style::bg_color_1;
            const ImVec4 bg_2     = ImGui::Style::bg_color_2;
            const ImVec2 position = ImGui::GetWindowPos();
            const ImVec2 size     = ImGui::GetWindowSize();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), ImGui::ColorConvertFloat4ToU32(ImGui::Style::with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.18f), 0.96f)), 8.0f * dpi);
            draw_list->AddRect(position, ImVec2(position.x + size.x, position.y + size.y), ImGui::ColorConvertFloat4ToU32(ImGui::Style::with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.70f), 0.78f)), 8.0f * dpi);

            ImGui::SetCursorPosY(38.0f * dpi);
            for (size_t i = 0; i < lines.size(); i++)
            {
                prompt_text_centered(lines[i]);
                if (i == 0 && !reason.empty())
                {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + reason_gap);
                    ImGui::SetCursorPosX(reason_margin);
                    ImGui::PushTextWrapPos(reason_margin + reason_width);
                    ImGui::TextUnformatted(reason.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + reason_gap);
                }
            }
            ImGui::Dummy(ImVec2(0.0f, 18.0f * dpi));

            const float button_w = 148.0f * dpi;
            const float spacing  = 10.0f * dpi;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button_w * 2.0f - spacing) * 0.5f);
            if (ImGuiSp::command_button(content_update_failed ? "Retry" : "Update", ImVec2(button_w, 0.0f), true))
            {
                start_content_update();
            }
            ImGui::SameLine(0.0f, spacing);
            if (ImGuiSp::command_button("Skip", ImVec2(button_w, 0.0f), false))
            {
                skip_content_update();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);

        // closing only dismisses it for this session, skip is what remembers the choice
        if (!open)
        {
            content_prompt_pending = false;
        }
    }

    void apply_check_result()
    {
        if (!check_ready.exchange(false))
        {
            return;
        }

        CheckResult result;
        {
            lock_guard<mutex> lock(check_mutex);
            result = move(check_result);
        }

        if (result.found_engine)
        {
            engine::release        = result.engine_release;
            engine::prompt_pending = true;
        }

        if (!result.content.empty() && !installing)
        {
            content_update         = move(result.content);
            content_update_failed  = false;
            content_prompt_pending = true;
        }

        if (check_manual)
        {
            const bool found   = result.found_engine || content_prompt_pending;
            check_status       = result.failed ? Updates::CheckStatus::Failed : found ? Updates::CheckStatus::Idle : Updates::CheckStatus::UpToDate;
            check_status_until = static_cast<float>(ImGui::GetTime()) + status_duration;
        }
    }

    void apply_install_outcome()
    {
        const auto outcome = static_cast<Updates::InstallResult>(install_outcome.exchange(static_cast<int>(Updates::InstallResult::None)));
        if (outcome == Updates::InstallResult::None)
        {
            return;
        }

        installing = false;
        {
            lock_guard<mutex> lock(install_error_mutex);
            reported_install_error = install_error;
        }
        if (!install_update)
        {
            launcher_result = outcome;
            return;
        }

        if (outcome == Updates::InstallResult::Installed)
        {
            content_update  = {};
            launcher_result = outcome;
            return;
        }

        // the project installed before the worlds failed, a retry should not fetch it again
        if (outcome == Updates::InstallResult::WorldsFailed)
        {
            content_update.project = {};
        }
        content_update_failed  = true;
        content_prompt_pending = true;
    }
}

void Updates::Initialize(Editor* editor_in)
{
    editor = editor_in;
    start_check(false);
}

void Updates::Tick(bool allow_engine_prompt, bool launcher_visible)
{
    apply_check_result();
    apply_install_outcome();

    const bool engine_prompt_visible = allow_engine_prompt && engine::window();

    // waits for the engine prompt, both centered at once would overlap
    content_prompt_visible = content_prompt_pending && launcher_visible && !installing && !engine_prompt_visible;
    if (content_prompt_visible)
    {
        content_prompt_window();
    }
}

void Updates::CheckNow()
{
    start_check(true);
}

Updates::CheckStatus Updates::GetCheckStatus()
{
    if (check_running)
    {
        return CheckStatus::Checking;
    }

    return ImGui::GetTime() < check_status_until ? check_status : CheckStatus::Idle;
}

void Updates::InstallContent(bool install_project, bool install_worlds)
{
    start_install(false, {}, install_project, install_worlds);
}

Updates::InstallResult Updates::TakeInstallResult()
{
    const InstallResult result = launcher_result;
    launcher_result            = InstallResult::None;
    return result;
}

const string& Updates::GetInstallError()
{
    return reported_install_error;
}

bool Updates::IsBusy()
{
    return installing;
}

bool Updates::IsPromptVisible()
{
    return content_prompt_visible;
}

bool Updates::IsProjectInstalled()
{
    const string project_dir = spartan::ResourceCache::GetProjectDirectory();
    size_t entry_count       = spartan::FileSystem::GetFilesInDirectory(project_dir).size();
    entry_count             += spartan::FileSystem::GetDirectoriesInDirectory(project_dir).size();
    return entry_count > 1;
}

bool Updates::IsWorldsDirectoryAClone()
{
    return worlds::is_clone();
}
