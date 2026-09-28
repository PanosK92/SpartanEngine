/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//======================================
#include "pch.h"
#include "../EditorHistory.h"
#include "AssetViewer.h"
#include "AssetViewerCommon.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Style.h"
#include "../imgui/source/imgui_stdlib.h"
#include "file_system/FileSystem.h"
#include "geometry/GeometryProcessing.h"
#include "geometry/Mesh.h"
#include "io/pugixml.hpp"
#include "rhi/RHI_Texture.h"
#include "rendering/Material.h"
#include "rendering/Renderer.h"
#include "resource/ResourceCache.h"
#include "world/Entity.h"
#include "world/GameReady.h"
#include "world/Prefab.h"
#include "world/World.h"
#include "core/Event.h"
#include "world/components/Camera.h"
#include "world/components/Light.h"
#include "world/components/Render.h"
#include "mcp/McpJson.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <sstream>
//======================================

using namespace std;
using namespace spartan;

namespace asset_viewer_common
{
    JsonValue* json_object_find(JsonValue& object, const string& key)
    {
        if (object.type != mcp_json::kind::object)
        {
            return nullptr;
        }

        for (pair<string, JsonValue>& item : object.object_items)
        {
            if (item.first == key)
            {
                return &item.second;
            }
        }

        return nullptr;
    }

    JsonValue& json_object_ensure(JsonValue& object, const string& key)
    {
        if (JsonValue* existing = json_object_find(object, key))
        {
            return *existing;
        }

        object.object_items.emplace_back(key, JsonValue{});
        return object.object_items.back().second;
    }

    vector<string> json_strings(const JsonValue* value)
    {
        vector<string> strings;
        if (!value || value->type != mcp_json::kind::array)
        {
            return strings;
        }

        for (const JsonValue& item : value->array_items)
        {
            if (item.type == mcp_json::kind::string)
            {
                strings.emplace_back(item.string_value);
            }
        }
        return strings;
    }

    string lower_copy(string value)
    {
        transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(tolower(character));
            }
        );
        return value;
    }

    string asset_type_from_path(const string& path)
    {
        const string extension = lower_copy(
            FileSystem::GetExtensionFromFilePath(path)
        );
        if (extension == ".mesh")
        {
            return "mesh";
        }
        if (extension == ".xml")
        {
            return "material";
        }
        if (extension == ".prefab")
        {
            return "prefab";
        }
        if (
            extension == ".png" ||
            extension == ".jpg" ||
            extension == ".jpeg" ||
            extension == ".tga" ||
            extension == ".dds"
        )
        {
            return "texture";
        }
        return "resource";
    }

    string normalized_path(string value)
    {
        replace(
            value.begin(),
            value.end(),
            '\\',
            '/'
        );
        while (value.rfind("./", 0) == 0)
        {
            value.erase(0, 2);
        }
        return lower_copy(move(value));
    }

    string serialize_json_string(const string& value)
    {
        string result = "\"";
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b";  break;
            case '\f': result += "\\f";  break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:
                if (character < 0x20)
                {
                    char escaped[7] = {};
                    snprintf(
                        escaped,
                        sizeof(escaped),
                        "\\u%04x",
                        character
                    );
                    result += escaped;
                }
                else
                {
                    result += static_cast<char>(character);
                }
                break;
            }
        }
        result += '"';
        return result;
    }

    bool path_is_within(
        const string& value,
        const string& directory
    )
    {
        const auto engine_relative =
            [](const string& path_value)
        {
            const string normalized =
                normalized_path(path_value);
            const size_t project = normalized.find(
                "project/"
            );
            return
                project == string::npos ?
                normalized :
                normalized.substr(project);
        };
        const string relative_value =
            engine_relative(value);
        string relative_directory =
            engine_relative(directory);
        while (
            !relative_directory.empty() &&
            relative_directory.back() == '/'
        )
        {
            relative_directory.pop_back();
        }
        // disk scans store absolute paths, catalogs store project/mcp/...
        // both have to match the same root
        if (
            !relative_directory.empty() &&
            (
                relative_value == relative_directory ||
                relative_value.rfind(
                    relative_directory + "/",
                    0
                ) == 0
            )
        )
        {
            return true;
        }

        try
        {
            // strip trailing separators so a catalog root still
            // matches the asset files under it
            filesystem::path normalized_value =
                filesystem::absolute(value).lexically_normal();
            filesystem::path normalized_directory =
                filesystem::absolute(directory).lexically_normal();
            while (
                normalized_directory.has_filename() &&
                normalized_directory.filename().empty()
            )
            {
                normalized_directory =
                    normalized_directory.parent_path();
            }
            if (
                !normalized_directory.empty() &&
                normalized_directory.filename() == "."
            )
            {
                normalized_directory =
                    normalized_directory.parent_path();
            }
            const auto mismatch = std::mismatch(
                normalized_directory.begin(),
                normalized_directory.end(),
                normalized_value.begin(),
                normalized_value.end()
            );
            return
                mismatch.first ==
                normalized_directory.end();
        }
        catch (...)
        {
            return false;
        }
    }

    bool resolved_path_is_within(
        const string& value,
        const string& directory
    )
    {
        error_code value_error;
        error_code directory_error;
        const filesystem::path resolved_value =
            filesystem::weakly_canonical(
                filesystem::path(value),
                value_error
            );
        const filesystem::path resolved_directory =
            filesystem::weakly_canonical(
                filesystem::path(directory),
                directory_error
            );
        if (
            value_error ||
            directory_error ||
            resolved_directory.empty()
        )
        {
            return false;
        }
        const auto mismatch = std::mismatch(
            resolved_directory.begin(),
            resolved_directory.end(),
            resolved_value.begin(),
            resolved_value.end()
        );
        return mismatch.first == resolved_directory.end();
    }

    bool path_is_in_viewer_roots(const string& value)
    {
        if (value.find("..") != string::npos)
        {
            return false;
        }

        const string roots[] =
        {
            World::GetLibraryResourceDirectory(),
            World::GetGeneratedResourceDirectory()
        };
        const bool is_absolute =
            filesystem::path(value).is_absolute();

        for (const string& root : roots)
        {
            if (root.empty() || !path_is_within(value, root))
            {
                continue;
            }
            // weakly_canonical of a relative root follows cwd, so a
            // project/mcp file opened from bin/ would be rejected
            if (
                !is_absolute ||
                resolved_path_is_within(value, root)
            )
            {
                return true;
            }
        }
        return false;
    }

    // strips whatever a user typed into an inline rename down to something safe to put on disk
    // and into the catalog, empty means the input was unusable
    string sanitize_asset_name(const string& value)
    {
        string result;
        result.reserve(value.size());
        for (const unsigned char character : value)
        {
            const bool illegal =
                character < 0x20 ||
                character == '/' ||
                character == '\\' ||
                character == ':' ||
                character == '*' ||
                character == '?' ||
                character == '"' ||
                character == '<' ||
                character == '>' ||
                character == '|';
            if (!illegal)
            {
                result += static_cast<char>(character);
            }
        }
        while (!result.empty() && isspace(static_cast<unsigned char>(result.front())))
        {
            result.erase(0, 1);
        }
        while (
            !result.empty() &&
            (
                isspace(static_cast<unsigned char>(result.back())) ||
                result.back() == '.'
            )
        )
        {
            result.pop_back();
        }
        return result;
    }

    string join_strings(
        const vector<string>& values,
        const char* separator
    )
    {
        string result;
        for (const string& value : values)
        {
            if (!result.empty())
            {
                result += separator;
            }
            result += value;
        }
        return result;
    }

    int count_prefab_entities(const pugi::xml_node& node)
    {
        int count = 0;
        for (
            pugi::xml_node child = node.first_child();
            child;
            child = child.next_sibling()
        )
        {
            if (string(child.name()) == "Entity")
            {
                count++;
            }
            count += count_prefab_entities(child);
        }
        return count;
    }

    vector<string> collect_xml_references(
        const string& path,
        const char* attribute_name
    )
    {
        vector<string> references;
        pugi::xml_document document;
        if (!document.load_file(path.c_str()))
        {
            return references;
        }

        vector<pugi::xml_node> pending =
        {
            document.document_element()
        };
        while (!pending.empty())
        {
            const pugi::xml_node node = pending.back();
            pending.pop_back();
            for (
                pugi::xml_node child = node.first_child();
                child;
                child = child.next_sibling()
            )
            {
                pending.push_back(child);
            }

            const string reference =
                node.attribute(attribute_name).as_string();
            if (
                !reference.empty() &&
                find(
                    references.begin(),
                    references.end(),
                    reference
                ) == references.end()
            )
            {
                references.push_back(reference);
            }
        }
        return references;
    }

    float ui_scale()
    {
        return Window::GetDpiScale();
    }

    string compact_count(const uint64_t value)
    {
        char buffer[32] = {};
        if (value >= 1000000)
        {
            snprintf(
                buffer,
                sizeof(buffer),
                "%.1fM",
                static_cast<double>(value) / 1000000.0
            );
        }
        else if (value >= 1000)
        {
            snprintf(
                buffer,
                sizeof(buffer),
                "%.1fK",
                static_cast<double>(value) / 1000.0
            );
        }
        else
        {
            snprintf(
                buffer,
                sizeof(buffer),
                "%llu",
                static_cast<unsigned long long>(value)
            );
        }
        return buffer;
    }

    void detail_row(const char* label, const string& value)
    {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(104.0f * ui_scale());
        ImGui::TextWrapped("%s", value.c_str());
    }
}
