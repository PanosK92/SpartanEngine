/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

//======================================
#include "pch.h"
#include "AssetViewer.h"
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

namespace
{
    using JsonValue = mcp_json::value;

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

    bool json_object_erase(JsonValue& object, const string& key)
    {
        if (object.type != mcp_json::kind::object)
        {
            return false;
        }

        const auto iterator = find_if(
            object.object_items.begin(),
            object.object_items.end(),
            [&](const pair<string, JsonValue>& item)
            {
                return item.first == key;
            }
        );
        if (iterator == object.object_items.end())
        {
            return false;
        }

        object.object_items.erase(iterator);
        return true;
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

    string asset_display_name(const string& value)
    {
        string result;
        result.reserve(value.size() + 8);
        for (size_t index = 0; index < value.size(); index++)
        {
            const unsigned char character = value[index];
            const bool separator =
                character == '_' ||
                character == '-' ||
                isspace(character);
            if (separator)
            {
                if (!result.empty() && result.back() != ' ')
                {
                    result += ' ';
                }
                continue;
            }

            if (
                isupper(character) &&
                index > 0 &&
                !result.empty() &&
                result.back() != ' ' &&
                (
                    islower(
                        static_cast<unsigned char>(
                            value[index - 1]
                        )
                    ) ||
                    isdigit(
                        static_cast<unsigned char>(
                            value[index - 1]
                        )
                    )
                )
            )
            {
                result += ' ';
            }
            result += static_cast<char>(tolower(character));
        }

        const size_t first_space = result.find(' ');
        const string first_word = result.substr(0, first_space);
        if (
            first_space != string::npos &&
            (
                first_word == "build" ||
                first_word == "create" ||
                first_word == "generate" ||
                first_word == "make"
            )
        )
        {
            result.erase(0, first_space + 1);
        }

        bool capitalize = true;
        for (char& character : result)
        {
            if (character == ' ')
            {
                capitalize = true;
                continue;
            }
            if (capitalize)
            {
                character = static_cast<char>(
                    toupper(
                        static_cast<unsigned char>(character)
                    )
                );
                capitalize = false;
            }
        }
        return result.empty() ? value : result;
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

    // a packed map is a gpu side artifact, occlusion in red, roughness in green, metalness in blue
    // and height in alpha, the material already binds it and nothing authors it by hand, so it is
    // noise in a browser meant for source assets
    bool is_packed_texture(const string& path)
    {
        const string stem = lower_copy(
            FileSystem::GetFileNameWithoutExtensionFromFilePath(path)
        );
        const auto ends_with =
            [&stem](const string& suffix)
        {
            return
                stem.size() > suffix.size() &&
                stem.compare(
                    stem.size() - suffix.size(),
                    suffix.size(),
                    suffix
                ) == 0;
        };

        // the generator writes name_packed.png, the runtime repacker writes
        // name_packed_slot0.tex_packed.texture
        return
            ends_with("_packed") ||
            ends_with(".tex_packed") ||
            stem.find("_packed_slot") != string::npos;
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

    string serialize_json(
        const JsonValue& value,
        const uint32_t depth = 0
    )
    {
        const string indentation(depth * 2, ' ');
        const string child_indentation((depth + 1) * 2, ' ');
        switch (value.type)
        {
        case mcp_json::kind::null:
            return "null";
        case mcp_json::kind::boolean:
            return value.boolean_value ? "true" : "false";
        case mcp_json::kind::number:
        {
            ostringstream stream;
            stream.precision(17);
            stream << value.number_value;
            return stream.str();
        }
        case mcp_json::kind::string:
            return serialize_json_string(value.string_value);
        case mcp_json::kind::array:
        {
            if (value.array_items.empty())
            {
                return "[]";
            }
            string result = "[\n";
            for (size_t index = 0; index < value.array_items.size(); index++)
            {
                result +=
                    child_indentation +
                    serialize_json(value.array_items[index], depth + 1);
                result +=
                    index + 1 < value.array_items.size() ?
                    ",\n" :
                    "\n";
            }
            return result + indentation + "]";
        }
        case mcp_json::kind::object:
        {
            if (value.object_items.empty())
            {
                return "{}";
            }
            string result = "{\n";
            size_t index = 0;
            for (const auto& [key, child] : value.object_items)
            {
                result +=
                    child_indentation +
                    serialize_json_string(key) +
                    ": " +
                    serialize_json(child, depth + 1);
                result +=
                    ++index < value.object_items.size() ?
                    ",\n" :
                    "\n";
            }
            return result + indentation + "}";
        }
        }
        return "null";
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

    // rewrites references to a renamed file inside the library's xml assets, prefabs point at
    // meshes and materials, materials point at textures, a rename without this orphans them
    uint32_t retarget_library_references(
        const string& library_root,
        const string& old_leaf,
        const string& new_leaf
    )
    {
        if (
            old_leaf.empty() ||
            new_leaf.empty() ||
            old_leaf == new_leaf
        )
        {
            return 0;
        }

        uint32_t changed_files = 0;
        error_code error;
        filesystem::recursive_directory_iterator iterator(
            filesystem::path(library_root),
            error
        );
        if (error)
        {
            return 0;
        }

        for (
            ;
            iterator != filesystem::recursive_directory_iterator();
            iterator.increment(error)
        )
        {
            if (error)
            {
                break;
            }

            const filesystem::directory_entry& item = *iterator;
            if (!item.is_regular_file(error) || error)
            {
                error.clear();
                continue;
            }

            const string path = item.path().generic_string();
            if (
                lower_copy(
                    FileSystem::GetExtensionFromFilePath(path)
                ) != ".xml"
            )
            {
                continue;
            }

            string contents;
            if (!FileSystem::ReadFile(path, contents))
            {
                continue;
            }

            string result;
            result.reserve(contents.size() + 64);
            size_t cursor = 0;
            bool touched = false;
            while (true)
            {
                const size_t hit = contents.find(old_leaf, cursor);
                if (hit == string::npos)
                {
                    break;
                }

                // a file name has to begin right after a separator or a quote, without this
                // renaming bottle.mesh would also rewrite glass_bottle.mesh
                const char before =
                    hit == 0 ? '"' : contents[hit - 1];
                const bool boundary =
                    before == '/' ||
                    before == '\\' ||
                    before == '"' ||
                    before == '\'';
                result.append(contents, cursor, hit - cursor);
                result.append(boundary ? new_leaf : old_leaf);
                cursor = hit + old_leaf.size();
                touched = touched || boundary;
            }

            if (!touched)
            {
                continue;
            }
            result.append(contents, cursor, string::npos);
            if (FileSystem::WriteFile(path, result))
            {
                changed_files++;
            }
        }

        return changed_files;
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

    const JsonValue* json_object_value(
        const JsonValue& root,
        const char* key
    )
    {
        const JsonValue* value = root.find(key);
        if (value)
        {
            return value;
        }

        const JsonValue* revision = root.find("revision");
        return
            revision &&
            revision->type == mcp_json::kind::object ?
            revision->find(key) :
            nullptr;
    }

    string json_string_any(
        const JsonValue& root,
        const initializer_list<const char*>& keys
    )
    {
        for (const char* key : keys)
        {
            const JsonValue* value = json_object_value(root, key);
            if (value && value->type == mcp_json::kind::string)
            {
                return value->string_value;
            }
        }
        return "";
    }

    uint64_t json_uint_any(
        const JsonValue& root,
        const initializer_list<const char*>& keys
    )
    {
        for (const char* key : keys)
        {
            const JsonValue* value = json_object_value(root, key);
            if (
                value &&
                value->type == mcp_json::kind::number &&
                value->number_value > 0.0
            )
            {
                return static_cast<uint64_t>(value->number_value);
            }
        }
        return 0;
    }

    pair<uint64_t, uint64_t> prefab_counts(const string& path)
    {
        pugi::xml_document document;
        if (!document.load_file(path.c_str()))
        {
            return {};
        }

        const pugi::xml_node prefab = document.child("Prefab");
        if (!prefab)
        {
            return {};
        }

        unordered_set<string> dependencies;
        vector<pugi::xml_node> pending = { prefab };
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

            for (
                const char* attribute_name :
                {
                    "mesh_path",
                    "material_path",
                    "texture_path"
                }
            )
            {
                const string reference =
                    node.attribute(attribute_name).as_string();
                if (!reference.empty())
                {
                    dependencies.insert(normalized_path(reference));
                }
            }
        }

        return
        {
            static_cast<uint64_t>(
                1 + count_prefab_entities(prefab)
            ),
            static_cast<uint64_t>(dependencies.size())
        };
    }

    string absolute_manifest_path(
        const string& value,
        const string& manifest_path
    )
    {
        filesystem::path path(value);
        if (path.is_relative())
        {
            path =
                filesystem::path(manifest_path).parent_path() /
                path;
        }
        return path.lexically_normal().generic_string();
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

    ImU32 color_with_alpha(const Color& color, float alpha)
    {
        return ImGui::ColorConvertFloat4ToU32(
            ImVec4(
                color.r,
                color.g,
                color.b,
                alpha
            )
        );
    }

    // imgui 1.92 asserts if setcursorscreenpos is the last layout call in a window
    void finish_overlay_cursor(const ImVec2& cursor)
    {
        if (ImGuiWindow* window = ImGui::GetCurrentWindow())
        {
            window->DC.CurrLineSize = ImVec2(0.0f, 0.0f);
            window->DC.CurrLineTextBaseOffset = 0.0f;
        }
        ImGui::SetCursorScreenPos(cursor);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        ImGui::SetCursorScreenPos(cursor);
    }

    float ui_scale()
    {
        return Window::GetDpiScale();
    }

    ImU32 asset_type_color(const string& type, const int alpha = 255)
    {
        if (type == "mesh")
        {
            return IM_COL32(74, 170, 255, alpha);
        }
        if (type == "material")
        {
            return IM_COL32(190, 112, 255, alpha);
        }
        if (type == "texture")
        {
            return IM_COL32(255, 176, 74, alpha);
        }
        return IM_COL32(75, 205, 150, alpha);
    }

    IconType asset_type_icon(const string& type)
    {
        if (type == "mesh")
        {
            return IconType::Model;
        }
        if (type == "material")
        {
            return IconType::Material;
        }
        if (type == "texture")
        {
            return IconType::Texture;
        }
        return IconType::Entity;
    }

    bool toolbar_toggle(
        const char* label,
        const bool active,
        const ImVec2& size = ImVec2(0.0f, 0.0f)
    )
    {
        if (active)
        {
            const ImVec4 accent = ImGui::Style::color_accent_1;
            ImGui::PushStyleColor(
                ImGuiCol_Button,
                ImVec4(accent.x, accent.y, accent.z, 0.28f)
            );
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                ImVec4(accent.x, accent.y, accent.z, 0.42f)
            );
        }
        const bool clicked = ImGui::Button(label, size);
        if (active)
        {
            ImGui::PopStyleColor(2);
        }
        return clicked;
    }

    void horizontal_splitter(
        const char* id,
        float& width,
        const float minimum,
        const float maximum,
        const float height,
        const float direction
    )
    {
        const float thickness = 5.0f * ui_scale();
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(thickness, height));
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        if (active || hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (active)
        {
            width = clamp(
                width + ImGui::GetIO().MouseDelta.x * direction,
                minimum,
                maximum
            );
        }
        const ImU32 color = ImGui::GetColorU32(
            active
                ? ImGuiCol_SeparatorActive
                : hovered
                    ? ImGuiCol_SeparatorHovered
                    : ImGuiCol_Separator
        );
        ImGui::GetWindowDrawList()->AddRectFilled(
            position,
            ImVec2(position.x + thickness, position.y + height),
            color
        );
    }

    void vertical_splitter(
        const char* id,
        float& height,
        const float minimum,
        const float maximum,
        const float width
    )
    {
        const float thickness = 5.0f * ui_scale();
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(width, thickness));
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        if (active || hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }
        if (active)
        {
            height = clamp(
                height + ImGui::GetIO().MouseDelta.y,
                minimum,
                maximum
            );
        }
        const ImU32 color = ImGui::GetColorU32(
            active
                ? ImGuiCol_SeparatorActive
                : hovered
                    ? ImGuiCol_SeparatorHovered
                    : ImGuiCol_Separator
        );
        ImGui::GetWindowDrawList()->AddRectFilled(
            position,
            ImVec2(position.x + width, position.y + thickness),
            color
        );
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

    void draw_asset_identity(
        const string& name,
        const string& type,
        const char* context
    )
    {
        const float scale = ui_scale();
        const ImVec4 background =
            ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(
                background.x,
                background.y,
                background.z,
                0.42f
            )
        );
        ImGui::BeginChild(
            "##asset_identity",
            ImVec2(0.0f, 72.0f * scale),
            ImGuiChildFlags_None
        );
        ImGui::SetCursorPos(
            ImVec2(12.0f * scale, 16.0f * scale)
        );
        ImGuiSp::image(
            asset_type_icon(type),
            30.0f * scale,
            ImVec4(0.82f, 0.9f, 1.0f, 1.0f)
        );
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(name.c_str());
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(
                asset_type_color(type)
            ),
            "%s  %s",
            type.c_str(),
            context
        );
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // a labelled number in a box, the inspector shows the handful that decide whether an asset is
    // game ready as a row of these rather than as a column of text
    void draw_metric_card(
        const char* label,
        const string& value,
        const float width,
        const ImU32 accent = 0
    )
    {
        const float scale = ui_scale();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(width, 46.0f * scale);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec4 frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        draw_list->AddRectFilled(
            origin,
            ImVec2(origin.x + size.x, origin.y + size.y),
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(frame.x, frame.y, frame.z, 0.45f)
            ),
            5.0f * scale
        );
        if (accent != 0)
        {
            draw_list->AddRectFilled(
                origin,
                ImVec2(origin.x + 3.0f * scale, origin.y + size.y),
                accent,
                2.0f * scale
            );
        }
        draw_list->AddText(
            ImVec2(origin.x + 9.0f * scale, origin.y + 6.0f * scale),
            ImGui::GetColorU32(ImGuiCol_Text),
            value.c_str()
        );
        draw_list->AddText(
            ImVec2(origin.x + 9.0f * scale, origin.y + 25.0f * scale),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            label
        );
        ImGui::Dummy(size);
    }

    void section_title(const char* title)
    {
        ImGui::Spacing();
        ImGui::TextUnformatted(title);
        ImGui::Separator();
    }

    // twelve hex characters, the same shape the generator gives its files so a baked mesh sits
    // next to its siblings without looking foreign
    string short_hash(const string& seed)
    {
        const uint64_t now = static_cast<uint64_t>(
            chrono::steady_clock::now().time_since_epoch().count()
        );
        uint64_t value = std::hash<string>{}(seed) ^ (now * 1099511628211ull);
        value ^= value >> 29;
        value *= 0xbf58476d1ce4e5b9ull;
        value ^= value >> 32;
        char buffer[16] = {};
        snprintf(
            buffer,
            sizeof(buffer),
            "%012llx",
            static_cast<unsigned long long>(value & 0xffffffffffffull)
        );
        return buffer;
    }
}

AssetViewer::AssetViewer(Editor* editor) : Widget(editor)
{
    m_title         = "Asset Viewer";
    m_visible       = false;
    m_toolbar_order = 6;
    m_toolbar_icon  = static_cast<int>(spartan::IconType::Model);
    m_size_initial = math::Vector2(1280.0f, 760.0f);
    m_size_min     = math::Vector2(720.0f, 480.0f);
    m_next_refresh_check = chrono::steady_clock::now();
    m_world_unloading_handle = SP_SUBSCRIBE_TO_EVENT(
        EventType::WorldUnloading,
        SP_EVENT_HANDLER(ClearLoadedAsset)
    );
}

AssetViewer::~AssetViewer()
{
    if (m_world_unloading_handle != 0)
    {
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldUnloading, m_world_unloading_handle);
        m_world_unloading_handle = 0;
    }
    DestroyPreviewScene();
}

void AssetViewer::OnVisible()
{
    RefreshCatalog(true);
    ScanRevisionCandidates(true);
    // an authoring tool parks the asset it is working on in the world under this tag, adopting it is
    // what makes the panel show that work when it is opened by hand rather than driven
    Entity* focused_workspace = nullptr;
    for (Entity* entity : World::GetEntities())
    {
        if (
            entity &&
            entity->HasTag("authoring_workspace")
        )
        {
            entity->SetActive(false);
            entity->SetTransient(true);
            focused_workspace = entity;
        }
    }
    if (
        !PreviewRoot() &&
        m_selected_asset >= 0 &&
        m_selected_asset <
            static_cast<int>(m_assets.size())
    )
    {
        LoadSelectedAsset(false, false);
    }
    else if (!PreviewRoot() && focused_workspace)
    {
        PreviewEntity(focused_workspace);
    }
    else if (!PreviewRoot() && m_selected_asset < 0)
    {
        // an empty viewport on open is a dead end, the first prefab is what the user most likely
        // came to look at so it is up and expanded before they click anything
        for (int index = 0; index < static_cast<int>(m_assets.size()); index++)
        {
            if (m_assets[index].type != "prefab")
            {
                continue;
            }
            m_selected_asset = index;
            m_selected_assets.clear();
            m_selected_assets.insert(m_assets[index].id);
            m_selection_anchor = index;
            m_expanded_assets.insert(m_assets[index].id);
            m_inspector_tab = 0;
            LoadSelectedAsset();
            break;
        }
    }
}

void AssetViewer::OnInvisible()
{
    // the preview is kept and only the pending render is dropped. tearing it down here meant closing the
    // panel threw away the asset being reviewed, so the next capture failed with nothing loaded and the panel
    // had to be reopened and re-previewed before it could be looked at again, which is where the open and
    // close churn in a driven run comes from
    //
    // this is safe to leave standing because the rig, its camera and its lights are all transient and
    // parented under the preview root, and that root is inactive, so none of it lights or draws into the main
    // scene and none of it is written into the world
    Renderer::InvalidateSecondaryView();
    m_preview_dirty = false;
}

void AssetViewer::OnTickVisible()
{
    const string world_file_path = World::GetFilePath();
    if (world_file_path != m_world_file_path)
    {
        DestroyPreviewScene();
        RefreshCatalog(m_visible);
    }
    else if (chrono::steady_clock::now() >= m_next_refresh_check)
    {
        m_next_refresh_check =
            chrono::steady_clock::now() +
            chrono::seconds(1);
        RefreshCatalog(false);
        ScanRevisionCandidates(false);
        // unsaved mesh edits win over the auto reload, otherwise a background
        // write would silently throw the edits away
        if (
            !m_loaded_path.empty() &&
            !m_working_modified &&
            !m_working_lods_built &&
            FileSystem::Exists(m_loaded_path)
        )
        {
            const string write_time =
                FileSystem::GetLastWriteTime(m_loaded_path);
            if (write_time != m_loaded_write_time)
            {
                if (m_selected_asset >= 0)
                {
                    LoadSelectedAsset(false, true);
                }
                else
                {
                    // a linked file preview has no catalog entry, reloading through the
                    // catalog path would just clear it
                    const float yaw = m_preview_yaw;
                    const float pitch = m_preview_pitch;
                    const float zoom = m_preview_zoom;
                    const math::Vector2 pan = m_texture_pan;
                    const string path = m_loaded_path;
                    LoadDependencyPreview(path);
                    m_preview_yaw = yaw;
                    m_preview_pitch = pitch;
                    m_preview_zoom = zoom;
                    m_texture_pan = pan;
                }
            }
        }
    }

    // the world can remove the previewed entity at any time, drop the stale id
    // before anything walks the preview hierarchy this frame
    const bool preview_entity_alive = PreviewRoot() != nullptr;
    if (m_preview_root_id != 0 && !preview_entity_alive)
    {
        m_preview_root_id = 0;
        m_preview_camera_id = 0;
        m_preview_root_owned = false;
        m_preview_dirty = false;
        m_preview_orbiting = false;
    }

    if (
        m_preview_auto_rotate &&
        (m_mesh || m_material || preview_entity_alive) &&
        !m_texture &&
        !m_preview_orbiting
    )
    {
        m_preview_yaw -= ImGui::GetIO().DeltaTime * 0.42f;
        m_preview_dirty = true;
    }

    // an inline rename whose row is no longer drawn can never be committed or cancelled, drop it
    // rather than leaving the library stuck in a rename that is invisible
    if (!m_rename_asset_id.empty())
    {
        bool found = false;
        for (const AssetEntry& entry : m_assets)
        {
            if (entry.id == m_rename_asset_id)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            m_rename_asset_id.clear();
        }
    }
    if (
        !m_rename_dependency_path.empty() &&
        !FileSystem::Exists(m_rename_dependency_path)
    )
    {
        m_rename_dependency_path.clear();
    }

    const float available_width =
        ImGui::GetContentRegionAvail().x;
    const bool compact_layout =
        available_width < 1040.0f * ui_scale();

    const float status_height =
        ImGui::GetTextLineHeightWithSpacing() +
        8.0f * ui_scale();
    const float available_height =
        max(
            1.0f,
            ImGui::GetContentRegionAvail().y -
            status_height
        );
    const float splitter_width = 5.0f * ui_scale();

    if (compact_layout)
    {
        const float minimum_preview = min(
            240.0f * ui_scale(),
            available_height * 0.55f
        );
        const float minimum_lower = min(
            220.0f * ui_scale(),
            available_height -
                minimum_preview -
                splitter_width
        );
        const float maximum_preview = max(
            minimum_preview,
            available_height -
                minimum_lower -
                splitter_width
        );
        m_compact_preview_height = clamp(
            m_compact_preview_height,
            minimum_preview,
            maximum_preview
        );
        const float lower_height = max(
            1.0f,
            available_height -
                m_compact_preview_height -
                splitter_width
        );

        DrawPreview(
            available_width,
            m_compact_preview_height
        );
        vertical_splitter(
            "##asset_preview_splitter",
            m_compact_preview_height,
            minimum_preview,
            maximum_preview,
            available_width
        );

        const float minimum_library = min(
            230.0f * ui_scale(),
            available_width * 0.4f
        );
        const float minimum_inspector = min(
            320.0f * ui_scale(),
            available_width * 0.45f
        );
        const float maximum_library = max(
            minimum_library,
            available_width -
                minimum_inspector -
                splitter_width
        );
        m_library_width = clamp(
            m_library_width,
            minimum_library,
            maximum_library
        );

        DrawAssetList(
            m_library_width,
            lower_height
        );
        ImGui::SameLine(0.0f, 0.0f);
        horizontal_splitter(
            "##asset_compact_library_splitter",
            m_library_width,
            minimum_library,
            maximum_library,
            lower_height,
            1.0f
        );
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginGroup();
        DrawDetails(lower_height);
        ImGui::EndGroup();
    }
    else
    {
        const float minimum_library = 230.0f * ui_scale();
        const float minimum_preview = 360.0f * ui_scale();
        const float minimum_inspector = 320.0f * ui_scale();
        const float maximum_library = min(
            340.0f * ui_scale(),
            available_width -
                minimum_preview -
                minimum_inspector -
                splitter_width * 2.0f
        );
        m_library_width = clamp(
            m_library_width,
            minimum_library,
            max(minimum_library, maximum_library)
        );

        const float maximum_inspector = min(
            400.0f * ui_scale(),
            available_width -
                minimum_preview -
                m_library_width -
                splitter_width * 2.0f
        );
        m_inspector_width = clamp(
            m_inspector_width,
            minimum_inspector,
            max(minimum_inspector, maximum_inspector)
        );
        const float preview_width =
            available_width -
            m_library_width -
            m_inspector_width -
            splitter_width * 2.0f;

        DrawAssetList(m_library_width, available_height);
        ImGui::SameLine(0.0f, 0.0f);
        horizontal_splitter(
            "##asset_library_splitter",
            m_library_width,
            minimum_library,
            max(minimum_library, maximum_library),
            available_height,
            1.0f
        );
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginGroup();
        DrawPreview(preview_width, available_height);
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 0.0f);
        horizontal_splitter(
            "##asset_inspector_splitter",
            m_inspector_width,
            minimum_inspector,
            max(minimum_inspector, maximum_inspector),
            available_height,
            -1.0f
        );
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginGroup();
        DrawDetails(available_height);
        ImGui::EndGroup();
    }

    if (!m_rename_commit_id.empty())
    {
        const string id = m_rename_commit_id;
        const string name = m_rename_commit_name;
        m_rename_commit_id.clear();
        m_rename_commit_name.clear();
        for (int index = 0; index < static_cast<int>(m_assets.size()); index++)
        {
            if (m_assets[index].id == id)
            {
                RenameAsset(index, name);
                break;
            }
        }
    }

    if (!m_rename_commit_path.empty())
    {
        const string path = m_rename_commit_path;
        const string name = m_rename_commit_name;
        m_rename_commit_path.clear();
        m_rename_commit_name.clear();
        const string new_path =
            FileSystem::GetDirectoryFromFilePath(path) +
            sanitize_asset_name(name) +
            FileSystem::GetExtensionFromFilePath(path);
        const bool previewing =
            normalized_path(m_loaded_path) == normalized_path(path);
        if (RenameAssetFile(path, name))
        {
            // reloading the preview overwrites the status line, the rename result is what the
            // user needs to read
            const string status = m_status;
            RefreshCatalog(true);
            if (previewing && FileSystem::Exists(new_path))
            {
                LoadDependencyPreview(new_path);
            }
            m_status = status;
        }
    }

    // f2 renames the selection, same as the hierarchy, the rename input owns the keyboard while
    // it is open so the shortcut cannot fire on top of itself
    if (
        m_rename_asset_id.empty() &&
        m_rename_dependency_path.empty() &&
        m_selected_assets.size() <= 1 &&
        m_selected_asset >= 0 &&
        m_selected_asset < static_cast<int>(m_assets.size()) &&
        ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows
        ) &&
        ImGui::IsKeyPressed(ImGuiKey_F2)
    )
    {
        m_rename_dependency_path.clear();
        m_rename_asset_id = m_assets[m_selected_asset].id;
        m_rename_buffer = m_assets[m_selected_asset].name;
        m_rename_request_focus = true;
    }

    DrawDeleteConfirmation();
    DrawCleanupConfirmation();
    DrawStatusBar();
}

void AssetViewer::RefreshCatalog(bool force)
{
    m_next_refresh_check =
        chrono::steady_clock::now() +
        chrono::seconds(1);

    const string world_file_path = World::GetFilePath();
    const string catalog_path =
        World::GetLibraryResourceDirectory() +
        "catalog.json";
    // the directories are part of the signature, files can land on disk without the
    // catalog ever being rewritten. mcp writes into mcp/blockout, the viewer catalog
    // lives in mcp/library, so both roots have to be watched
    string write_time;
    auto append_root_times = [&](const string& root)
    {
        if (root.empty())
        {
            return;
        }
        const string catalog = root + "catalog.json";
        if (FileSystem::Exists(catalog))
        {
            write_time +=
                "|" +
                FileSystem::GetLastWriteTime(catalog);
        }
        for (
            const char* folder :
            { "meshes", "materials", "textures", "prefabs" }
        )
        {
            const string directory = root + folder;
            if (FileSystem::Exists(directory))
            {
                write_time +=
                    "|" +
                    FileSystem::GetLastWriteTime(directory);
            }
        }
    };
    append_root_times(World::GetLibraryResourceDirectory());
    append_root_times(World::GetGeneratedResourceDirectory());

    if (
        !force &&
        world_file_path == m_world_file_path &&
        catalog_path == m_catalog_path &&
        write_time == m_catalog_write_time
    )
    {
        return;
    }

    const bool library_changed =
        world_file_path != m_world_file_path ||
        catalog_path != m_catalog_path ||
        write_time != m_catalog_write_time;
    if (library_changed)
    {
        m_cleanup_generation++;
        m_cleanup.scanned = false;
    }

    const string previous_id =
        (
            m_selected_asset >= 0 &&
            m_selected_asset < static_cast<int>(m_assets.size())
        ) ?
        m_assets[m_selected_asset].id :
        "";
    const bool reload_preview =
        !m_loaded_path.empty();

    m_world_file_path = world_file_path;
    m_catalog_path = catalog_path;
    m_catalog_write_time = write_time;
    m_assets.clear();
    m_selected_asset = -1;
    ClearLoadedAsset();

    // a missing or broken catalog must not hide the files on disk, so the parse only
    // bails out of itself and the directory scan below still runs
    string catalog_error;
    JsonValue root;
    string source;
    const JsonValue* assets = nullptr;
    if (!FileSystem::Exists(m_catalog_path))
    {
        catalog_error = "no catalog registered";
    }
    else if (!FileSystem::ReadFile(m_catalog_path, source))
    {
        catalog_error =
            "catalog could not be read from " +
            m_catalog_path;
    }
    else
    {
        string parse_error;
        if (!mcp_json::parse(source, root, parse_error))
        {
            catalog_error = "invalid catalog, " + parse_error;
        }
        else
        {
            const JsonValue* schema_version =
                root.find("schema_version");
            assets = root.find("assets");
            if (
                root.type != mcp_json::kind::object ||
                !schema_version ||
                static_cast<int>(schema_version->number_or(0.0)) != 2 ||
                !assets ||
                assets->type != mcp_json::kind::object
            )
            {
                catalog_error =
                    "unsupported or invalid asset catalog schema";
                assets = nullptr;
            }
        }
    }

    static const vector<pair<string, JsonValue>> no_assets;
    for (
        const auto& [catalog_id, value] :
        assets ? assets->object_items : no_assets
    )
    {
        if (value.type != mcp_json::kind::object)
        {
            continue;
        }

        AssetEntry asset;
        asset.id =
            value.find("id") ?
            value.find("id")->string_or(catalog_id) :
            catalog_id;
        asset.name =
            value.find("name") ?
            value.find("name")->string_or(asset.id) :
            asset.id;
        asset.type =
            value.find("type") ?
            lower_copy(value.find("type")->string_or("")) :
            "";
        asset.path =
            value.find("path") ?
            value.find("path")->string_or("") :
            "";
        asset.source_path =
            value.find("source_path") ?
            value.find("source_path")->string_or("") :
            "";
        asset.thumbnail_path =
            value.find("thumbnail_path") ?
            value.find("thumbnail_path")->string_or("") :
            "";
        asset.aliases = json_strings(value.find("aliases"));
        asset.tags = json_strings(value.find("tags"));
        asset.dependencies = json_strings(
            value.find("dependencies")
        );
        if (asset.type == "prefab")
        {
            for (
                const char* attribute :
                {
                    "mesh_path",
                    "material_path"
                }
            )
            {
                for (
                    const string& dependency :
                    collect_xml_references(
                        asset.path,
                        attribute
                    )
                )
                {
                    if (
                        find(
                            asset.dependencies.begin(),
                            asset.dependencies.end(),
                            dependency
                        ) == asset.dependencies.end()
                    )
                    {
                        asset.dependencies.push_back(dependency);
                    }
                }
            }
        }
        const vector<string> direct_dependencies =
            asset.dependencies;
        for (const string& dependency : direct_dependencies)
        {
            if (
                lower_copy(
                    FileSystem::GetExtensionFromFilePath(
                        dependency
                    )
                ) != ".xml"
            )
            {
                continue;
            }
            for (
                const string& texture_path :
                collect_xml_references(
                    dependency,
                    "texture_path"
                )
            )
            {
                if (
                    find(
                        asset.dependencies.begin(),
                        asset.dependencies.end(),
                        texture_path
                    ) == asset.dependencies.end()
                )
                {
                    asset.dependencies.push_back(texture_path);
                }
            }
        }
        const JsonValue* quality = value.find("quality");
        if (
            quality &&
            quality->type == mcp_json::kind::object
        )
        {
            asset.quality_score =
                quality->find("score") ?
                static_cast<float>(
                    quality->find("score")->number_or(0.0)
                ) :
                0.0f;
            asset.quality_verified =
                quality->find("verified") ?
                quality->find("verified")->boolean_or(false) :
                false;
        }

        if (
            asset.type == "mesh" ||
            asset.type == "material" ||
            asset.type == "prefab" ||
            asset.type == "texture"
        )
        {
            m_assets.emplace_back(move(asset));
        }
    }

    // the catalog only holds what an agent chose to register, browse the resource
    // directories too so no file on disk is unreachable from the tool
    {
        unordered_set<string> known;
        for (const AssetEntry& entry : m_assets)
        {
            known.insert(entry.type + ":" + lower_copy(entry.id));
            known.insert(entry.type + ":" + lower_copy(entry.name));
            known.insert(
                "path:" + normalized_path(entry.path)
            );
        }

        const auto scan =
            [this, &known](
                const string& root,
                const char* folder,
                const char* type
            )
        {
            if (root.empty())
            {
                return;
            }
            const string directory = root + folder;
            error_code error;
            filesystem::directory_iterator iterator(
                filesystem::path(directory),
                error
            );
            if (error)
            {
                return;
            }

            for (
                ;
                iterator != filesystem::directory_iterator();
                iterator.increment(error)
            )
            {
                if (error)
                {
                    return;
                }

                const filesystem::directory_entry& item = *iterator;
                if (!item.is_regular_file(error) || error)
                {
                    error.clear();
                    continue;
                }

                const string path =
                    item.path().generic_string();
                if (asset_type_from_path(path) != type)
                {
                    continue;
                }
                const string stem =
                    FileSystem::
                        GetFileNameWithoutExtensionFromFilePath(
                            path
                        );
                const string id =
                    FileSystem::GetFileNameFromFilePath(path);
                if (
                    known.count(
                        "path:" + normalized_path(path)
                    ) ||
                    known.count(type + string(":") + lower_copy(stem)) ||
                    known.count(type + string(":") + lower_copy(id))
                )
                {
                    continue;
                }

                AssetEntry entry;
                entry.id = id;
                entry.name = stem;
                entry.type = type;
                entry.path = path;
                entry.disk_only = true;

                known.insert(type + string(":") + lower_copy(stem));
                known.insert("path:" + normalized_path(path));
                m_assets.emplace_back(move(entry));
            }
        };

        const auto scan_root = [&](const string& root)
        {
            scan(root, "meshes", "mesh");
            scan(root, "materials", "material");
            scan(root, "textures", "texture");
            scan(root, "prefabs", "prefab");
        };
        const string library_root =
            FileSystem::GetDirectoryFromFilePath(m_catalog_path);
        scan_root(library_root);
        const string generated_root =
            World::GetGeneratedResourceDirectory();
        if (
            !generated_root.empty() &&
            normalized_path(generated_root) !=
            normalized_path(library_root)
        )
        {
            scan_root(generated_root);
        }
    }

    sort(
        m_assets.begin(),
        m_assets.end(),
        [this](
            const AssetEntry& first,
            const AssetEntry& second
        )
        {
            if (m_sort_mode == 1)
            {
                const float first_quality = first.quality_score;
                const float second_quality = second.quality_score;
                if (first_quality != second_quality)
                {
                    return first_quality > second_quality;
                }
            }
            else if (
                m_sort_mode == 2 &&
                first.type != second.type
            )
            {
                return first.type < second.type;
            }
            return lower_copy(first.name) < lower_copy(second.name);
        }
    );

    for (size_t index = 0; index < m_assets.size(); index++)
    {
        if (m_assets[index].id == previous_id)
        {
            m_selected_asset = static_cast<int>(index);
            break;
        }
    }

    m_status =
        to_string(m_assets.size()) +
        (
            m_assets.size() == 1 ?
            " asset" :
            " assets"
        );
    if (!catalog_error.empty())
    {
        m_status += ", " + catalog_error;
    }
    if (
        reload_preview &&
        m_selected_asset >= 0
    )
    {
        LoadSelectedAsset(false, true);
    }
}

void AssetViewer::ClearLoadedAsset()
{
    m_revision_previewing = false;
    m_revision.candidate_previewed = false;
    DestroyPreviewScene();
    m_mesh.reset();
    m_working_meshes.clear();
    m_preview_meshes.clear();
    m_preview_meshes_sources.clear();
    m_material.reset();
    m_texture.reset();
    m_loaded_path.clear();
    m_loaded_write_time.clear();
    m_prefab_entity_count = 0;
    m_bake_confirmation_open = false;
    m_working_sub_meshes.clear();
    m_working_lods.clear();
    m_working_vertices.clear();
    m_working_indices.clear();
    m_missing_dependencies.clear();
    m_working_modified = false;
    m_working_editable = false;
    m_working_lods_built = false;
    m_working_lods_attempted = false;
    m_working_lods_scanned = false;
    m_preview_lod = 0;
    m_target_ratio = 0.5f;
}

void AssetViewer::LoadSelectedAsset(
    const bool reset_view,
    const bool force_reload
)
{
    ClearLoadedAsset();
    if (
        m_selected_asset < 0 ||
        m_selected_asset >= static_cast<int>(m_assets.size())
    )
    {
        return;
    }

    m_selected_dependency_path.clear();
    const AssetEntry& asset = m_assets[m_selected_asset];
    if (asset.path.empty())
    {
        m_status = "The selected asset has no path.";
        return;
    }
    if (!path_is_in_viewer_roots(asset.path))
    {
        m_status =
            "The selected asset is outside the library and mcp blockout folders.";
        return;
    }
    if (!FileSystem::Exists(asset.path))
    {
        m_status =
            "Asset file not found: " +
            asset.path;
        return;
    }

    if (asset.type == "mesh")
    {
        if (!AddWorkingMesh(asset.path, force_reload))
        {
            m_status =
                "Mesh could not be loaded: " +
                asset.path;
            return;
        }
        m_mesh = m_working_meshes.front().mesh;
        LoadWorkingGeometry();
    }
    else if (asset.type == "material")
    {
        m_material =
            ResourceCache::Load<Material>(asset.path);
        if (m_material && force_reload)
        {
            m_material->LoadFromFile(asset.path);
        }
        if (!m_material)
        {
            m_status =
                "Material could not be loaded: " +
                asset.path;
            return;
        }
    }
    else if (asset.type == "texture")
    {
        m_texture =
            ResourceCache::Load<RHI_Texture>(asset.path);
        if (!m_texture)
        {
            m_status =
                "Texture could not be loaded: " +
                asset.path;
            return;
        }
        // load only produces a cpu texture, the preview needs it on the gpu
        m_texture->PrepareForGpu();
        if (!m_texture->GetRhiResource())
        {
            m_texture.reset();
            m_status =
                "Texture could not be uploaded: " +
                asset.path;
            return;
        }
    }
    else
    {
        pugi::xml_document document;
        const pugi::xml_parse_result result =
            document.load_file(asset.path.c_str());
        const pugi::xml_node prefab =
            document.child("Prefab");
        if (!result || !prefab)
        {
            m_status =
                "Prefab could not be parsed: " +
                asset.path;
            return;
        }
        m_prefab_entity_count =
            1 +
            count_prefab_entities(prefab);
        CollectPrefabDependencies(asset.path);
        // the optimize tools see every mesh the prefab uses as one job, a chair is simplified as a
        // chair rather than one plank at a time
        CollectPrefabMeshes(asset.path, force_reload);
        LoadWorkingGeometry();
    }

    m_loaded_path = asset.path;
    m_loaded_write_time =
        FileSystem::GetLastWriteTime(m_loaded_path);
    if (reset_view)
    {
        m_preview_yaw = 0.65f;
        m_preview_pitch = 0.35f;
        m_preview_zoom = 1.0f;
        m_texture_pan = math::Vector2::Zero;
    }
    m_status =
        "Loaded renderer preview for " +
        asset_display_name(asset.name);
    if (!m_missing_dependencies.empty())
    {
        m_status =
            "Loaded " +
            asset_display_name(asset.name) +
            " but " +
            to_string(m_missing_dependencies.size()) +
            " referenced file(s) are missing on disk";
    }
    RebuildPreviewScene();
}

void AssetViewer::LoadDependencyPreview(
    const string& path
)
{
    ClearLoadedAsset();
    m_selected_asset = -1;
    m_selected_dependency_path = path;
    m_preview_zoom = 1.0f;
    m_texture_pan = math::Vector2::Zero;
    if (!FileSystem::Exists(path))
    {
        m_status =
            "Linked resource not found: " +
            path;
        return;
    }

    const string type = asset_type_from_path(path);
    if (type == "mesh")
    {
        if (!AddWorkingMesh(path, false))
        {
            m_status =
                "Linked mesh could not be loaded: " +
                path;
            return;
        }
        m_mesh = m_working_meshes.front().mesh;
        LoadWorkingGeometry();
    }
    else if (type == "material")
    {
        m_material =
            ResourceCache::Load<Material>(path);
        if (!m_material)
        {
            m_status =
                "Linked material could not be loaded: " +
                path;
            return;
        }
    }
    else if (type == "texture")
    {
        m_texture =
            ResourceCache::Load<RHI_Texture>(path);
        if (!m_texture)
        {
            m_status =
                "Linked texture could not be loaded: " +
                path;
            return;
        }
        m_texture->PrepareForGpu();
        if (!m_texture->GetRhiResource())
        {
            m_texture.reset();
            m_status =
                "Linked texture could not be uploaded: " +
                path;
            return;
        }
    }
    else
    {
        m_status =
            "Linked resource type is not previewable: " +
            path;
        return;
    }

    m_loaded_path = path;
    m_loaded_write_time =
        FileSystem::GetLastWriteTime(path);
    m_status =
        "Previewing linked " +
        type +
        " " +
        FileSystem::GetFileNameFromFilePath(path);
    RebuildPreviewScene();
}

void AssetViewer::CollectPrefabDependencies(const string& path)
{
    m_missing_dependencies.clear();

    pugi::xml_document document;
    if (!document.load_file(path.c_str()))
    {
        return;
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

        for (
            const char* attribute_name :
            {
                "mesh_path",
                "material_path",
                "texture_path"
            }
        )
        {
            const string reference =
                node.attribute(attribute_name).as_string();
            if (reference.empty() || FileSystem::Exists(reference))
            {
                continue;
            }

            const string missing =
                FileSystem::GetFileNameFromFilePath(reference);
            if (
                find(
                    m_missing_dependencies.begin(),
                    m_missing_dependencies.end(),
                    missing
                ) == m_missing_dependencies.end()
            )
            {
                m_missing_dependencies.push_back(missing);
            }
        }
    }
}

bool AssetViewer::AddWorkingMesh(
    const string& path,
    const bool force_reload
)
{
    for (const WorkingMesh& working : m_working_meshes)
    {
        if (normalized_path(working.path) == normalized_path(path))
        {
            return true;
        }
    }
    if (!FileSystem::Exists(path))
    {
        return false;
    }

    shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>(path);
    if (mesh && force_reload)
    {
        mesh->LoadFromFile(path);
    }
    if (!mesh || mesh->GetVertexCount() == 0)
    {
        return false;
    }

    WorkingMesh working;
    working.mesh = move(mesh);
    working.path = path;
    m_working_meshes.emplace_back(move(working));
    return true;
}

void AssetViewer::CollectPrefabMeshes(
    const string& path,
    const bool force_reload
)
{
    // in the order the prefab references them so the sub mesh list reads top to bottom like the
    // hierarchy does, a set would shuffle a chair's legs above its seat
    const vector<string> mesh_paths =
        collect_xml_references(path, "mesh_path");
    for (const string& mesh_path : mesh_paths)
    {
        if (!path_is_in_viewer_roots(mesh_path))
        {
            continue;
        }
        AddWorkingMesh(mesh_path, force_reload);
    }
}

void AssetViewer::CollectPreviewRenderSlots(Entity* root)
{
    m_preview_render_slots.clear();
    if (!root)
    {
        return;
    }

    vector<Entity*> entities;
    entities.push_back(root);
    root->GetDescendants(&entities);
    for (Entity* entity : entities)
    {
        Render* render =
            entity ?
            entity->GetComponent<Render>() :
            nullptr;
        if (!render || !render->GetMesh())
        {
            continue;
        }

        for (
            uint32_t mesh_index = 0;
            mesh_index < static_cast<uint32_t>(m_working_meshes.size());
            mesh_index++
        )
        {
            if (
                m_working_meshes[mesh_index].mesh.get() !=
                render->GetMesh()
            )
            {
                continue;
            }

            PreviewRenderSlot slot;
            slot.entity_id = entity->GetObjectId();
            slot.mesh_index = mesh_index;
            slot.sub_mesh = render->GetSubMeshIndex();
            m_preview_render_slots.push_back(slot);
            break;
        }
    }
}

void AssetViewer::ScanRevisionCandidates(const bool force)
{
    const string candidates_root =
        World::GetLibraryResourceDirectory() +
        "candidates";
    vector<string> manifests;
    vector<string> signature_files;
    string signature;

    error_code iterator_error;
    filesystem::recursive_directory_iterator iterator(
        filesystem::path(candidates_root),
        iterator_error
    );
    for (
        ;
        !iterator_error &&
        iterator != filesystem::recursive_directory_iterator();
        iterator.increment(iterator_error)
    )
    {
        const filesystem::directory_entry& item = *iterator;
        if (!item.is_regular_file(iterator_error) || iterator_error)
        {
            iterator_error.clear();
            continue;
        }

        const string path = item.path().generic_string();
        const string file_name = lower_copy(
            FileSystem::GetFileNameFromFilePath(path)
        );
        if (
            lower_copy(
                FileSystem::GetExtensionFromFilePath(path)
            ) != ".json" ||
            file_name.find(".tmp") != string::npos
        )
        {
            continue;
        }

        signature_files.push_back(path);
        if (file_name != "manifest.json")
        {
            continue;
        }
        manifests.push_back(path);
    }
    sort(manifests.begin(), manifests.end());
    sort(signature_files.begin(), signature_files.end());
    for (const string& path : signature_files)
    {
        signature +=
            path +
            "|" +
            FileSystem::GetLastWriteTime(path) +
            ";";
    }

    if (!force && signature == m_revision_scan_signature)
    {
        return;
    }

    const RevisionStatus previous = m_revision;
    m_revision_scan_signature = signature;
    m_revision_candidates.clear();
    for (const string& manifest_path : manifests)
    {
        string source;
        JsonValue root;
        string parse_error;
        if (
            !FileSystem::ReadFile(manifest_path, source) ||
            !mcp_json::parse(source, root, parse_error) ||
            root.type != mcp_json::kind::object
        )
        {
            continue;
        }

        RevisionCandidate candidate;
        RevisionStatus& status = candidate.status;
        status.manifest_path = manifest_path;
        status.base_asset_id = json_string_any(
            root,
            { "base_asset_id", "asset_id" }
        );
        if (status.base_asset_id.empty())
        {
            const filesystem::path relative =
                filesystem::path(manifest_path).lexically_relative(
                    filesystem::path(candidates_root)
                );
            if (!relative.empty())
            {
                status.base_asset_id =
                    relative.begin()->generic_string();
            }
        }

        const string candidate_value = json_string_any(
            root,
            {
                "candidate_path",
                "prefab_path",
                "output_path",
                "path"
            }
        );
        status.generation = json_uint_any(
            root,
            { "generation", "revision_generation" }
        );
        const string state = lower_copy(
            json_string_any(root, { "status", "state" })
        );
        const JsonValue* active_value =
            json_object_value(root, "active");
        const bool manifest_active =
            !active_value ||
            active_value->type != mcp_json::kind::boolean ||
            active_value->boolean_value;
        if (
            status.base_asset_id.empty() ||
            candidate_value.empty() ||
            status.generation == 0 ||
            !manifest_active ||
            state == "applied" ||
            state == "discarded" ||
            state == "cancelled"
        )
        {
            continue;
        }

        status.candidate_path = absolute_manifest_path(
            candidate_value,
            manifest_path
        );
        const string candidate_directory =
            FileSystem::GetDirectoryFromFilePath(manifest_path);
        const string candidate_extension =
            FileSystem::GetExtensionFromFilePath(
                status.candidate_path
            );
        if (
            (
                candidate_extension != ".prefab" &&
                candidate_extension != ".mesh" &&
                candidate_extension != ".xml" &&
                candidate_extension != ".png" &&
                candidate_extension != ".jpg" &&
                candidate_extension != ".jpeg" &&
                candidate_extension != ".tga" &&
                candidate_extension != ".dds"
            ) ||
            status.candidate_path.find("..") != string::npos ||
            !path_is_within(
                status.candidate_path,
                candidate_directory
            ) ||
            !resolved_path_is_within(
                status.candidate_path,
                candidates_root
            ) ||
            !FileSystem::Exists(status.candidate_path)
        )
        {
            continue;
        }

        const auto [candidate_entities, candidate_dependencies] =
            prefab_counts(status.candidate_path);
        const uint64_t manifest_candidate_entities =
            json_uint_any(
                root,
                { "candidate_entity_count", "entity_count" }
            );
        const uint64_t manifest_candidate_dependencies =
            json_uint_any(
                root,
                {
                    "candidate_dependency_count",
                    "dependency_count"
                }
            );
        status.candidate_entity_count =
            manifest_candidate_entities != 0 ?
            manifest_candidate_entities :
            candidate_entities;
        status.candidate_dependency_count =
            manifest_candidate_dependencies != 0 ?
            manifest_candidate_dependencies :
            candidate_dependencies;

        string base_path = json_string_any(
            root,
            { "base_path", "base_prefab_path" }
        );
        if (!base_path.empty())
        {
            base_path = absolute_manifest_path(
                base_path,
                manifest_path
            );
        }
        for (const AssetEntry& asset : m_assets)
        {
            if (
                asset.id == status.base_asset_id &&
                asset.type == "prefab"
            )
            {
                base_path = asset.path;
                break;
            }
        }
        const auto [base_entities, base_dependencies] =
            prefab_counts(base_path);
        const uint64_t manifest_base_entities =
            json_uint_any(root, { "base_entity_count" });
        const uint64_t manifest_base_dependencies =
            json_uint_any(root, { "base_dependency_count" });
        status.base_entity_count =
            manifest_base_entities != 0 ?
            manifest_base_entities :
            base_entities;
        status.base_dependency_count =
            manifest_base_dependencies != 0 ?
            manifest_base_dependencies :
            base_dependencies;

        status.request_path =
            candidate_directory +
            "revision_request.json";
        if (FileSystem::Exists(status.request_path))
        {
            string request_source;
            JsonValue request_root;
            string request_error;
            if (
                FileSystem::ReadFile(
                    status.request_path,
                    request_source
                ) &&
                mcp_json::parse(request_source, request_root, request_error)
            )
            {
                const uint64_t request_generation =
                    json_uint_any(request_root, { "generation" });
                if (request_generation == status.generation)
                {
                    status.request_pending = true;
                    status.request_action = json_string_any(
                        request_root,
                        { "action" }
                    );
                }
            }
        }
        else
        {
            const string response_path =
                candidate_directory +
                "revision_response.json";
            string response_source;
            JsonValue response_root;
            string response_error;
            if (
                FileSystem::ReadFile(
                    response_path,
                    response_source
                ) &&
                mcp_json::parse(response_source, response_root, response_error) &&
                json_uint_any(
                    response_root,
                    { "generation" }
                ) == status.generation
            )
            {
                status.request_error = json_string_any(
                    response_root,
                    { "error" }
                );
            }
        }

        status.candidate_active = true;
        m_revision_candidates.push_back(move(candidate));
    }

    const string selected_id =
        m_selected_asset >= 0 &&
        m_selected_asset < static_cast<int>(m_assets.size()) ?
        m_assets[m_selected_asset].id :
        "";
    const string preferred_id =
        m_revision_previewing &&
        !previous.base_asset_id.empty() ?
        previous.base_asset_id :
        selected_id;
    RevisionStatus selected;
    for (const RevisionCandidate& candidate : m_revision_candidates)
    {
        const RevisionStatus& status = candidate.status;
        if (
            !preferred_id.empty() &&
            status.base_asset_id != preferred_id
        )
        {
            continue;
        }
        if (
            !selected.candidate_active ||
            status.generation > selected.generation
        )
        {
            selected = status;
        }
    }
    if (!selected.candidate_active)
    {
        for (const RevisionCandidate& candidate : m_revision_candidates)
        {
            if (
                !selected.candidate_active ||
                candidate.status.generation > selected.generation
            )
            {
                selected = candidate.status;
            }
        }
    }

    const bool preview_survived =
        m_revision_previewing &&
        selected.candidate_active &&
        selected.base_asset_id == previous.base_asset_id &&
        selected.generation == previous.generation;
    m_revision = selected;
    m_revision_previewing = preview_survived;
    m_revision.candidate_previewed = preview_survived;
    if (
        previous.candidate_active &&
        previous.candidate_previewed &&
        !preview_survived
    )
    {
        if (
            m_selected_asset >= 0 &&
            m_selected_asset < static_cast<int>(m_assets.size())
        )
        {
            LoadSelectedAsset(false, true);
        }
        else
        {
            ClearLoadedAsset();
        }
        m_status = "Asset revision was completed outside the editor";
    }
}

bool AssetViewer::SelectRevisionCandidate(
    const string& asset_id,
    const uint64_t generation,
    string& error
)
{
    const RevisionStatus previous = m_revision;
    const bool was_previewing = m_revision_previewing;
    ScanRevisionCandidates(true);
    RevisionStatus selected;
    for (const RevisionCandidate& candidate : m_revision_candidates)
    {
        const RevisionStatus& status = candidate.status;
        if (
            !asset_id.empty() &&
            status.base_asset_id != asset_id
        )
        {
            continue;
        }
        if (generation != 0 && status.generation != generation)
        {
            continue;
        }
        if (
            !selected.candidate_active ||
            status.generation > selected.generation
        )
        {
            selected = status;
        }
    }
    if (!selected.candidate_active)
    {
        error =
            generation == 0 ?
            "no active asset revision candidate was found" :
            "asset revision generation is stale";
        return false;
    }

    m_revision = selected;
    m_revision_previewing =
        was_previewing &&
        previous.base_asset_id == selected.base_asset_id &&
        previous.generation == selected.generation;
    m_revision.candidate_previewed = m_revision_previewing;
    return true;
}

bool AssetViewer::LoadRevisionCandidate(string& error)
{
    if (!m_revision.candidate_active)
    {
        error = "no active asset revision candidate was found";
        return false;
    }
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before previewing a revision";
        return false;
    }

    const string extension =
        FileSystem::GetExtensionFromFilePath(
            m_revision.candidate_path
        );
    if (extension != ".prefab")
    {
        LoadDependencyPreview(m_revision.candidate_path);
    }
    else
    {
        pugi::xml_document document;
        const pugi::xml_parse_result result =
            document.load_file(m_revision.candidate_path.c_str());
        const pugi::xml_node prefab = document.child("Prefab");
        if (!result || !prefab)
        {
            error =
                "asset revision candidate prefab could not be parsed";
            return false;
        }

        ClearLoadedAsset();
        m_selected_dependency_path.clear();
        m_prefab_entity_count =
            1 +
            count_prefab_entities(prefab);
        CollectPrefabDependencies(m_revision.candidate_path);
        CollectPrefabMeshes(m_revision.candidate_path, false);
        LoadWorkingGeometry();
        m_loaded_path = m_revision.candidate_path;
        m_loaded_write_time =
            FileSystem::GetLastWriteTime(m_loaded_path);
        RebuildPreviewScene();
    }
    if (
        !PreviewRoot() &&
        !m_mesh &&
        !m_material &&
        !m_texture
    )
    {
        error = m_status.empty()
            ? "asset revision candidate could not be previewed"
            : m_status;
        return false;
    }

    m_revision_previewing = true;
    m_revision.candidate_previewed = true;
    m_visible = true;
    m_status =
        "Previewing revision " +
        to_string(m_revision.generation) +
        " for " +
        m_revision.base_asset_id;
    return true;
}

bool AssetViewer::RequestRevision(
    const char* action,
    const uint64_t generation,
    const bool confirm,
    string& error
)
{
    if (!confirm)
    {
        error =
            string("confirm=true is required to ") +
            action +
            " an asset revision";
        return false;
    }
    if (
        generation == 0 ||
        generation != m_revision.generation
    )
    {
        error = "asset revision generation is stale";
        return false;
    }
    if (!m_revision.candidate_active)
    {
        error = "no active asset revision candidate was found";
        return false;
    }
    if (m_revision.request_pending)
    {
        error =
            "an asset revision request is already pending";
        return false;
    }

    const string request =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"action\": " +
        serialize_json_string(action) +
        ",\n"
        "  \"base_asset_id\": " +
        serialize_json_string(m_revision.base_asset_id) +
        ",\n"
        "  \"candidate_path\": " +
        serialize_json_string(m_revision.candidate_path) +
        ",\n"
        "  \"manifest_path\": " +
        serialize_json_string(m_revision.manifest_path) +
        ",\n"
        "  \"generation\": " +
        to_string(m_revision.generation) +
        ",\n"
        "  \"confirm\": true\n"
        "}\n";
    const string temporary_path =
        m_revision.request_path +
        ".tmp";
    if (!FileSystem::WriteFile(temporary_path, request))
    {
        error = "asset revision request could not be written";
        return false;
    }

    error_code rename_error;
    filesystem::rename(
        filesystem::path(temporary_path),
        filesystem::path(m_revision.request_path),
        rename_error
    );
    if (rename_error)
    {
        FileSystem::Delete(temporary_path);
        error = "asset revision request could not be published";
        return false;
    }

    m_revision.request_pending = true;
    m_revision.request_action = action;
    m_status =
        string("Requested asset revision ") +
        action +
        " for generation " +
        to_string(generation);
    return true;
}

AssetViewer::RevisionStatus AssetViewer::GetRevisionStatus(
    const string& asset_id
)
{
    string error;
    if (!asset_id.empty())
    {
        if (!SelectRevisionCandidate(asset_id, 0, error))
        {
            return {};
        }
    }
    else
    {
        ScanRevisionCandidates(false);
    }
    m_revision.candidate_previewed = m_revision_previewing;
    return m_revision;
}

bool AssetViewer::PreviewRevision(
    const string& asset_id,
    const uint64_t generation,
    string& error
)
{
    if (!SelectRevisionCandidate(asset_id, generation, error))
    {
        return false;
    }
    return LoadRevisionCandidate(error);
}

bool AssetViewer::RequestRevisionApply(
    const string& asset_id,
    const uint64_t generation,
    const bool confirm,
    string& error
)
{
    if (!SelectRevisionCandidate(asset_id, generation, error))
    {
        return false;
    }
    return RequestRevision("apply", generation, confirm, error);
}

bool AssetViewer::RequestRevisionDiscard(
    const string& asset_id,
    const uint64_t generation,
    const bool confirm,
    string& error
)
{
    if (!SelectRevisionCandidate(asset_id, generation, error))
    {
        return false;
    }
    return RequestRevision("discard", generation, confirm, error);
}

void AssetViewer::LoadWorkingGeometry()
{
    m_working_sub_meshes.clear();
    m_working_lods.clear();
    m_working_vertices.clear();
    m_working_indices.clear();
    m_working_modified = false;
    m_working_editable = false;
    m_working_lods_attempted = false;
    m_preview_lod = 0;
    if (m_working_meshes.empty())
    {
        return;
    }

    // only lod 0, the lods sit back to back with lod local indices so drawing them raw misplaces every triangle
    for (
        uint32_t mesh_index = 0;
        mesh_index < static_cast<uint32_t>(m_working_meshes.size());
        mesh_index++
    )
    {
        Mesh* mesh = m_working_meshes[mesh_index].mesh.get();
        for (
            uint32_t sub_mesh = 0;
            sub_mesh < mesh->GetSubMeshCount();
            sub_mesh++
        )
        {
            if (mesh->GetSubMesh(sub_mesh).lods.empty())
            {
                continue;
            }

            WorkingSubMesh working;
            mesh->GetGeometry(
                sub_mesh,
                &working.indices,
                &working.vertices
            );
            if (working.vertices.empty() || working.indices.size() < 3)
            {
                continue;
            }

            working.source_mesh = mesh_index;
            working.source_sub_mesh = sub_mesh;
            working.source_vertex_count =
                static_cast<uint32_t>(working.vertices.size());
            working.source_index_count =
                static_cast<uint32_t>(working.indices.size());
            m_working_sub_meshes.emplace_back(move(working));
        }
    }

    m_working_editable = !m_working_sub_meshes.empty();
    m_working_lods_scanned = false;
    FlattenWorkingGeometry();
    RefreshPreviewMeshGeometry();
    m_preview_dirty = true;
}

// reads the levels the mesh already carries so the picker exists the moment a mesh is selected, having
// it appear only after pressing build meant a mesh that shipped with a full chain looked like it had none
void AssetViewer::LoadExistingLods()
{
    m_working_lods.clear();
    m_working_lods_built = false;
    if (m_working_meshes.empty() || m_working_sub_meshes.empty())
    {
        return;
    }

    const auto source_mesh =
        [this](const WorkingSubMesh& working)
        {
            return m_working_meshes[working.source_mesh].mesh.get();
        };

    // the chain is only as deep as the shallowest sub mesh, a level that exists for one part and not
    // another would silently drop the parts that ran out of levels
    uint32_t depth =
        source_mesh(m_working_sub_meshes.front())->GetLodCount(
            m_working_sub_meshes.front().source_sub_mesh
        );
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        depth = min(
            depth,
            source_mesh(working)->GetLodCount(working.source_sub_mesh)
        );
    }
    if (depth < 2)
    {
        return;
    }

    vector<vector<WorkingSubMesh>> levels;
    for (uint32_t lod = 0; lod < depth; lod++)
    {
        vector<WorkingSubMesh> level;
        for (const WorkingSubMesh& source : m_working_sub_meshes)
        {
            WorkingSubMesh working;
            working.source_mesh = source.source_mesh;
            working.source_sub_mesh = source.source_sub_mesh;
            working.source_vertex_count = source.source_vertex_count;
            working.source_index_count = source.source_index_count;
            if (
                !source_mesh(source)->GetGeometryLod(
                    source.source_sub_mesh,
                    lod,
                    &working.indices,
                    &working.vertices
                ) ||
                working.vertices.empty() ||
                working.indices.size() < 3
            )
            {
                return;
            }

            level.emplace_back(move(working));
        }

        levels.emplace_back(move(level));
    }

    m_working_lods = move(levels);
}

void AssetViewer::FlattenWorkingGeometry()
{
    m_working_vertices.clear();
    m_working_indices.clear();

    // the preview draws one mesh, so whatever level is selected gets concatenated with rebased
    // indices, the per sub mesh copies stay untouched and remain what gets baked
    const vector<WorkingSubMesh>& source =
        (
            m_preview_lod > 0 &&
            m_preview_lod < static_cast<int>(m_working_lods.size())
        )
            ? m_working_lods[m_preview_lod]
            : m_working_sub_meshes;

    for (const WorkingSubMesh& working : source)
    {
        const uint32_t base =
            static_cast<uint32_t>(m_working_vertices.size());
        m_working_indices.reserve(
            m_working_indices.size() + working.indices.size()
        );
        for (const uint32_t index : working.indices)
        {
            m_working_indices.push_back(base + index);
        }
        m_working_vertices.insert(
            m_working_vertices.end(),
            working.vertices.begin(),
            working.vertices.end()
        );
    }
}

bool AssetViewer::EnsurePreviewMeshes()
{
    uint64_t required_capacity = 0;
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        required_capacity += working.source_vertex_count;
    }

    vector<const Mesh*> sources;
    sources.reserve(m_working_meshes.size());
    for (const WorkingMesh& working : m_working_meshes)
    {
        sources.push_back(working.mesh.get());
    }

    // the pool survives simplify, revert and lod switches, rebuilding it per edit would grow the
    // global geometry buffer without bound because it only ever appends
    if (
        m_preview_meshes.size() == m_working_sub_meshes.size() &&
        m_preview_meshes_sources == sources &&
        m_preview_meshes_capacity >= required_capacity
    )
    {
        return !m_preview_meshes.empty();
    }

    m_preview_meshes.clear();
    m_preview_meshes_sources = sources;
    m_preview_meshes_capacity = required_capacity;
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        // dynamic rounds the reservation up to a power of two, every later edit only shrinks the
        // geometry so it always fits back into this one allocation
        shared_ptr<Mesh> scratch = make_shared<Mesh>();
        scratch->SetObjectName(
            "asset_viewer_working_" +
            to_string(working.source_mesh) +
            "_" +
            to_string(working.source_sub_mesh)
        );

        // a faithful mirror of the working data, any post process here would silently disagree with
        // the triangle counts the panel reports
        scratch->SetFlags(0);
        scratch->SetDynamic(true);

        // sized from the source rather than from the current working copy, an already simplified
        // copy would allocate too little to ever revert back into
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;
        m_working_meshes[working.source_mesh].mesh->GetGeometry(
            working.source_sub_mesh,
            &indices,
            &vertices
        );
        scratch->AddGeometry(vertices, indices, false);
        scratch->CreateGpuBuffers();
        m_preview_meshes.push_back(move(scratch));
    }

    return !m_preview_meshes.empty();
}

void AssetViewer::RefreshPreviewMeshGeometry()
{
    if (m_working_meshes.empty() || m_working_sub_meshes.empty())
    {
        return;
    }

    // an untouched asset draws straight from the cached mesh, the scratch pool costs a geometry
    // buffer allocation per asset and that buffer only ever appends, so it stays unallocated until
    // there is an actual edit or a lod level to show
    const bool wants_scratch =
        m_working_modified ||
        m_preview_lod > 0;
    if (!wants_scratch)
    {
        for (const PreviewRenderSlot& slot : m_preview_render_slots)
        {
            Entity* entity = World::GetEntityById(slot.entity_id);
            Render* render =
                entity ?
                entity->GetComponent<Render>() :
                nullptr;
            if (
                !render ||
                slot.mesh_index >= m_working_meshes.size()
            )
            {
                continue;
            }

            Mesh* source = m_working_meshes[slot.mesh_index].mesh.get();
            if (render->GetMesh() == source)
            {
                continue;
            }

            render->SetMesh(source, slot.sub_mesh);
        }
        m_preview_dirty = true;
        return;
    }

    if (!EnsurePreviewMeshes())
    {
        return;
    }

    const vector<WorkingSubMesh>& source =
        (
            m_preview_lod > 0 &&
            m_preview_lod < static_cast<int>(m_working_lods.size())
        )
            ? m_working_lods[m_preview_lod]
            : m_working_sub_meshes;

    for (size_t index = 0; index < m_preview_meshes.size(); index++)
    {
        if (index >= source.size())
        {
            break;
        }

        vector<RHI_Vertex_PosTexNorTan> vertices =
            source[index].vertices;
        vector<uint32_t> indices = source[index].indices;
        if (vertices.empty() || indices.size() < 3)
        {
            continue;
        }

        m_preview_meshes[index]->UpdateGeometry(
            vertices,
            indices
        );
    }

    // repoint every render at its scratch mesh, the mapping is kept separately because the render
    // sub mesh index becomes zero the first time this runs
    for (const PreviewRenderSlot& slot : m_preview_render_slots)
    {
        Entity* entity = World::GetEntityById(slot.entity_id);
        Render* render =
            entity ?
            entity->GetComponent<Render>() :
            nullptr;
        if (!render)
        {
            continue;
        }

        for (size_t index = 0; index < m_working_sub_meshes.size(); index++)
        {
            if (
                m_working_sub_meshes[index].source_mesh !=
                    slot.mesh_index ||
                m_working_sub_meshes[index].source_sub_mesh !=
                    slot.sub_mesh
            )
            {
                continue;
            }

            render->SetMesh(m_preview_meshes[index].get(), 0);
            break;
        }
    }
    m_preview_dirty = true;
}

void AssetViewer::SimplifyWorkingGeometry(const float ratio)
{
    // always start from the source so the slider stays absolute, welding first
    // because simplification cannot collapse edges across duplicated vertices
    LoadWorkingGeometry();
    if (m_working_sub_meshes.empty())
    {
        return;
    }

    const float clamped = clamp(ratio, 0.01f, 1.0f);
    for (WorkingSubMesh& working : m_working_sub_meshes)
    {
        geometry_processing::optimize(
            working.vertices,
            working.indices
        );

        // each sub mesh is simplified against its own budget, a shared triangle target would
        // erase the small parts of a model long before the large ones lose any detail
        const size_t target = max<size_t>(
            24,
            static_cast<size_t>(
                static_cast<float>(working.indices.size()) *
                clamped
            )
        );
        if (
            working.indices.size() < 36 ||
            target >= working.indices.size()
        )
        {
            continue;
        }

        geometry_processing::simplify(
            working.indices,
            working.vertices,
            target,
            true,
            false
        );
    }

    m_working_modified = true;
    m_preview_lod = 0;
    // the mesh's saved chain was derived from geometry that no longer exists, leaving it in the picker
    // would offer levels that do not match the lod 0 on screen, and it must not be read back either
    m_working_lods.clear();
    m_working_lods_built = false;
    m_working_lods_attempted = false;
    m_working_lods_scanned = true;
    FlattenWorkingGeometry();
    RefreshPreviewMeshGeometry();
}

void AssetViewer::OptimizeWorkingGeometry()
{
    if (m_working_sub_meshes.empty())
    {
        return;
    }

    for (WorkingSubMesh& working : m_working_sub_meshes)
    {
        geometry_processing::optimize(
            working.vertices,
            working.indices
        );
    }

    m_working_modified = true;
    m_working_lods.clear();
    m_working_lods_built = false;
    m_working_lods_attempted = false;
    m_working_lods_scanned = true;
    m_preview_lod = 0;
    FlattenWorkingGeometry();
    RefreshPreviewMeshGeometry();
}

void AssetViewer::BuildWorkingLods()
{
    m_working_lods.clear();
    m_working_lods_built = false;
    m_working_lods_scanned = true;
    m_preview_lod = 0;
    m_working_lods_attempted = true;
    if (m_working_sub_meshes.empty())
    {
        return;
    }

    // mirrors the reduction curve mesh uses when it bakes lods, so what the picker shows is what
    // the saved file gets rather than an approximation of it
    static const float screen_thresholds[mesh_lod_count] =
    {
        0.05f,
        0.025f,
        0.012f,
        0.006f,
        0.003f
    };

    m_working_lods.push_back(m_working_sub_meshes);
    for (
        uint32_t lod = 1;
        lod < mesh_lod_count;
        lod++
    )
    {
        vector<WorkingSubMesh> level = m_working_lods.back();
        const float target_ratio =
            screen_thresholds[lod] / screen_thresholds[0];

        uint64_t previous_indices = 0;
        for (const WorkingSubMesh& working : level)
        {
            previous_indices += working.indices.size();
        }

        for (size_t index = 0; index < level.size(); index++)
        {
            WorkingSubMesh& working = level[index];
            if (working.indices.size() <= 64)
            {
                continue;
            }

            // the target is relative to the original rather than to the previous level, so a level
            // that fails to reduce does not drag every level below it up with it
            const size_t target = max<size_t>(
                64,
                static_cast<size_t>(
                    static_cast<float>(
                        m_working_sub_meshes[index].indices.size()
                    ) *
                    target_ratio
                )
            );
            if (target >= working.indices.size())
            {
                continue;
            }

            geometry_processing::simplify(
                working.indices,
                working.vertices,
                target,
                true,
                false
            );
        }

        uint64_t level_indices = 0;
        for (const WorkingSubMesh& working : level)
        {
            level_indices += working.indices.size();
        }

        // a level that barely moved is a duplicate of the one above it, keeping it would put two
        // identical looking entries in the picker and two identical ranges in the saved file
        if (
            level_indices >=
            static_cast<uint64_t>(
                static_cast<float>(previous_indices) *
                mesh_lod_min_reduction
            )
        )
        {
            break;
        }

        m_working_lods.push_back(move(level));
    }

    // a single entry means nothing could be reduced, so there is no chain worth showing
    if (m_working_lods.size() < 2)
    {
        m_working_lods.clear();
    }
    else
    {
        m_working_lods_built = true;
    }

    FlattenWorkingGeometry();
    RefreshPreviewMeshGeometry();
}

bool AssetViewer::SaveWorkingGeometry()
{
    if (
        m_working_meshes.empty() ||
        !m_working_editable ||
        m_loaded_path.empty() ||
        m_working_sub_meshes.empty()
    )
    {
        return false;
    }

    uint32_t files_written = 0;
    string written_path;
    for (
        uint32_t mesh_index = 0;
        mesh_index < static_cast<uint32_t>(m_working_meshes.size());
        mesh_index++
    )
    {
        const WorkingMesh& source = m_working_meshes[mesh_index];

        // bake into a standalone mesh so the cached resource is untouched until the
        // file is reloaded below, lods are rebuilt from the edited geometry
        Mesh baked;
        baked.SetObjectName(
            FileSystem::GetFileNameWithoutExtensionFromFilePath(
                source.path
            )
        );

        // inherit the import flags so the rebuilt file keeps the behaviour the original was authored
        // with, only the lod flag follows the checkbox
        baked.SetFlags(source.mesh->GetFlags());
        baked.SetFlag(
            static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods),
            m_working_generate_lods
        );

        // sub mesh order has to survive the bake, the prefab and the materials reference sub meshes by
        // index, so slots are reserved up front and written in place
        baked.ReserveSubMeshes(source.mesh->GetSubMeshCount());
        uint32_t written = 0;
        for (const WorkingSubMesh& working : m_working_sub_meshes)
        {
            if (
                working.source_mesh != mesh_index ||
                working.source_sub_mesh >= source.mesh->GetSubMeshCount()
            )
            {
                continue;
            }

            vector<RHI_Vertex_PosTexNorTan> vertices = working.vertices;
            vector<uint32_t> indices = working.indices;
            baked.AddGeometry(
                vertices,
                indices,
                m_working_generate_lods,
                working.source_sub_mesh
            );
            written++;
        }
        if (written == 0)
        {
            continue;
        }

        baked.SaveToFile(source.path);
        if (!FileSystem::Exists(source.path))
        {
            m_status =
                "Failed to write " +
                source.path;
            return false;
        }
        files_written++;
        written_path = source.path;
    }

    const uint64_t vertex_count = m_working_vertices.size();
    m_working_modified = false;
    m_loaded_write_time.clear();
    if (m_selected_asset >= 0)
    {
        LoadSelectedAsset(false, true);
    }
    else
    {
        const string path = m_loaded_path;
        LoadDependencyPreview(path);
    }
    m_status =
        "Saved " +
        (
            files_written == 1
                ? FileSystem::GetFileNameFromFilePath(written_path)
                : to_string(files_written) + " mesh files"
        ) +
        " with " +
        compact_count(vertex_count) +
        " vertices";
    return true;
}

bool AssetViewer::UpdateCatalogAsset(
    const string& asset_id,
    const function<void(JsonValue&)>& edit
)
{
    if (m_catalog_path.empty())
    {
        m_status = "No asset catalog to write to.";
        return false;
    }

    const string catalog_write_time =
        FileSystem::GetLastWriteTime(m_catalog_path);
    string source;
    if (!FileSystem::ReadFile(m_catalog_path, source))
    {
        m_status = "The asset catalog could not be read.";
        return false;
    }

    JsonValue root;
    string parse_error;
    if (!mcp_json::parse(source, root, parse_error))
    {
        m_status = "The asset catalog is invalid: " + parse_error;
        return false;
    }

    JsonValue* assets = json_object_find(root, "assets");
    if (
        root.type != mcp_json::kind::object ||
        !assets ||
        assets->type != mcp_json::kind::object
    )
    {
        m_status = "The asset catalog has no assets object.";
        return false;
    }

    JsonValue* asset_value = json_object_find(*assets, asset_id);
    if (!asset_value)
    {
        m_status = "The asset is no longer in the catalog.";
        return false;
    }
    edit(*asset_value);

    // written beside the catalog and swapped in, a crash mid write must not leave half a catalog
    const string temporary_path = m_catalog_path + ".update.tmp";
    const string backup_path = m_catalog_path + ".update.backup";
    FileSystem::Delete(temporary_path);
    FileSystem::Delete(backup_path);
    if (
        !FileSystem::WriteFile(
            temporary_path,
            serialize_json(root) + "\n"
        )
    )
    {
        m_status = "The updated asset catalog could not be written.";
        return false;
    }
    if (
        FileSystem::GetLastWriteTime(m_catalog_path) !=
        catalog_write_time
    )
    {
        FileSystem::Delete(temporary_path);
        m_status = "The catalog changed during the update, try again.";
        return false;
    }

    error_code error;
    filesystem::rename(m_catalog_path, backup_path, error);
    if (error)
    {
        FileSystem::Delete(temporary_path);
        m_status = "The asset catalog could not be backed up.";
        return false;
    }
    filesystem::rename(temporary_path, m_catalog_path, error);
    if (error)
    {
        filesystem::rename(backup_path, m_catalog_path, error);
        FileSystem::Delete(temporary_path);
        m_status = "The asset catalog could not be replaced.";
        return false;
    }
    FileSystem::Delete(backup_path);
    return true;
}

vector<Material*> AssetViewer::PreviewMaterials() const
{
    vector<Material*> materials;
    if (m_material)
    {
        materials.push_back(m_material.get());
        return materials;
    }

    vector<Entity*> entities;
    CollectPreviewEntities(entities);
    for (Entity* entity : entities)
    {
        Render* render =
            entity ?
            entity->GetComponent<Render>() :
            nullptr;
        Material* material =
            render ? render->GetMaterial() : nullptr;
        if (
            !material ||
            find(
                materials.begin(),
                materials.end(),
                material
            ) != materials.end()
        )
        {
            continue;
        }
        materials.push_back(material);
    }
    return materials;
}

AssetViewer::BakeSummary AssetViewer::PreviewBakeSummary() const
{
    BakeSummary summary;
    vector<Entity*> entities;
    CollectPreviewEntities(entities);

    // mirrors what game_ready groups on closely enough to promise a number, the merge itself is
    // the one that decides and its report is what the status line quotes afterwards
    vector<pair<Material*, uint32_t>> groups;
    for (Entity* entity : entities)
    {
        Render* render =
            entity ?
            entity->GetComponent<Render>() :
            nullptr;
        if (
            !render ||
            !render->GetMesh() ||
            render->GetIndexCount() == 0
        )
        {
            continue;
        }

        summary.renderers_before++;
        Material* material = render->GetMaterial();
        if (
            !material ||
            render->GetMesh()->IsSkinned() ||
            render->HasInstancing() ||
            entity->GetComponentCount() > 1
        )
        {
            summary.skipped++;
            summary.renderers_after++;
            continue;
        }

        const auto group = find_if(
            groups.begin(),
            groups.end(),
            [material](const pair<Material*, uint32_t>& candidate)
            {
                return candidate.first == material;
            }
        );
        if (group == groups.end())
        {
            groups.emplace_back(material, 1u);
        }
        else
        {
            group->second++;
        }
    }

    summary.materials = static_cast<uint32_t>(groups.size());
    summary.renderers_after += summary.materials;
    return summary;
}

bool AssetViewer::BakePrefabByMaterial(const bool generate_lods)
{
    if (
        m_selected_asset < 0 ||
        m_selected_asset >= static_cast<int>(m_assets.size()) ||
        m_assets[m_selected_asset].type != "prefab"
    )
    {
        m_status = "Select a prefab to bake.";
        return false;
    }
    if (m_working_modified || m_working_lods_built)
    {
        m_status =
            "Save or revert the mesh changes before baking.";
        return false;
    }

    // copied, the catalog refresh at the end rebuilds m_assets under the reference
    const AssetEntry asset = m_assets[m_selected_asset];
    const string prefab_path = asset.path;
    if (!FileSystem::Exists(prefab_path))
    {
        m_status = "Prefab file not found: " + prefab_path;
        return false;
    }

    // the merged mesh lives with the prefab's other files, next to the first mesh it already has
    // or in its own dependency folder when it has none yet
    string directory;
    for (const string& dependency : asset.dependencies)
    {
        if (asset_type_from_path(dependency) == "mesh")
        {
            directory =
                FileSystem::GetDirectoryFromFilePath(dependency);
            break;
        }
    }
    if (directory.empty())
    {
        const filesystem::path prefab_directory =
            filesystem::path(
                FileSystem::GetDirectoryFromFilePath(prefab_path)
            ).parent_path();
        directory =
            (
                prefab_directory.parent_path() /
                "dependencies" /
                asset.id
            ).generic_string() + "/";
    }
    const string mesh_path =
        directory +
        sanitize_asset_name(asset.id) +
        "_merged_" +
        short_hash(prefab_path) +
        ".mesh";

    pugi::xml_document document;
    document.load_file(prefab_path.c_str());
    const string prefab_name =
        document.child("Prefab").attribute("name").as_string(
            asset.name.c_str()
        );

    // the bake runs on a private copy of the hierarchy so the rig never ends up inside the saved
    // file, the preview is torn down first because both copies would carry the prefab's entity ids
    DestroyPreviewScene();
    Entity* root = World::CreateEntity();
    if (!root)
    {
        m_status = "The bake could not create a working entity.";
        return false;
    }
    root->SetObjectName(prefab_name);
    root->SetTransient(true);
    if (!Prefab::LoadFromFile(prefab_path, root))
    {
        World::RemoveEntityImmediate(root);
        m_status = "Prefab could not be loaded for baking.";
        return false;
    }
    World::ProcessPendingAdditions();

    const game_ready::MergeReport report =
        game_ready::MergeRenderersByMaterial(
            root,
            mesh_path,
            generate_lods
        );
    if (!report.ok)
    {
        World::RemoveEntityImmediate(root);
        m_status = "Bake failed: " + report.error;
        return false;
    }
    if (report.groups.empty())
    {
        World::RemoveEntityImmediate(root);
        m_status =
            "Nothing to bake, every material already draws once.";
        return true;
    }

    const bool saved = Prefab::SaveToFile(root, prefab_path);
    World::RemoveEntityImmediate(root);
    if (!saved)
    {
        m_status = "The baked prefab could not be written.";
        return false;
    }

    // the catalog lists what the prefab depends on, the parts that were folded away are gone from
    // the file so their mesh entries go too and the merged mesh takes their place
    const vector<string> mesh_references =
        collect_xml_references(prefab_path, "mesh_path");
    const bool catalog_updated =
        asset.disk_only ||
        UpdateCatalogAsset(
            asset.id,
            [&](JsonValue& record)
            {
                JsonValue& dependencies =
                    json_object_ensure(record, "dependencies");
                vector<string> kept;
                for (const string& dependency : json_strings(&dependencies))
                {
                    if (asset_type_from_path(dependency) != "mesh")
                    {
                        kept.push_back(dependency);
                    }
                }
                for (const string& reference : mesh_references)
                {
                    if (
                        find(kept.begin(), kept.end(), reference) ==
                        kept.end()
                    )
                    {
                        kept.push_back(reference);
                    }
                }
                dependencies.type = mcp_json::kind::array;
                dependencies.array_items.clear();
                for (const string& dependency : kept)
                {
                    JsonValue item;
                    item.type = mcp_json::kind::string;
                    item.string_value = dependency;
                    dependencies.array_items.push_back(move(item));
                }
            }
        );

    const string status =
        "Baked " +
        to_string(report.renderers_before) +
        " parts into " +
        to_string(report.renderers_after) +
        " draw calls across " +
        to_string(report.groups.size()) +
        (report.groups.size() == 1 ? " material" : " materials") +
        (
            report.skipped.empty()
                ? ""
                : ", " + to_string(report.skipped.size()) + " left alone"
        ) +
        (
            catalog_updated
                ? ""
                : ", the catalog could not be updated"
        );

    ClearLoadedAsset();
    RefreshCatalog(true);
    for (int index = 0; index < static_cast<int>(m_assets.size()); index++)
    {
        if (m_assets[index].id == asset.id)
        {
            m_selected_asset = index;
            m_selected_dependency_path.clear();
            LoadSelectedAsset(false, true);
            break;
        }
    }
    m_status = status;
    return true;
}

Entity* AssetViewer::PreviewRoot() const
{
    if (m_preview_root_id == 0)
    {
        return nullptr;
    }

    return World::GetEntityById(m_preview_root_id);
}

void AssetViewer::CollectPreviewEntities(
    vector<Entity*>& entities
) const
{
    Entity* root = PreviewRoot();
    if (!root)
    {
        return;
    }

    entities.push_back(root);
    root->GetDescendants(&entities);
}

void AssetViewer::DestroyPreviewScene()
{
    Renderer::InvalidateSecondaryView();
    m_preview_render_slots.clear();
    if (
        !m_preview_root_owned &&
        m_preview_rig_id != 0
    )
    {
        if (
            Entity* rig =
                World::GetEntityById(m_preview_rig_id)
        )
        {
            World::RemoveEntityImmediate(rig);
        }
    }
    if (Entity* root = PreviewRoot())
    {
        if (m_preview_root_owned)
        {
            World::RemoveEntityImmediate(root);
        }
    }
    m_preview_root_id = 0;
    m_preview_rig_id = 0;
    m_preview_camera_id = 0;
    m_preview_root_owned = false;
    m_preview_dirty = false;
    m_preview_settle_frames = 0;
    m_preview_signature = 0;
    m_preview_orbiting = false;
}

void AssetViewer::RebuildPreviewScene()
{
    DestroyPreviewScene();
    if (m_loaded_path.empty() || m_texture)
    {
        return;
    }

    Entity* root = World::CreateEntity();
    if (!root)
    {
        return;
    }

    m_preview_root_id = root->GetObjectId();
    m_preview_root_owned = true;
    root->SetObjectName(
        "asset_viewer_preview"
    );
    root->SetTransient(true);

    if (m_mesh)
    {
        // always built against the source so the sub mesh count and the material slots are the real
        // ones, the scratch meshes are swapped in afterwards
        Mesh* mesh = m_mesh.get();

        // a mesh file holds one sub mesh per material slot, a single render
        // component only ever draws the first one
        const uint32_t sub_mesh_count =
            max(1u, mesh->GetSubMeshCount());
        if (sub_mesh_count == 1)
        {
            Render* render = root->AddComponent<Render>();
            render->SetMesh(mesh, 0);
            render->SetDefaultMaterial();
        }
        else
        {
            for (uint32_t i = 0; i < sub_mesh_count; i++)
            {
                Entity* part = World::CreateEntity();
                part->SetObjectName(
                    "asset_viewer_sub_mesh_" + to_string(i)
                );
                part->SetTransient(true);
                part->SetParent(root);
                Render* render = part->AddComponent<Render>();
                render->SetMesh(mesh, i);
                render->SetDefaultMaterial();
            }
        }

        // the scratch meshes hold the edit in progress, the source is only the layout donor
        CollectPreviewRenderSlots(root);
        RefreshPreviewMeshGeometry();
    }
    else if (m_material)
    {
        Render* render = root->AddComponent<Render>();
        render->SetMesh(MeshType::Sphere);
        render->SetMaterial(m_material);
    }
    else if (
        !Prefab::LoadFromFile(
            m_loaded_path,
            root
        )
    )
    {
        m_status =
            "Prefab could not be loaded into the preview.";
        DestroyPreviewScene();
        return;
    }
    else
    {
        // the prefab's own parts are what the optimize tools preview through, so an edit to a leg
        // shows on the leg rather than on a detached copy of the mesh
        CollectPreviewRenderSlots(root);
        RefreshPreviewMeshGeometry();
    }

    root->SetPosition(math::Vector3::Zero);
    CreatePreviewRig(root);
    root->SetActive(false);
    m_preview_dirty = true;
}

void AssetViewer::CreatePreviewRig(Entity* root)
{
    if (!root)
    {
        return;
    }

    root->SetActive(true);
    RefreshPreviewBounds(root);

    Entity* rig_entity = World::CreateEntity();
    rig_entity->SetObjectName("asset_viewer_rig");
    rig_entity->SetTransient(true);
    rig_entity->SetParent(root);
    m_preview_rig_id = rig_entity->GetObjectId();

    Entity* camera_entity = World::CreateEntity();
    camera_entity->SetObjectName("asset_viewer_camera");
    camera_entity->SetTransient(true);
    camera_entity->SetParent(rig_entity);
    Camera* camera = camera_entity->AddComponent<Camera>();
    camera->SetFovHorizontalDeg(50.0f);
    camera->SetExposureMode(
        CameraExposureMode::automatic
    );
    m_preview_camera_id = camera_entity->GetObjectId();

    Entity* key_entity = World::CreateEntity();
    key_entity->SetObjectName("asset_viewer_key_light");
    key_entity->SetTransient(true);
    key_entity->SetParent(rig_entity);
    key_entity->SetRotation(
        math::Quaternion::FromLookRotation(
            math::Vector3(-0.45f, -0.8f, 0.65f),
            math::Vector3::Up
        )
    );
    Light* key_light = key_entity->AddComponent<Light>();
    key_light->SetLightType(LightType::Directional);
    key_light->SetIntensity(85000.0f);
    key_light->SetColor(Color::standard_white);

    Entity* fill_entity = World::CreateEntity();
    fill_entity->SetObjectName("asset_viewer_fill_light");
    fill_entity->SetTransient(true);
    fill_entity->SetParent(rig_entity);
    fill_entity->SetPosition(
        m_preview_center +
        math::Vector3(
            m_preview_radius * 2.0f,
            m_preview_radius * 1.2f,
            -m_preview_radius * 1.5f
        )
    );
    Light* fill_light = fill_entity->AddComponent<Light>();
    fill_light->SetLightType(LightType::Point);
    fill_light->SetIntensity(5000.0f);
    fill_light->SetRange(m_preview_radius * 8.0f);
    fill_light->SetColor(Color::standard_white);

    UpdatePreviewCamera();

    // the renderer walks the live entity lists, a freshly created entity only lands there on the next
    // world tick, so without this drain the first preview frame draws whichever parts happened to be
    // committed already and the rig lights are not in it at all
    World::ProcessPendingAdditions();
    m_preview_settle_frames = 3;
}

// an asset being authored gains parts while it is being previewed, so the framing cannot be decided once
// when the preview opens, a chair that grows a backrest after the camera was fitted to its base would put
// the backrest off screen with nothing to bring it back
void AssetViewer::RefreshPreviewBounds(Entity* root)
{
    // the rig is built before the world commits the entities it just created, so a lookup by id would come
    // back empty there, the caller that already holds the root passes it
    vector<Entity*> render_entities;
    if (root)
    {
        render_entities.push_back(root);
        root->GetDescendants(&render_entities);
    }
    else
    {
        CollectPreviewEntities(render_entities);
    }
    bool bounds_found = false;
    math::BoundingBox bounds;
    for (Entity* entity : render_entities)
    {
        Render* render =
            entity ?
            entity->GetComponent<Render>() :
            nullptr;
        if (!render || !render->GetMesh())
        {
            continue;
        }
        const math::BoundingBox render_bounds =
            render->GetBoundingBoxMesh() *
            entity->GetMatrix();
        if (!bounds_found)
        {
            bounds = render_bounds;
            bounds_found = true;
        }
        else
        {
            bounds.Merge(render_bounds);
        }
    }

    m_preview_center = bounds_found
        ? bounds.GetCenter()
        : math::Vector3::Zero;
    if (bounds_found)
    {
        // half the diagonal is the enclosing sphere, the longest axis alone
        // under estimates it and crops elongated assets
        const math::Vector3 size = bounds.GetSize();
        m_preview_radius = max(
            0.001f,
            size.Length() * 0.5f
        );
    }
    else
    {
        m_preview_radius = 1.0f;
    }
}

void AssetViewer::UpdatePreviewCamera()
{
    Entity* camera_entity =
        World::GetEntityById(m_preview_camera_id);
    if (!camera_entity)
    {
        return;
    }

    const math::Vector3 direction(
        cos(m_preview_pitch) * sin(m_preview_yaw),
        sin(m_preview_pitch),
        -cos(m_preview_pitch) * cos(m_preview_yaw)
    );

    Camera* camera = camera_entity->GetComponent<Camera>();
    const float fov_horizontal =
        camera
            ? camera->GetFovHorizontalRad()
            : 50.0f * math::deg_to_rad;
    const float aspect = max(0.05f, m_preview_aspect);
    const float fov_vertical =
        2.0f *
        atan(
            tan(fov_horizontal * 0.5f) /
            aspect
        );

    // fit the bounding sphere against the tighter frustum axis, otherwise a
    // wide or tall panel crops the asset
    const float fov_fit = min(fov_horizontal, fov_vertical);
    float distance =
        m_preview_radius *
        1.15f /
        max(0.01f, sin(fov_fit * 0.5f));
    distance /= max(0.05f, m_preview_zoom);

    // the near plane is fixed engine side, keep small assets in front of it
    const float near_plane =
        camera ? camera->GetNearPlane() : 0.1f;
    distance = max(
        distance,
        near_plane + m_preview_radius * 1.05f
    );

    const math::Vector3 position =
        m_preview_center + direction * distance;
    camera_entity->SetPosition(position);
    camera_entity->SetRotation(
        math::Quaternion::FromLookRotation(
            (m_preview_center - position).Normalized(),
            math::Vector3::Up
        )
    );
}

bool AssetViewer::RequestPreviewRender(
    const uint32_t width,
    const uint32_t height
)
{
    Entity* root = PreviewRoot();
    Entity* camera =
        World::GetEntityById(m_preview_camera_id);
    if (!root || !camera)
    {
        return false;
    }

    m_preview_aspect =
        static_cast<float>(width) /
        static_cast<float>(max(1u, height));
    UpdatePreviewCamera();
    const Renderer_SecondaryViewMode mode =
        m_preview_mode == 1
            ? Renderer_SecondaryViewMode::Wireframe
            : m_preview_mode == 2
                ? Renderer_SecondaryViewMode::Vertices
                : Renderer_SecondaryViewMode::Solid;
    if (
        Renderer::RequestSecondaryView(
            camera,
            root,
            width,
            height,
            mode,
            ResolvePreviewBackdrop()
        )
    )
    {
        m_preview_dirty = false;
        m_next_preview_request =
            chrono::steady_clock::now() +
            chrono::milliseconds(16);
        return true;
    }
    return false;
}

Renderer_SecondaryViewBackdrop
AssetViewer::ResolvePreviewBackdrop() const
{
    // auto keeps the sky for solid shading since it is the nicest looking backdrop, then swaps to
    // charcoal for wire and vertex modes where a bright sky swallows the wires whole
    if (m_preview_backdrop == 0)
    {
        return m_preview_mode == 0
            ? Renderer_SecondaryViewBackdrop::Sky
            : Renderer_SecondaryViewBackdrop::Charcoal;
    }

    switch (m_preview_backdrop)
    {
        case 1:
            return Renderer_SecondaryViewBackdrop::Sky;
        case 2:
            return Renderer_SecondaryViewBackdrop::Charcoal;
        case 3:
            return Renderer_SecondaryViewBackdrop::Slate;
        default:
            return Renderer_SecondaryViewBackdrop::Paper;
    }
}

void AssetViewer::PreviewEntity(Entity* entity)
{
    if (!entity)
    {
        return;
    }

    DestroyPreviewScene();
    m_mesh.reset();
    m_working_meshes.clear();
    m_working_sub_meshes.clear();
    m_working_lods.clear();
    m_working_vertices.clear();
    m_working_indices.clear();
    m_working_editable = false;
    m_working_modified = false;
    m_preview_meshes.clear();
    m_preview_meshes_sources.clear();
    m_material.reset();
    m_texture.reset();
    m_loaded_path.clear();
    m_loaded_write_time.clear();
    m_selected_asset = -1;
    m_preview_root_id = entity->GetObjectId();
    m_preview_root_owned = false;
    entity->SetTransient(true);
    entity->SetActive(false);
    if (!entity->GetParent())
    {
        entity->SetPosition(
            math::Vector3::Zero
        );
    }
    CreatePreviewRig(entity);
    entity->SetActive(false);
    m_visible = true;
    m_preview_yaw = 0.65f;
    m_preview_pitch = 0.35f;
    m_preview_zoom = 1.0f;
    m_texture_pan = math::Vector2::Zero;
    m_status =
        "Previewing focused asset workspace";
    m_preview_dirty = true;
}

pair<uint64_t, uint64_t>
AssetViewer::GetPreviewGeometryCounts() const
{
    // the working copy is lod 0 of exactly what is being edited, the mesh totals below include every
    // lod level and count a mesh once per part that draws it
    if (
        !m_working_vertices.empty() &&
        m_working_indices.size() >= 3
    )
    {
        return
        {
            static_cast<uint64_t>(
                m_working_vertices.size()
            ),
            static_cast<uint64_t>(
                m_working_indices.size()
            )
        };
    }
    if (m_mesh)
    {
        return
        {
            static_cast<uint64_t>(
                m_mesh->GetVertices().size()
            ),
            static_cast<uint64_t>(
                m_mesh->GetIndices().size()
            )
        };
    }
    if (m_material)
    {
        return { 0, 0 };
    }

    vector<Entity*> entities;
    CollectPreviewEntities(entities);
    uint64_t vertex_count = 0;
    uint64_t index_count = 0;
    for (Entity* entity : entities)
    {
        Render* render =
            entity ?
            entity->GetComponent<Render>() :
            nullptr;
        Mesh* mesh =
            render ?
            render->GetMesh() :
            nullptr;
        if (!mesh)
        {
            continue;
        }
        vertex_count += static_cast<uint64_t>(
            render->GetVertexCount()
        );
        index_count += static_cast<uint64_t>(
            render->GetIndexCount()
        );
    }
    return
    {
        vertex_count,
        index_count
    };
}

uint64_t AssetViewer::PreviewSceneSignature() const
{
    if (m_texture)
    {
        return 0;
    }

    vector<Entity*> entities;
    CollectPreviewEntities(entities);
    if (entities.empty())
    {
        return 0;
    }

    uint64_t signature = 1469598103934665603ull;
    const auto mix =
        [&signature](const uint64_t value)
        {
            signature =
                (signature ^ value) * 1099511628211ull;
        };

    const auto mix_float =
        [&mix](const float value)
        {
            // a raw float bit pattern, so a zero and a negative zero do not read as different values
            mix(
                static_cast<uint64_t>(
                    std::hash<float>{}(value == 0.0f ? 0.0f : value)
                )
            );
        };

    mix(entities.size());
    for (Entity* entity : entities)
    {
        if (!entity)
        {
            continue;
        }

        Render* render = entity->GetComponent<Render>();
        if (!render)
        {
            continue;
        }

        // a draw is dropped when either the mesh or the material is still missing, so both have to be
        // part of the fingerprint, the glass of a bottle goes missing exactly this way
        Mesh* mesh         = render->GetMesh();
        Material* material = render->GetMaterial();
        mix(entity->GetObjectId());
        mix(reinterpret_cast<uint64_t>(mesh));
        mix(reinterpret_cast<uint64_t>(material));
        if (mesh)
        {
            mix(mesh->GetIndices().size());
            mix(mesh->GetVertices().size());
        }

        // a part being moved, turned or resized changes nothing about which resources are bound, so the
        // pointers alone cannot see it, the placement has to be in the fingerprint too. the local
        // matrix, not the world one, so orbiting the camera rig does not read as the asset changing
        const math::Matrix& transform = entity->GetLocalMatrix();
        for (uint32_t element = 0; element < 16; element++)
        {
            mix_float(transform.Data()[element]);
        }

        // a material is usually retuned in place rather than replaced, so its colour, roughness, metal
        // and texture bindings move without the pointer ever changing, this is what makes a recolour
        // or a newly finished texture upload show up on its own
        if (material)
        {
            mix(static_cast<uint64_t>(material->GetResourceState()));
            for (const float property : material->GetProperties())
            {
                mix_float(property);
            }
            for (const RHI_Texture* texture : material->GetTextures())
            {
                mix(reinterpret_cast<uint64_t>(texture));
                if (texture)
                {
                    mix(static_cast<uint64_t>(texture->GetResourceState()));
                }
            }
        }
    }
    return signature;
}

void AssetViewer::DrawLibraryToolbar(const float width)
{
    const float scale = ui_scale();
    ImGui::PushStyleVar(
        ImGuiStyleVar_FrameRounding,
        5.0f * scale
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(9.0f * scale, 5.0f * scale)
    );

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SetNextItemShortcut(
        ImGuiMod_Ctrl | ImGuiKey_F,
        ImGuiInputFlags_Tooltip
    );
    ImGui::InputTextWithHint(
        "##asset_viewer_search",
        "Search assets, tags and aliases",
        m_search.data(),
        m_search.size(),
        ImGuiInputTextFlags_EscapeClearsAll
    );

    const char* filters =
        "All types\0"
        "Meshes\0"
        "Materials\0"
        "Prefabs\0"
        "Textures\0";
    const float action_width = 28.0f * scale;
    const float gap = 4.0f * scale;
    const float combo_width = max(
        76.0f * scale,
        (
            width -
            action_width -
            gap * 4.0f -
            16.0f * scale
        ) *
        0.52f
    );
    ImGui::SetNextItemWidth(combo_width);
    ImGui::Combo(
        "##asset_type_filter",
        &m_type_filter,
        filters
    );
    ImGuiSp::tooltip("Filter by asset type");
    ImGui::SameLine(0.0f, gap);

    const char* sort_labels[] =
    {
        "Name",
        "Quality",
        "Type"
    };
    ImGui::SetNextItemWidth(
        max(
            70.0f * scale,
            width -
                combo_width -
                action_width -
                gap * 4.0f -
                16.0f * scale
        )
    );
    if (
        ImGui::Combo(
            "##asset_sort",
            &m_sort_mode,
            sort_labels,
            IM_ARRAYSIZE(sort_labels)
        )
    )
    {
        const string selected_id =
            (
                m_selected_asset >= 0 &&
                m_selected_asset < static_cast<int>(m_assets.size())
            )
                ? m_assets[m_selected_asset].id
                : "";
        sort(
            m_assets.begin(),
            m_assets.end(),
            [this](
                const AssetEntry& first,
                const AssetEntry& second
            )
            {
                if (m_sort_mode == 1)
                {
                    const float first_quality =
                        first.quality_score;
                    const float second_quality =
                        second.quality_score;
                    if (first_quality != second_quality)
                    {
                        return first_quality > second_quality;
                    }
                }
                else if (
                    m_sort_mode == 2 &&
                    first.type != second.type
                )
                {
                    return first.type < second.type;
                }
                return lower_copy(first.name) <
                    lower_copy(second.name);
            }
        );
        m_selected_asset = -1;
        for (
            int index = 0;
            index < static_cast<int>(m_assets.size());
            index++
        )
        {
            if (m_assets[index].id == selected_id)
            {
                m_selected_asset = index;
                break;
            }
        }
    }
    ImGuiSp::tooltip("Sort asset library");

    ImGui::SameLine(0.0f, gap);
    if (
        ImGuiSp::image_button(
            IconType::Refresh,
            math::Vector2(17.0f * scale, 17.0f * scale),
            false
        )
    )
    {
        RefreshCatalog(true);
    }
    ImGuiSp::tooltip("Refresh catalog");

    if (ImGui::Button("Library actions", ImVec2(-1.0f, 0.0f)))
    {
        ImGui::OpenPopup("##asset_library_actions");
    }
    if (ImGui::BeginPopup("##asset_library_actions"))
    {
        if (ImGui::MenuItem("Clean up library"))
        {
            ScanLibraryCleanup();
        }
        ImGui::Separator();
        ImGui::TextDisabled(
            "Removes files not referenced by the catalog or worlds"
        );
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

void AssetViewer::DrawSelectionBar()
{
    if (m_selected_assets.size() <= 1)
    {
        return;
    }

    const float scale = ui_scale();
    const ImVec4 accent = ImGui::Style::color_accent_1;
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImVec4(accent.x, accent.y, accent.z, 0.1f)
    );
    ImGui::BeginChild(
        "##asset_selection_actions",
        ImVec2(0.0f, 42.0f * scale),
        ImGuiChildFlags_Borders
    );
    ImGui::SetCursorPos(
        ImVec2(8.0f * scale, 8.0f * scale)
    );
    ImGui::Text(
        "%zu selected",
        m_selected_assets.size()
    );

    const bool has_focus =
        m_selected_asset >= 0 &&
        m_selected_asset < static_cast<int>(m_assets.size()) &&
        m_selected_assets.find(
            m_assets[m_selected_asset].id
        ) != m_selected_assets.end();
    const float actions_width = 112.0f * scale;
    const float right =
        ImGui::GetCursorPosX() +
        ImGui::GetContentRegionAvail().x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(
        max(
            ImGui::GetCursorPosX(),
            right - actions_width
        )
    );
    if (!has_focus)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::SmallButton("Focus only"))
    {
        m_selected_assets.clear();
        m_selected_assets.insert(
            m_assets[m_selected_asset].id
        );
        m_status = "Focused selection only";
    }
    if (!has_focus)
    {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete"))
    {
        m_pending_delete_selection = true;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void AssetViewer::DrawStatusBar()
{
    ImGui::Separator();
    const float scale = ui_scale();
    int visible_count = 0;
    for (const AssetEntry& asset : m_assets)
    {
        if (AssetMatchesFilter(asset))
        {
            visible_count++;
        }
    }
    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() +
        2.0f * scale
    );
    ImGui::TextDisabled(
        "%d / %zu assets",
        visible_count,
        m_assets.size()
    );
    if (!m_catalog_path.empty())
    {
        ImGuiSp::tooltip(m_catalog_path.c_str());
    }
    if (m_selected_assets.size() > 1)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(
            "|  %zu selected",
            m_selected_assets.size()
        );
    }
    if (!m_status.empty())
    {
        const string status_lower = lower_copy(m_status);
        ImVec4 color = ImGui::Style::color_info;
        if (
            status_lower.find("fail") != string::npos ||
            status_lower.find("error") != string::npos ||
            status_lower.find("unsafe") != string::npos ||
            status_lower.find("not found") != string::npos ||
            status_lower.find("could not") != string::npos
        )
        {
            color = ImGui::Style::color_error;
        }
        else if (
            status_lower.find("missing") != string::npos ||
            status_lower.find("warning") != string::npos
        )
        {
            color = ImGui::Style::color_warning;
        }

        const float status_width =
            ImGui::CalcTextSize(m_status.c_str()).x;
        const float right =
            ImGui::GetCursorPosX() +
            ImGui::GetContentRegionAvail().x;
        ImGui::SameLine();
        if (
            ImGui::GetCursorPosX() +
                status_width <
            right
        )
        {
            ImGui::SetCursorPosX(
                right - status_width
            );
        }
        ImGui::TextColored(
            color,
            "%s",
            m_status.c_str()
        );
    }
}

void AssetViewer::DrawAssetList(float width, float height)
{
    ImGui::BeginChild(
        "##asset_viewer_list",
        ImVec2(width, height),
        ImGuiChildFlags_Borders
    );

    const float scale = ui_scale();
    const ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(
        ImVec2(
            cursor.x + 5.0f * scale,
            cursor.y + 4.0f * scale
        )
    );
    ImGui::TextUnformatted("LIBRARY");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu assets", m_assets.size());
    ImGui::Spacing();
    DrawLibraryToolbar(width);
    ImGui::Spacing();
    vector<vector<int>> prefab_children(m_assets.size());
    vector<vector<string>> direct_dependencies(m_assets.size());
    vector<bool> nested_assets(m_assets.size(), false);
    for (
        int prefab_index = 0;
        prefab_index < static_cast<int>(m_assets.size());
        prefab_index++
    )
    {
        const AssetEntry& prefab = m_assets[prefab_index];
        if (prefab.type != "prefab")
        {
            continue;
        }

        if (prefab.path.empty())
        {
            continue;
        }
        for (const string& dependency : prefab.dependencies)
        {
            const string dependency_name = lower_copy(
                FileSystem::GetFileNameWithoutExtensionFromFilePath(
                    dependency
                )
            );
            int matched_index = -1;
            // path first, a mesh and its material share a stem so name matching alone
            // would fold two different assets into one child
            const string dependency_path =
                normalized_path(dependency);
            for (
                int index = 0;
                index < static_cast<int>(m_assets.size());
                index++
            )
            {
                if (
                    index == prefab_index ||
                    m_assets[index].type == "prefab"
                )
                {
                    continue;
                }
                if (
                    !m_assets[index].path.empty() &&
                    normalized_path(m_assets[index].path) ==
                        dependency_path
                )
                {
                    matched_index = index;
                    break;
                }
            }
            for (
                int index = 0;
                matched_index < 0 &&
                index < static_cast<int>(m_assets.size());
                index++
            )
            {
                if (
                    index == prefab_index ||
                    m_assets[index].type == "prefab"
                )
                {
                    continue;
                }
                const string child_file =
                    !m_assets[index].path.empty()
                    ? lower_copy(
                        FileSystem::
                            GetFileNameWithoutExtensionFromFilePath(
                                m_assets[index].path
                            )
                    )
                    : "";
                if (
                    dependency_name ==
                        lower_copy(m_assets[index].id) ||
                    dependency_name ==
                        lower_copy(m_assets[index].name) ||
                    dependency_name == child_file
                )
                {
                    matched_index = index;
                    break;
                }
            }

            if (matched_index >= 0)
            {
                if (
                    find(
                        prefab_children[prefab_index].begin(),
                        prefab_children[prefab_index].end(),
                        matched_index
                    ) == prefab_children[prefab_index].end()
                )
                {
                    prefab_children[prefab_index].push_back(
                        matched_index
                    );
                    nested_assets[matched_index] = true;
                }
            }
            else
            {
                direct_dependencies[prefab_index].push_back(
                    dependency
                );
            }
        }
    }

    const auto dependency_matches_filter =
        [this](const string& path)
        {
            const string type = asset_type_from_path(path);
            if (
                (m_type_filter == 1 && type != "mesh") ||
                (m_type_filter == 2 && type != "material") ||
                (m_type_filter == 3 && type != "prefab") ||
                (m_type_filter == 4 && type != "texture")
            )
            {
                return false;
            }

            const string query = lower_copy(m_search.data());
            if (
                query.find("packed") == string::npos &&
                is_packed_texture(path)
            )
            {
                return false;
            }
            if (query.empty())
            {
                return true;
            }
            const string searchable =
                lower_copy(path + " " + type);
            istringstream stream(query);
            string term;
            while (stream >> term)
            {
                if (searchable.find(term) == string::npos)
                {
                    return false;
                }
            }
            return true;
        };

    vector<int> visible_roots;
    visible_roots.reserve(m_assets.size());
    for (
        int index = 0;
        index < static_cast<int>(m_assets.size());
        index++
    )
    {
        const AssetEntry& asset = m_assets[index];
        if (asset.type != "prefab" && nested_assets[index])
        {
            continue;
        }

        bool visible = AssetMatchesFilter(asset);
        if (asset.type == "prefab")
        {
            for (const int child_index : prefab_children[index])
            {
                visible |= AssetMatchesFilter(
                    m_assets[child_index]
                );
            }
            for (
                const string& dependency :
                direct_dependencies[index]
            )
            {
                visible |= dependency_matches_filter(dependency);
            }
        }
        if (visible)
        {
            visible_roots.push_back(index);
        }
    }

    ImGui::TextDisabled(
        "%zu shown",
        visible_roots.size()
    );
    ImGui::Separator();
    DrawSelectionBar();

    // rebuilt every frame in draw order, select all and shift ranges both operate on what is on screen
    // rather than on m_assets, which is sorted differently and includes collapsed and filtered rows
    vector<int> drawn_rows;
    drawn_rows.reserve(visible_roots.size());

    const auto select_asset =
        [this, &drawn_rows](const int index)
        {
            ApplyRowSelection(index, drawn_rows);
        };
    const auto draw_asset_row =
        [this, scale, &select_asset, &drawn_rows](
            const int index,
            const bool nested,
            const int child_count
        )
        {
            drawn_rows.push_back(index);
            const AssetEntry& asset = m_assets[index];
            const float row_height =
                (nested ? 44.0f : 58.0f) * scale;
            const bool has_children = child_count > 0;

            const bool renaming = m_rename_asset_id == asset.id;

            ImGui::PushID(asset.id.c_str());
            const ImVec2 row_start =
                ImGui::GetCursorScreenPos();
            const float row_width =
                max(
                    1.0f,
                    ImGui::GetContentRegionAvail().x
                );
            ImGui::InvisibleButton(
                "##asset_row",
                ImVec2(row_width, row_height),
                ImGuiButtonFlags_MouseButtonLeft |
                ImGuiButtonFlags_MouseButtonRight
            );
            const ImVec2 row_end =
                ImGui::GetItemRectMax();
            const ImVec2 cursor_below_row =
                ImGui::GetCursorScreenPos();
            // the item queries are cached before the context menu, a popup leaves its own last
            // item behind so anything asked afterwards would be about the popup's contents
            const bool row_hovered = ImGui::IsItemHovered();
            const bool row_clicked = ImGui::IsItemClicked();
            const bool row_double_clicked =
                row_hovered &&
                ImGui::IsMouseDoubleClicked(
                    ImGuiMouseButton_Left
                );
            DrawAssetContextMenu(index);
            const float chevron_width =
                has_children
                    ? 24.0f * scale
                    : 0.0f;
            const ImVec2 mouse =
                ImGui::GetIO().MousePos;
            const bool chevron_hovered =
                has_children &&
                mouse.x >= row_start.x &&
                mouse.x <=
                    row_start.x + chevron_width &&
                mouse.y >= row_start.y &&
                mouse.y <= row_end.y;
            if (row_clicked && !renaming)
            {
                if (chevron_hovered)
                {
                    if (
                        m_expanded_assets.find(asset.id) !=
                        m_expanded_assets.end()
                    )
                    {
                        m_expanded_assets.erase(asset.id);
                    }
                    else
                    {
                        m_expanded_assets.insert(asset.id);
                    }
                }
                else
                {
                    select_asset(index);
                }
            }
            if (
                !renaming &&
                has_children &&
                !chevron_hovered &&
                row_double_clicked
            )
            {
                if (
                    m_expanded_assets.find(asset.id) !=
                    m_expanded_assets.end()
                )
                {
                    m_expanded_assets.erase(asset.id);
                }
                else
                {
                    m_expanded_assets.insert(asset.id);
                }
            }
            const bool open =
                has_children &&
                m_expanded_assets.find(asset.id) !=
                    m_expanded_assets.end();

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const bool row_selected = IsAssetSelected(index);
            if (
                row_selected ||
                row_hovered
            )
            {
                draw_list->AddRectFilled(
                    row_start,
                    row_end,
                    ImGui::GetColorU32(
                        row_selected
                            ? ImGuiCol_Header
                            : ImGuiCol_HeaderHovered
                    ),
                    4.0f * scale
                );
            }
            // the row the inspector is showing gets an edge, otherwise a selection of forty rows gives no
            // clue which one the panel on the right is about
            if (row_selected && m_selected_asset == index)
            {
                draw_list->AddRect(
                    row_start,
                    row_end,
                    ImGui::GetColorU32(ImGuiCol_NavCursor),
                    4.0f * scale
                );
            }
            if (has_children)
            {
                const ImVec2 center(
                    row_start.x + 11.0f * scale,
                    row_start.y + row_height * 0.5f
                );
                const float arrow_size = 4.0f * scale;
                if (open)
                {
                    draw_list->AddTriangleFilled(
                        ImVec2(
                            center.x - arrow_size,
                            center.y - arrow_size * 0.5f
                        ),
                        ImVec2(
                            center.x + arrow_size,
                            center.y - arrow_size * 0.5f
                        ),
                        ImVec2(
                            center.x,
                            center.y + arrow_size
                        ),
                        ImGui::GetColorU32(ImGuiCol_Text)
                    );
                }
                else
                {
                    draw_list->AddTriangleFilled(
                        ImVec2(
                            center.x - arrow_size * 0.5f,
                            center.y - arrow_size
                        ),
                        ImVec2(
                            center.x - arrow_size * 0.5f,
                            center.y + arrow_size
                        ),
                        ImVec2(
                            center.x + arrow_size,
                            center.y
                        ),
                        ImGui::GetColorU32(ImGuiCol_Text)
                    );
                }
            }
            if (!nested)
            {
                draw_list->AddRectFilled(
                    row_start,
                    ImVec2(
                        row_start.x + 3.0f * scale,
                        row_end.y
                    ),
                    asset_type_color(asset.type),
                    2.0f * scale
                );
            }

            const float badge_size =
                (nested ? 25.0f : 31.0f) * scale;
            const float badge_offset = has_children
                ? 29.0f * scale
                : 10.0f * scale;
            const ImVec2 badge_min(
                row_start.x + badge_offset,
                row_start.y +
                    (row_height - badge_size) * 0.5f
            );
            const ImVec2 badge_max(
                badge_min.x + badge_size,
                badge_min.y + badge_size
            );
            draw_list->AddRectFilled(
                badge_min,
                badge_max,
                asset_type_color(asset.type, 38),
                6.0f * scale
            );
            const char* badge = asset.type == "mesh"
                ? "M"
                : asset.type == "material"
                    ? "MAT"
                    : asset.type == "texture"
                        ? "TEX"
                        : "P";
            const ImVec2 badge_text = ImGui::CalcTextSize(badge);
            draw_list->AddText(
                ImVec2(
                    badge_min.x +
                        (badge_size - badge_text.x) * 0.5f,
                    badge_min.y +
                        (badge_size - badge_text.y) * 0.5f
                ),
                asset_type_color(asset.type),
                badge
            );

            const float text_x =
                badge_max.x + 9.0f * scale;
            const string display_name =
                asset_display_name(asset.name);
            if (renaming)
            {
                // the input is submitted after the row button so it wins the mouse, then the
                // cursor goes back below the row and the layout carries on untouched
                ImGui::SetCursorScreenPos(
                    ImVec2(
                        text_x,
                        row_start.y +
                            (nested ? 2.0f : 6.0f) * scale
                    )
                );
                DrawAssetRenameInline(
                    index,
                    max(
                        60.0f * scale,
                        row_end.x - text_x - 8.0f * scale
                    )
                );
                finish_overlay_cursor(cursor_below_row);
            }
            else
            {
                draw_list->AddText(
                    ImVec2(
                        text_x,
                        row_start.y +
                            (nested ? 4.0f : 8.0f) * scale
                    ),
                    ImGui::GetColorU32(ImGuiCol_Text),
                    display_name.c_str()
                );
            }

            string metadata = asset.type;
            if (has_children)
            {
                metadata +=
                    "  " +
                    to_string(child_count) +
                    (
                        child_count == 1
                            ? " resource"
                            : " resources"
                    );
            }
            else if (asset.disk_only)
            {
                metadata += "  unregistered";
            }
            else if (!asset.path.empty())
            {
                char quality[32] = {};
                snprintf(
                    quality,
                    sizeof(quality),
                    "  q %.1f%s",
                    asset.quality_score,
                    asset.quality_verified
                        ? "  verified"
                        : ""
                );
                metadata += quality;
            }
            else
            {
                metadata += "  unavailable";
            }
            draw_list->AddText(
                ImVec2(
                    text_x,
                    row_start.y +
                        (nested ? 24.0f : 31.0f) * scale
                ),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                metadata.c_str()
            );
            if (!renaming && row_hovered)
            {
                ImGuiSp::tooltip(
                    (
                        display_name +
                        "\nCatalog id: " +
                        asset.id +
                        "\nRight click to rename or delete"
                    ).c_str()
                );
            }
            ImGui::PopID();
            return open;
        };

    const bool filtering =
        m_type_filter != 0 ||
        m_search[0] != '\0';
    for (const int index : visible_roots)
    {
        const AssetEntry& asset = m_assets[index];
        const bool parent_matches = AssetMatchesFilter(asset);
        int visible_child_count = 0;
        if (asset.type == "prefab")
        {
            for (const int child_index : prefab_children[index])
            {
                if (
                    !filtering ||
                    parent_matches ||
                    AssetMatchesFilter(m_assets[child_index])
                )
                {
                    visible_child_count++;
                }
            }
            for (
                const string& dependency :
                direct_dependencies[index]
            )
            {
                if (
                    !filtering ||
                    parent_matches ||
                    dependency_matches_filter(dependency)
                )
                {
                    visible_child_count++;
                }
            }
        }

        if (filtering && !parent_matches)
        {
            m_expanded_assets.insert(asset.id);
        }
        const bool open = draw_asset_row(
            index,
            false,
            visible_child_count
        );
        if (!open)
        {
            continue;
        }

        ImGui::Indent(18.0f * scale);
        for (const int child_index : prefab_children[index])
        {
            if (
                filtering &&
                !parent_matches &&
                !AssetMatchesFilter(m_assets[child_index])
            )
            {
                continue;
            }
            draw_asset_row(child_index, true, 0);
        }
        for (
            const string& dependency :
            direct_dependencies[index]
        )
        {
            if (
                filtering &&
                !parent_matches &&
                !dependency_matches_filter(dependency)
            )
            {
                continue;
            }

            const float dependency_height = 38.0f * scale;
            const bool renaming_dependency =
                m_rename_dependency_path == dependency;
            ImGui::PushID(dependency.c_str());
            ImGui::InvisibleButton(
                "##dependency_row",
                ImVec2(
                    max(
                        1.0f,
                        ImGui::GetContentRegionAvail().x
                    ),
                    dependency_height
                ),
                ImGuiButtonFlags_MouseButtonLeft |
                ImGuiButtonFlags_MouseButtonRight
            );
            const ImVec2 cursor_below_dependency =
                ImGui::GetCursorScreenPos();
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            const bool dependency_hovered = ImGui::IsItemHovered();
            const bool dependency_clicked = ImGui::IsItemClicked();
            DrawDependencyContextMenu(dependency);
            if (dependency_clicked && !renaming_dependency)
            {
                LoadDependencyPreview(dependency);
            }
            ImDrawList* draw_list =
                ImGui::GetWindowDrawList();
            if (
                m_selected_dependency_path == dependency ||
                dependency_hovered
            )
            {
                draw_list->AddRectFilled(
                    minimum,
                    maximum,
                    ImGui::GetColorU32(
                        m_selected_dependency_path == dependency
                            ? ImGuiCol_Header
                            : ImGuiCol_HeaderHovered
                    ),
                    4.0f * scale
                );
            }
            const string type =
                asset_type_from_path(dependency);
            const string name = asset_display_name(
                FileSystem::
                    GetFileNameWithoutExtensionFromFilePath(
                        dependency
                    )
            );
            draw_list->AddCircleFilled(
                ImVec2(
                    minimum.x + 12.0f * scale,
                    minimum.y + 18.0f * scale
                ),
                3.0f * scale,
                asset_type_color(type)
            );
            if (renaming_dependency)
            {
                ImGui::SetCursorScreenPos(
                    ImVec2(
                        minimum.x + 23.0f * scale,
                        minimum.y + 1.0f * scale
                    )
                );
                DrawDependencyRenameInline(
                    dependency,
                    max(
                        60.0f * scale,
                        maximum.x -
                        minimum.x -
                        31.0f * scale
                    )
                );
                finish_overlay_cursor(cursor_below_dependency);
            }
            else
            {
                draw_list->AddText(
                    ImVec2(
                        minimum.x + 23.0f * scale,
                        minimum.y + 2.0f * scale
                    ),
                    ImGui::GetColorU32(ImGuiCol_Text),
                    name.c_str()
                );
            }
            draw_list->AddText(
                ImVec2(
                    minimum.x + 23.0f * scale,
                    minimum.y + 20.0f * scale
                ),
                ImGui::GetColorU32(
                    ImGuiCol_TextDisabled
                ),
                (type + "  linked file").c_str()
            );
            if (!renaming_dependency && dependency_hovered)
            {
                ImGuiSp::tooltip(dependency.c_str());
            }
            ImGui::PopID();
        }
        ImGui::Unindent(18.0f * scale);
    }

    if (visible_roots.empty())
    {
        const float offset =
            max(
                12.0f * scale,
                (height - 80.0f * scale) * 0.35f
            );
        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() +
            offset
        );
        const char* message = m_assets.empty()
            ? "No generated assets yet"
            : "No assets match these filters";
        const float message_width =
            ImGui::CalcTextSize(message).x;
        ImGui::SetCursorPosX(
            max(
                8.0f * scale,
                (width - message_width) * 0.5f
            )
        );
        ImGui::TextDisabled(
            "%s",
            message
        );
        const char* hint = m_assets.empty()
            ? "AI builds land in mcp/blockout. Refresh the library after a run."
            : "Try another name or asset type.";
        const float hint_width =
            ImGui::CalcTextSize(hint).x;
        ImGui::SetCursorPosX(
            max(
                8.0f * scale,
                (width - hint_width) * 0.5f
            )
        );
        ImGui::TextDisabled("%s", hint);
        if (!m_assets.empty())
        {
            ImGui::SetCursorPosX(
                max(
                    8.0f * scale,
                    (width - 108.0f * scale) * 0.5f
                )
            );
            if (ImGui::SmallButton("Clear filters"))
            {
                m_search.fill('\0');
                m_type_filter = 0;
            }
        }
    }

    m_visible_rows = drawn_rows;

    // the shortcuts read the list the user is looking at, so they are handled here rather than with the
    // panel wide ones, and only while the keyboard is not owned by a rename input or the search box
    const bool list_focused =
        ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows
        ) &&
        m_rename_asset_id.empty() &&
        m_rename_dependency_path.empty() &&
        !ImGui::GetIO().WantTextInput;
    if (
        list_focused &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_A, false)
    )
    {
        m_selected_assets.clear();
        for (const int index : drawn_rows)
        {
            m_selected_assets.insert(m_assets[index].id);
        }
        if (!drawn_rows.empty())
        {
            m_selection_anchor = drawn_rows.front();
        }
        m_status =
            "Selected " +
            to_string(m_selected_assets.size()) +
            (m_selected_assets.size() == 1 ? " asset" : " assets");
    }
    if (
        list_focused &&
        !m_selected_assets.empty() &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false)
    )
    {
        m_pending_delete_selection = true;
    }

    ImGui::EndChild();
}

bool AssetViewer::IsAssetSelected(const int index) const
{
    if (
        index < 0 ||
        index >= static_cast<int>(m_assets.size())
    )
    {
        return false;
    }

    return
        m_selected_assets.find(m_assets[index].id) !=
        m_selected_assets.end();
}

vector<string> AssetViewer::SelectedAssetIds() const
{
    // returned in list order so the confirmation reads the same way the list does
    vector<string> ids;
    ids.reserve(m_selected_assets.size());
    for (const AssetEntry& asset : m_assets)
    {
        if (
            m_selected_assets.find(asset.id) !=
            m_selected_assets.end()
        )
        {
            ids.push_back(asset.id);
        }
    }
    return ids;
}

void AssetViewer::ApplyRowSelection(
    const int index,
    const vector<int>& order
)
{
    if (
        index < 0 ||
        index >= static_cast<int>(m_assets.size())
    )
    {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const string& id = m_assets[index].id;

    // ctrl and shift only move the highlight, loading a preview per row would make extending a selection
    // across forty meshes rebuild the preview scene forty times
    if (io.KeyCtrl && !io.KeyShift)
    {
        if (m_selected_assets.erase(id) == 0)
        {
            m_selected_assets.insert(id);
            m_selection_anchor = index;
        }
        return;
    }

    if (io.KeyShift)
    {
        const auto anchor_position = find(
            order.begin(),
            order.end(),
            m_selection_anchor
        );
        const auto index_position = find(
            order.begin(),
            order.end(),
            index
        );
        if (
            anchor_position == order.end() ||
            index_position == order.end()
        )
        {
            m_selection_anchor = index;
            m_selected_assets.insert(id);
            return;
        }

        auto first = anchor_position;
        auto last = index_position;
        if (first > last)
        {
            swap(first, last);
        }
        m_selected_assets.clear();
        for (auto row = first; row <= last; ++row)
        {
            m_selected_assets.insert(m_assets[*row].id);
        }
        return;
    }

    m_selected_assets.clear();
    m_selected_assets.insert(id);
    m_selection_anchor = index;
    m_selected_asset = index;
    m_selected_dependency_path.clear();
    m_inspector_tab = 0;
    LoadSelectedAsset();
}

void AssetViewer::DrawAssetRenameInline(
    const int index,
    const float width
)
{
    if (
        index < 0 ||
        index >= static_cast<int>(m_assets.size())
    )
    {
        m_rename_asset_id.clear();
        return;
    }

    if (m_rename_request_focus)
    {
        ImGui::SetKeyboardFocusHere();
        m_rename_request_focus = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f * ui_scale());
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(4.0f * ui_scale(), 1.0f * ui_scale())
    );
    ImGui::SetNextItemWidth(width);

    const bool committed = ImGui::InputText(
        "##asset_rename_inline",
        &m_rename_buffer,
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_AutoSelectAll
    );
    const bool deactivated = ImGui::IsItemDeactivated();
    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);

    ImGui::PopStyleVar(2);

    if (cancelled)
    {
        m_rename_asset_id.clear();
        return;
    }
    if (committed || deactivated)
    {
        // queued rather than applied, the rename rebuilds m_assets and the row being drawn holds
        // a reference into it
        m_rename_commit_id = m_assets[index].id;
        m_rename_commit_name = m_rename_buffer;
        m_rename_asset_id.clear();
    }
}

void AssetViewer::DrawAssetContextMenu(const int index)
{
    if (
        index < 0 ||
        index >= static_cast<int>(m_assets.size())
    )
    {
        return;
    }

    if (!ImGui::BeginPopupContextItem("##asset_context"))
    {
        return;
    }

    const AssetEntry& asset = m_assets[index];

    // right clicking inside a selection acts on the selection, right clicking outside one replaces it, which
    // is what stops a menu from quietly deleting rows the user had stopped looking at
    if (!IsAssetSelected(index))
    {
        m_selected_assets.clear();
        m_selected_assets.insert(asset.id);
        m_selection_anchor = index;
    }
    const size_t selected_count = m_selected_assets.size();

    ImGui::TextDisabled(
        "%s",
        selected_count > 1
        ? (
            to_string(selected_count) +
            " assets selected"
        ).c_str()
        : asset_display_name(asset.name).c_str()
    );
    ImGui::Separator();

    if (
        ImGui::MenuItem(
            "Rename",
            "F2",
            false,
            selected_count <= 1
        )
    )
    {
        // one rename at a time, the two inline inputs share the buffer
        m_rename_dependency_path.clear();
        m_rename_asset_id = asset.id;
        m_rename_buffer = asset.name;
        m_rename_request_focus = true;
    }
    if (selected_count > 1)
    {
        if (
            ImGui::MenuItem(
                (
                    "Delete " +
                    to_string(selected_count) +
                    " assets"
                ).c_str(),
                "Del"
            )
        )
        {
            m_pending_delete_selection = true;
        }
    }
    else if (ImGui::MenuItem("Delete", "Del"))
    {
        m_pending_delete_id = asset.id;
    }
    if (
        ImGui::MenuItem(
            "Select all",
            "Ctrl+A",
            false,
            !m_visible_rows.empty()
        )
    )
    {
        m_selected_assets.clear();
        for (const int row : m_visible_rows)
        {
            m_selected_assets.insert(m_assets[row].id);
        }
    }

    ImGui::Separator();
    const bool has_path = !asset.path.empty();
    if (ImGui::MenuItem("Copy path", nullptr, false, has_path))
    {
        ImGui::SetClipboardText(asset.path.c_str());
        m_status = "Copied " + asset.path;
    }
    if (ImGui::MenuItem("Show in explorer", nullptr, false, has_path))
    {
        error_code error;
        const filesystem::path absolute = filesystem::absolute(
            filesystem::path(
                FileSystem::GetDirectoryFromFilePath(asset.path)
            ),
            error
        );
        if (!error)
        {
            FileSystem::OpenUrl(
                "file:///" + absolute.generic_string()
            );
        }
    }

    ImGui::EndPopup();
}

void AssetViewer::DrawDependencyRenameInline(
    const string& path,
    const float width
)
{
    if (m_rename_request_focus)
    {
        ImGui::SetKeyboardFocusHere();
        m_rename_request_focus = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f * ui_scale());
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(4.0f * ui_scale(), 1.0f * ui_scale())
    );
    ImGui::SetNextItemWidth(width);

    const bool committed = ImGui::InputText(
        "##dependency_rename_inline",
        &m_rename_buffer,
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_AutoSelectAll
    );
    const bool deactivated = ImGui::IsItemDeactivated();
    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);

    ImGui::PopStyleVar(2);

    if (cancelled)
    {
        m_rename_dependency_path.clear();
        return;
    }
    if (committed || deactivated)
    {
        m_rename_commit_path = path;
        m_rename_commit_name = m_rename_buffer;
        m_rename_dependency_path.clear();
    }
}

void AssetViewer::DrawDependencyContextMenu(const string& path)
{
    if (!ImGui::BeginPopupContextItem("##dependency_context"))
    {
        return;
    }

    const string leaf = FileSystem::GetFileNameFromFilePath(path);
    ImGui::TextDisabled("%s", leaf.c_str());
    ImGui::Separator();

    if (ImGui::MenuItem("Rename"))
    {
        // one rename at a time, the two inline inputs share the buffer
        m_rename_asset_id.clear();
        m_rename_dependency_path = path;
        m_rename_buffer =
            FileSystem::GetFileNameWithoutExtensionFromFilePath(path);
        m_rename_request_focus = true;
    }
    if (ImGui::MenuItem("Delete"))
    {
        m_pending_delete_path = path;
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Copy path"))
    {
        ImGui::SetClipboardText(path.c_str());
        m_status = "Copied " + path;
    }
    if (ImGui::MenuItem("Show in explorer"))
    {
        error_code error;
        const filesystem::path absolute = filesystem::absolute(
            filesystem::path(
                FileSystem::GetDirectoryFromFilePath(path)
            ),
            error
        );
        if (!error)
        {
            FileSystem::OpenUrl(
                "file:///" + absolute.generic_string()
            );
        }
    }

    ImGui::EndPopup();
}

void AssetViewer::DrawDeleteConfirmation()
{
    if (m_pending_delete_selection)
    {
        const vector<string> ids = SelectedAssetIds();
        if (ids.empty())
        {
            m_pending_delete_selection = false;
            return;
        }

        const char* selection_title = "Delete selected assets?";
        if (!ImGui::IsPopupOpen(selection_title))
        {
            ImGui::OpenPopup(selection_title);
        }

        if (
            ImGui::BeginPopupModal(
                selection_title,
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize
            )
        )
        {
            size_t prefab_count = 0;
            for (const string& id : ids)
            {
                for (const AssetEntry& asset : m_assets)
                {
                    if (
                        asset.id == id &&
                        asset.type == "prefab"
                    )
                    {
                        prefab_count++;
                        break;
                    }
                }
            }
            ImGui::Text(
                "Permanently delete %zu %s?",
                ids.size(),
                ids.size() == 1 ? "asset" : "assets"
            );
            ImGui::TextDisabled(
                "The asset path, source and thumbnail go with them."
            );
            if (prefab_count > 0)
            {
                ImGui::TextDisabled(
                    "%zu selected prefab%s will also lose owned dependency copies.",
                    prefab_count,
                    prefab_count == 1 ? "" : "s"
                );
            }
            ImGui::Spacing();

            // a scrolling list rather than a wall of text, a full library selection is hundreds of rows and
            // the modal would grow past the screen
            const float scale = ui_scale();
            ImGui::BeginChild(
                "##delete_selection_list",
                ImVec2(
                    360.0f * scale,
                    min(
                        200.0f * scale,
                        static_cast<float>(ids.size()) *
                        ImGui::GetTextLineHeightWithSpacing() +
                        4.0f * scale
                    )
                ),
                ImGuiChildFlags_Borders
            );
            for (const string& id : ids)
            {
                for (const AssetEntry& asset : m_assets)
                {
                    if (asset.id != id)
                    {
                        continue;
                    }

                    ImGui::TextDisabled(
                        "%s",
                        (
                            asset_display_name(asset.name) +
                            "  -  " +
                            (
                                asset.disk_only
                                ? asset.type + ", file only"
                                : asset.type
                            )
                        ).c_str()
                    );
                    break;
                }
            }
            ImGui::EndChild();

            ImGui::Spacing();
            if (ImGuiSp::button("Delete permanently"))
            {
                m_pending_delete_selection = false;
                DeleteAssets(ids);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGuiSp::button("Cancel"))
            {
                m_pending_delete_selection = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        else
        {
            m_pending_delete_selection = false;
        }
        return;
    }

    if (!m_pending_delete_path.empty())
    {
        const char* file_title = "Delete linked file?";
        if (!ImGui::IsPopupOpen(file_title))
        {
            ImGui::OpenPopup(file_title);
        }

        if (
            ImGui::BeginPopupModal(
                file_title,
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize
            )
        )
        {
            ImGui::Text(
                "Permanently delete %s?",
                FileSystem::GetFileNameFromFilePath(
                    m_pending_delete_path
                ).c_str()
            );
            ImGui::TextDisabled("%s", m_pending_delete_path.c_str());
            ImGui::TextDisabled(
                "Anything referencing it will fail to load."
            );
            ImGui::Spacing();
            if (ImGuiSp::button("Delete permanently"))
            {
                const string path = m_pending_delete_path;
                m_pending_delete_path.clear();
                FileSystem::Delete(path);
                m_status = FileSystem::Exists(path)
                    ? "Failed to delete " + path
                    : "Deleted " + path;
                if (normalized_path(m_loaded_path) == normalized_path(path))
                {
                    ClearLoadedAsset();
                }
                RefreshCatalog(true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGuiSp::button("Cancel"))
            {
                m_pending_delete_path.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        else
        {
            m_pending_delete_path.clear();
        }
        return;
    }

    if (m_pending_delete_id.empty())
    {
        return;
    }

    int index = -1;
    for (int i = 0; i < static_cast<int>(m_assets.size()); i++)
    {
        if (m_assets[i].id == m_pending_delete_id)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
    {
        m_pending_delete_id.clear();
        return;
    }

    const char* title = "Delete library asset?";
    if (!ImGui::IsPopupOpen(title))
    {
        ImGui::OpenPopup(title);
    }

    if (
        ImGui::BeginPopupModal(
            title,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        const AssetEntry& asset = m_assets[index];
        ImGui::Text(
            "Permanently delete %s?",
            asset_display_name(asset.name).c_str()
        );
        if (asset.disk_only)
        {
            ImGui::TextDisabled("%s", asset.path.c_str());
        }
        else
        {
            if (asset.type == "prefab")
            {
                ImGui::TextDisabled(
                    "This removes the prefab, source, thumbnail and owned dependency copies."
                );
            }
            else
            {
                ImGui::TextDisabled(
                    "This removes the asset path, source and thumbnail."
                );
            }
        }
        ImGui::Spacing();
        if (ImGuiSp::button("Delete permanently"))
        {
            m_selected_asset = index;
            m_selected_dependency_path.clear();
            DeleteSelectedAsset();
            m_pending_delete_id.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Cancel"))
        {
            m_pending_delete_id.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    else
    {
        // the modal was dismissed by clicking away or pressing escape
        m_pending_delete_id.clear();
    }
}

vector<string> AssetViewer::CleanupFileSignatures() const
{
    vector<string> paths = m_cleanup.orphan_files;
    paths.insert(
        paths.end(),
        m_cleanup.reference_files.begin(),
        m_cleanup.reference_files.end()
    );
    sort(paths.begin(), paths.end());
    paths.erase(unique(paths.begin(), paths.end()), paths.end());

    vector<string> signatures;
    signatures.reserve(paths.size());
    for (const string& path : paths)
    {
        error_code error;
        const filesystem::path resolved =
            filesystem::weakly_canonical(path, error);
        error_code size_error;
        const uintmax_t size =
            filesystem::file_size(path, size_error);
        error_code time_error;
        const auto write_time =
            filesystem::last_write_time(path, time_error);
        signatures.push_back(
            (
                error
                    ? normalized_path(path)
                    : normalized_path(resolved.generic_string())
            ) +
            "|" +
            to_string(size_error ? 0 : size) +
            "|" +
            (
                time_error
                    ? "missing"
                    : to_string(
                        write_time.time_since_epoch().count()
                    )
            )
        );
    }
    return signatures;
}

void AssetViewer::ScanLibraryCleanup()
{
    m_cleanup_generation++;
    m_cleanup = CleanupPlan();
    if (m_catalog_path.empty())
    {
        m_cleanup.error = "No asset catalog to clean up.";
        m_cleanup.scanned = true;
        return;
    }

    const string library_root =
        FileSystem::GetDirectoryFromFilePath(m_catalog_path);
    string source;
    if (!FileSystem::ReadFile(m_catalog_path, source))
    {
        m_cleanup.error =
            "The catalog could not be read from " + m_catalog_path;
        m_cleanup.scanned = true;
        return;
    }
    m_cleanup.reference_files.push_back(m_catalog_path);

    string parse_error;
    JsonValue root;
    const bool parsed = mcp_json::parse(source, root, parse_error);
    const JsonValue* schema_version =
        root.find("schema_version");
    const JsonValue* assets = root.find("assets");
    if (
        !parsed ||
        root.type != mcp_json::kind::object ||
        !schema_version ||
        static_cast<int>(schema_version->number_or(0.0)) != 2 ||
        !assets ||
        assets->type != mcp_json::kind::object
    )
    {
        m_cleanup.error =
            "The asset catalog schema is unsupported or invalid, cleanup aborted.";
        m_cleanup.scanned = true;
        return;
    }

    // the reachable set starts from what the project still needs, everything else in the library is
    // a leftover, references are followed because a kept prefab pulls in meshes, materials and
    // textures stored outside the primary asset folders
    unordered_set<string> reachable;
    vector<string> frontier;
    const auto mark =
        [&reachable, &frontier](const string& path)
    {
        if (path.empty())
        {
            return;
        }

        const string key = normalized_path(path);
        if (reachable.insert(key).second)
        {
            frontier.push_back(path);
        }
    };

    // worlds are roots, whichever one is loaded, a world that is not open right now still owns its
    // references and must not lose them
    {
        // the library root carries a trailing slash, taking the parent of it as is returns itself
        string trimmed = library_root;
        while (!trimmed.empty() && trimmed.back() == '/')
        {
            trimmed.pop_back();
        }

        const string project_root =
            FileSystem::GetDirectoryFromFilePath(trimmed);
        m_cleanup.reference_files.push_back(
            project_root.empty() ? "." : project_root
        );
        error_code error;
        filesystem::recursive_directory_iterator iterator(
            filesystem::path(
                project_root.empty() ? "." : project_root
            ),
            error
        );
        for (
            ;
            !error &&
            iterator != filesystem::recursive_directory_iterator();
            iterator.increment(error)
        )
        {
            const filesystem::directory_entry& item = *iterator;
            if (item.is_directory(error) && !error)
            {
                m_cleanup.reference_files.push_back(
                    item.path().generic_string()
                );
                continue;
            }
            if (!item.is_regular_file(error) || error)
            {
                error.clear();
                continue;
            }

            const string path = item.path().generic_string();
            if (path_is_within(path, library_root))
            {
                continue;
            }
            if (
                lower_copy(
                    FileSystem::GetExtensionFromFilePath(path)
                ) == EXTENSION_WORLD
            )
            {
                mark(path);
            }
        }
    }

    // catalog assets are roots, including their source, thumbnail and dependencies
    for (const auto& [asset_id, entry] : assets->object_items)
    {
        for (
            const char* key :
            {
                "path",
                "source_path",
                "thumbnail_path"
            }
        )
        {
            if (const JsonValue* value = entry.find(key))
            {
                mark(value->string_or(""));
            }
        }
        if (
            const JsonValue* dependencies =
                entry.find("dependencies");
            dependencies &&
            dependencies->type == mcp_json::kind::array
        )
        {
            for (const JsonValue& dependency : dependencies->array_items)
            {
                mark(dependency.string_or(""));
            }
        }
    }
    // follow references out of everything reachable so far
    while (!frontier.empty())
    {
        const string path = frontier.back();
        frontier.pop_back();
        const string extension = lower_copy(
            FileSystem::GetExtensionFromFilePath(path)
        );
        if (
            extension != EXTENSION_WORLD &&
            extension != EXTENSION_PREFAB &&
            extension != EXTENSION_MATERIAL
        )
        {
            continue;
        }

        m_cleanup.reference_files.push_back(path);
        pugi::xml_document document;
        if (!document.load_file(path.c_str()))
        {
            continue;
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

            for (
                const char* attribute_name :
                {
                    "mesh_path",
                    "material_path",
                    "texture_path",
                    "prefab_path"
                }
            )
            {
                mark(
                    node.attribute(attribute_name).as_string()
                );
            }
        }
    }

    // a material xml never stores its packed map, the runtime rebuilds that slot, so a plain
    // reference walk marks the colour and normal maps and misses the packed one next to them, the
    // packed map holds baked roughness and metalness that no other file on disk carries
    {
        const vector<string> reachable_snapshot(
            reachable.begin(),
            reachable.end()
        );
        for (const string& path : reachable_snapshot)
        {
            const string extension = lower_copy(
                FileSystem::GetExtensionFromFilePath(path)
            );
            if (asset_type_from_path(path) != "texture")
            {
                continue;
            }

            string base =
                FileSystem::GetDirectoryFromFilePath(path) +
                FileSystem::
                    GetFileNameWithoutExtensionFromFilePath(path);
            const vector<string> suffixes =
            {
                "_normal",
                "_packed"
            };
            for (const string& suffix : suffixes)
            {
                if (
                    base.size() > suffix.size() &&
                    base.compare(
                        base.size() - suffix.size(),
                        suffix.size(),
                        suffix
                    ) == 0
                )
                {
                    base.erase(base.size() - suffix.size());
                    break;
                }
            }

            for (const string& suffix : suffixes)
            {
                const string sibling = base + suffix + extension;
                if (FileSystem::Exists(sibling))
                {
                    mark(sibling);
                }
            }
        }
    }

    // sweep, anything in the library that nothing reachable needs is a leftover
    unordered_set<string> planned;
    const auto file_size =
        [](const string& path)
    {
        error_code error;
        const uintmax_t size =
            filesystem::file_size(filesystem::path(path), error);
        return error ? 0ull : static_cast<uint64_t>(size);
    };

    {
        m_cleanup.reference_files.push_back(library_root);
        error_code error;
        filesystem::recursive_directory_iterator iterator(
            filesystem::path(library_root),
            error
        );
        for (
            ;
            !error &&
            iterator != filesystem::recursive_directory_iterator();
            iterator.increment(error)
        )
        {
            const filesystem::directory_entry& item = *iterator;
            if (item.is_directory(error) && !error)
            {
                m_cleanup.reference_files.push_back(
                    item.path().generic_string()
                );
                continue;
            }
            if (!item.is_regular_file(error) || error)
            {
                error.clear();
                continue;
            }

            const string path = item.path().generic_string();
            if (
                normalized_path(path) ==
                    normalized_path(m_catalog_path) ||
                reachable.count(normalized_path(path)) ||
                !planned.insert(normalized_path(path)).second
            )
            {
                continue;
            }

            m_cleanup.orphan_files.push_back(path);
            m_cleanup.bytes += file_size(path);
        }
    }

    m_cleanup.file_signatures = CleanupFileSignatures();
    m_cleanup.scanned = true;
}

bool AssetViewer::ApplyLibraryCleanup()
{
    if (!m_cleanup.scanned || !m_cleanup.error.empty())
    {
        return false;
    }

    const string library_root =
        FileSystem::GetDirectoryFromFilePath(m_catalog_path);
    uint32_t deleted = 0;
    uint32_t failed = 0;
    const auto remove_file =
        [&](const string& path)
    {
        if (
            !path_is_within(path, library_root) ||
            !resolved_path_is_within(path, library_root)
        )
        {
            return;
        }
        if (!FileSystem::Exists(path))
        {
            return;
        }

        if (FileSystem::Delete(path))
        {
            deleted++;
        }
        else
        {
            failed++;
        }
    };

    for (const string& path : m_cleanup.orphan_files)
    {
        remove_file(path);
    }

    m_cleanup = CleanupPlan();
    ClearLoadedAsset();
    RefreshCatalog(true);
    m_status =
        "Cleanup removed " +
        to_string(deleted) +
        (deleted == 1 ? " file" : " files") +
        (
            failed == 0
                ? ""
                : ", " + to_string(failed) + " could not be removed"
        );
    return failed == 0;
}

void AssetViewer::DrawCleanupConfirmation()
{
    if (!m_cleanup.scanned)
    {
        return;
    }

    const char* title = "Clean up asset library?";
    if (!ImGui::IsPopupOpen(title))
    {
        ImGui::OpenPopup(title);
    }

    if (
        ImGui::BeginPopupModal(
            title,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        if (!m_cleanup.error.empty())
        {
            ImGui::TextUnformatted(m_cleanup.error.c_str());
            ImGui::Spacing();
            if (ImGuiSp::button("Close"))
            {
                m_cleanup = CleanupPlan();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        const size_t total = m_cleanup.orphan_files.size();
        if (total == 0)
        {
            ImGui::TextUnformatted(
                "The asset library is already clean."
            );
            ImGui::TextDisabled(
                "Every file is reachable from a world or catalog asset."
            );
            ImGui::Spacing();
            if (ImGuiSp::button("Close"))
            {
                m_cleanup = CleanupPlan();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        ImGui::Text(
            "Delete %zu files, %.1f MB?",
            total,
            static_cast<double>(m_cleanup.bytes) /
                (1024.0 * 1024.0)
        );
        ImGui::TextDisabled(
            "Catalog assets and anything referenced by a world are kept."
        );
        ImGui::Spacing();

        ImGui::BeginChild(
            "##cleanup_list",
            ImVec2(560.0f * ui_scale(), 240.0f * ui_scale()),
            ImGuiChildFlags_Borders
        );
        if (!m_cleanup.orphan_files.empty())
        {
            ImGui::TextUnformatted("UNREFERENCED LEFTOVERS");
            ImGui::Separator();
            for (const string& path : m_cleanup.orphan_files)
            {
                ImGui::TextDisabled("%s", path.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        if (ImGuiSp::button("Delete permanently"))
        {
            ApplyLibraryCleanup();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Cancel"))
        {
            m_cleanup = CleanupPlan();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    else
    {
        m_cleanup = CleanupPlan();
    }
}

bool AssetViewer::RenameAssetFile(
    const string& path,
    const string& new_name
)
{
    const string name = sanitize_asset_name(new_name);
    if (name.empty())
    {
        m_status = "That name cannot be used.";
        return false;
    }
    if (!FileSystem::Exists(path))
    {
        m_status = "The file is gone: " + path;
        return false;
    }

    const string extension =
        FileSystem::GetExtensionFromFilePath(path);
    const string new_path =
        FileSystem::GetDirectoryFromFilePath(path) +
        name +
        extension;
    if (normalized_path(new_path) == normalized_path(path))
    {
        return true;
    }
    if (FileSystem::Exists(new_path))
    {
        m_status =
            "A file named " + name + extension + " already exists.";
        return false;
    }

    FileSystem::Rename(path, new_path);
    if (!FileSystem::Exists(new_path))
    {
        m_status = "Failed to rename " + path;
        return false;
    }

    const uint32_t retargeted = retarget_library_references(
        FileSystem::GetDirectoryFromFilePath(m_catalog_path),
        FileSystem::GetFileNameFromFilePath(path),
        name + extension
    );
    m_status =
        retargeted == 0
            ? "Renamed to " + name
            : "Renamed to " + name +
              ", updated " + to_string(retargeted) +
              (retargeted == 1 ? " reference" : " references");
    return true;
}

bool AssetViewer::RenameAsset(
    const int index,
    const string& new_name
)
{
    if (
        index < 0 ||
        index >= static_cast<int>(m_assets.size())
    )
    {
        return false;
    }

    const AssetEntry& asset = m_assets[index];
    const string name = sanitize_asset_name(new_name);
    if (name.empty())
    {
        m_status = "That name cannot be used.";
        return false;
    }
    if (name == asset.name)
    {
        return true;
    }

    // an unregistered file has no catalog record, its file name is the only name it has, so the
    // rename has to happen on disk and every xml reference to it has to follow
    if (asset.disk_only)
    {
        const string old_path = asset.path;
        const string new_path =
            FileSystem::GetDirectoryFromFilePath(old_path) +
            name +
            FileSystem::GetExtensionFromFilePath(old_path);
        if (!RenameAssetFile(old_path, name))
        {
            return false;
        }

        const string status = m_status;
        ClearLoadedAsset();
        RefreshCatalog(true);

        // the id of an unregistered asset is its file name, so the refresh cannot restore the
        // selection by id, find the entry that now owns the renamed file instead
        const string wanted = normalized_path(new_path);
        for (int i = 0; i < static_cast<int>(m_assets.size()); i++)
        {
            if (
                normalized_path(m_assets[i].path) ==
                wanted
            )
            {
                m_selected_asset = i;
                m_selected_dependency_path.clear();
                LoadSelectedAsset();
                break;
            }
        }

        m_status = status;
        return true;
    }

    // catalog assets keep their files and their id, only the display name changes, so nothing
    // that points at them can break
    if (m_catalog_path.empty())
    {
        m_status = "No asset catalog to write to.";
        return false;
    }

    const string catalog_write_time =
        FileSystem::GetLastWriteTime(m_catalog_path);
    string source;
    if (!FileSystem::ReadFile(m_catalog_path, source))
    {
        m_status = "The asset catalog could not be read.";
        return false;
    }

    JsonValue root;
    string parse_error;
    if (!mcp_json::parse(source, root, parse_error))
    {
        m_status = "The asset catalog is invalid: " + parse_error;
        return false;
    }

    JsonValue* assets = json_object_find(root, "assets");
    if (
        root.type != mcp_json::kind::object ||
        !assets ||
        assets->type != mcp_json::kind::object
    )
    {
        m_status = "The asset catalog has no assets object.";
        return false;
    }

    JsonValue* asset_value = json_object_find(*assets, asset.id);
    if (!asset_value)
    {
        m_status = "The selected asset is no longer in the catalog.";
        RefreshCatalog(true);
        return false;
    }

    JsonValue& name_value = json_object_ensure(*asset_value, "name");
    name_value.type = mcp_json::kind::string;
    name_value.string_value = name;

    const string temporary_path = m_catalog_path + ".rename.tmp";
    const string backup_path = m_catalog_path + ".rename.backup";
    FileSystem::Delete(temporary_path);
    FileSystem::Delete(backup_path);
    if (
        !FileSystem::WriteFile(
            temporary_path,
            serialize_json(root) + "\n"
        )
    )
    {
        m_status = "The updated asset catalog could not be written.";
        return false;
    }
    if (
        FileSystem::GetLastWriteTime(m_catalog_path) !=
        catalog_write_time
    )
    {
        FileSystem::Delete(temporary_path);
        m_status = "The catalog changed during the rename, try again.";
        return false;
    }

    error_code error;
    filesystem::rename(m_catalog_path, backup_path, error);
    if (error)
    {
        FileSystem::Delete(temporary_path);
        m_status = "The asset catalog could not be backed up.";
        return false;
    }
    filesystem::rename(temporary_path, m_catalog_path, error);
    if (error)
    {
        filesystem::rename(backup_path, m_catalog_path, error);
        FileSystem::Delete(temporary_path);
        m_status = "The asset catalog could not be replaced.";
        return false;
    }
    FileSystem::Delete(backup_path);

    RefreshCatalog(true);
    m_status = "Renamed to " + name;
    return true;
}

bool AssetViewer::DeleteSelectedAsset()
{
    if (
        m_selected_asset < 0 ||
        m_selected_asset >= static_cast<int>(m_assets.size())
    )
    {
        m_status = "Select a catalog asset before deleting.";
        return false;
    }

    return DeleteAssets({ m_assets[m_selected_asset].id });
}

// the whole selection goes through one catalog rewrite. doing it a row at a time would read, rewrite and
// reload the catalog once per asset and refresh the list in between, which for a full library is minutes
// of work and leaves a half deleted catalog behind if any single step fails
bool AssetViewer::DeleteAssets(const vector<string>& ids)
{
    if (ids.empty())
    {
        m_status = "Select an asset before deleting.";
        return false;
    }

    // disk only entries have no catalog record at all, the file itself is the whole asset
    vector<string> catalog_ids;
    vector<string> disk_paths;
    for (const string& id : ids)
    {
        const AssetEntry* entry = nullptr;
        for (const AssetEntry& asset : m_assets)
        {
            if (asset.id == id)
            {
                entry = &asset;
                break;
            }
        }
        if (!entry)
        {
            continue;
        }

        if (entry->disk_only)
        {
            disk_paths.push_back(entry->path);
        }
        else
        {
            catalog_ids.push_back(id);
        }
    }

    uint32_t deleted_count = 0;
    uint32_t failed_count = 0;
    for (const string& path : disk_paths)
    {
        FileSystem::Delete(path);
        if (FileSystem::Exists(path))
        {
            failed_count++;
        }
        else
        {
            deleted_count++;
        }
    }

    if (catalog_ids.empty())
    {
        m_selected_assets.clear();
        m_selection_anchor = -1;
        ClearLoadedAsset();
        RefreshCatalog(true);
        m_status =
            failed_count == 0
            ? "Deleted " + to_string(deleted_count) + " files"
            : "Deleted " +
                to_string(deleted_count) +
                " files, " +
                to_string(failed_count) +
                " could not be removed";
        return failed_count == 0;
    }

    if (m_catalog_path.empty())
    {
        m_status = "There is no asset catalog to delete from.";
        return false;
    }

    const string catalog_write_time =
        FileSystem::GetLastWriteTime(m_catalog_path);
    string source;
    if (!FileSystem::ReadFile(m_catalog_path, source))
    {
        m_status = "The asset catalog could not be read.";
        return false;
    }

    JsonValue root;
    string parse_error;
    if (!mcp_json::parse(source, root, parse_error))
    {
        m_status =
            "The asset catalog is invalid: " +
            parse_error;
        return false;
    }

    JsonValue* assets = json_object_find(root, "assets");
    if (
        root.type != mcp_json::kind::object ||
        !assets ||
        assets->type != mcp_json::kind::object
    )
    {
        m_status = "The asset catalog has no assets object.";
        return false;
    }

    vector<string> owned_paths;
    vector<string> erased_ids;
    for (const string& asset_id : catalog_ids)
    {
        JsonValue* asset_value = json_object_find(*assets, asset_id);
        if (!asset_value)
        {
            continue;
        }

        for (
            const char* key :
            {
                "path",
                "source_path",
                "thumbnail_path"
            }
        )
        {
            if (const JsonValue* path = asset_value->find(key))
            {
                if (!path->string_or("").empty())
                {
                    owned_paths.push_back(path->string_or(""));
                }
            }
        }

        json_object_erase(*assets, asset_id);
        erased_ids.push_back(asset_id);
    }

    if (erased_ids.empty())
    {
        m_status = "The selected assets are no longer in the catalog.";
        m_selected_assets.clear();
        m_selection_anchor = -1;
        RefreshCatalog(true);
        return false;
    }

    const string temporary_path =
        m_catalog_path + ".delete.tmp";
    const string backup_path =
        m_catalog_path + ".delete.backup";
    FileSystem::Delete(temporary_path);
    FileSystem::Delete(backup_path);
    if (
        !FileSystem::WriteFile(
            temporary_path,
            serialize_json(root) + "\n"
        )
    )
    {
        m_status = "The updated asset catalog could not be written.";
        return false;
    }
    if (
        FileSystem::GetLastWriteTime(m_catalog_path) !=
        catalog_write_time
    )
    {
        FileSystem::Delete(temporary_path);
        m_status =
            "The catalog changed during deletion, try again.";
        return false;
    }

    error_code error;
    filesystem::rename(
        m_catalog_path,
        backup_path,
        error
    );
    if (error)
    {
        FileSystem::Delete(temporary_path);
        m_status = "The asset catalog could not be backed up.";
        return false;
    }
    filesystem::rename(
        temporary_path,
        m_catalog_path,
        error
    );
    if (error)
    {
        filesystem::rename(
            backup_path,
            m_catalog_path,
            error
        );
        FileSystem::Delete(temporary_path);
        m_status = "The asset catalog could not be replaced.";
        return false;
    }
    FileSystem::Delete(backup_path);

    ClearLoadedAsset();
    const string library_root =
        FileSystem::GetDirectoryFromFilePath(m_catalog_path);
    for (const string& owned_path : owned_paths)
    {
        if (
            path_is_within(owned_path, library_root) &&
            FileSystem::Exists(owned_path) &&
            !FileSystem::Delete(owned_path)
        )
        {
            failed_count++;
        }
    }
    for (const string& asset_id : erased_ids)
    {
        for (
            const char* folder :
            {
                "meshes",
                "materials",
                "prefabs",
                "sources",
                "thumbnails",
                "dependencies"
            }
        )
        {
            const string owned_directory =
                library_root +
                "/" +
                folder +
                "/" +
                asset_id;
            if (
                path_is_within(
                    owned_directory,
                    library_root
                ) &&
                FileSystem::Exists(owned_directory)
            )
            {
                error.clear();
                filesystem::remove_all(
                    owned_directory,
                    error
                );
                if (error)
                {
                    failed_count++;
                }
            }
        }
    }

    deleted_count += static_cast<uint32_t>(erased_ids.size());
    m_selected_assets.clear();
    m_selection_anchor = -1;
    RefreshCatalog(true);
    m_status =
        failed_count == 0 ?
        "Deleted " +
            to_string(deleted_count) +
            (deleted_count == 1 ? " asset" : " assets") :
        "Deleted " +
            to_string(deleted_count) +
            ", but " +
            to_string(failed_count) +
            " owned files could not be removed";
    return failed_count == 0;
}

void AssetViewer::DrawDetails(float height)
{
    ImGui::BeginChild(
        "##asset_viewer_details",
        ImVec2(0.0f, height),
        ImGuiChildFlags_Borders
    );

    const float scale = ui_scale();
    if (
        (
            m_selected_asset < 0 ||
            m_selected_asset >= static_cast<int>(m_assets.size())
        ) &&
        !m_selected_dependency_path.empty()
    )
    {
        const string type =
            asset_type_from_path(m_selected_dependency_path);
        const string dependency_name = asset_display_name(
            FileSystem::
                GetFileNameWithoutExtensionFromFilePath(
                    m_selected_dependency_path
                )
        );
        draw_asset_identity(
            dependency_name,
            type,
            "linked resource"
        );
        ImGui::Spacing();

        // a mesh reached by expanding a prefab is still a mesh, without this the simplify and lod
        // controls are only reachable when the mesh happens to be a top level catalog entry
        const bool mesh_tools_available =
            m_working_editable &&
            !m_working_vertices.empty();
        if (mesh_tools_available)
        {
            if (ImGui::BeginTabBar("##asset_dependency_tabs"))
            {
                if (ImGui::BeginTabItem("Overview"))
                {
                    m_dependency_tab = 0;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Optimize"))
                {
                    m_dependency_tab = 1;
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        else
        {
            m_dependency_tab = 0;
        }

        if (m_dependency_tab == 1)
        {
            DrawMeshTools();
            ImGui::EndChild();
            return;
        }

        if (type == "texture" && m_texture)
        {
            detail_row(
                "Resolution",
                to_string(m_texture->GetWidth()) +
                " x " +
                to_string(m_texture->GetHeight())
            );
            detail_row(
                "Channels",
                to_string(m_texture->GetChannelCount())
            );
        }
        else if (type == "material" && m_material)
        {
            section_title("TEXTURES");
            const vector<string> texture_paths =
                m_material->GetTexturePaths();
            if (texture_paths.empty())
            {
                ImGui::TextDisabled("No textures bound");
            }
            for (const string& texture_path : texture_paths)
            {
                ImGui::BulletText(
                    "%s",
                    FileSystem::GetFileNameFromFilePath(texture_path).c_str()
                );
            }
        }
        else
        {
            const auto [vertex_count, index_count] =
                GetPreviewGeometryCounts();
            detail_row("Vertices", compact_count(vertex_count));
            detail_row(
                "Triangles",
                compact_count(index_count / 3)
            );
        }

        section_title("SOURCE");
        ImGui::TextWrapped(
            "%s",
            m_selected_dependency_path.c_str()
        );
        if (ImGuiSp::button("Copy path"))
        {
            ImGui::SetClipboardText(
                m_selected_dependency_path.c_str()
            );
            m_status = "Copied linked file path";
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Show in folder"))
        {
            FileSystem::OpenUrl(
                FileSystem::GetDirectoryFromFilePath(
                    m_selected_dependency_path
                )
            );
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Reload"))
        {
            const string path = m_selected_dependency_path;
            LoadDependencyPreview(path);
        }
        ImGui::EndChild();
        return;
    }
    if (
        m_selected_asset < 0 ||
        m_selected_asset >= static_cast<int>(m_assets.size())
    )
    {
        const char* title = "Nothing selected";
        const char* hint =
            "Choose an asset from the library to inspect it.";
        ImGui::SetCursorPosY(
            max(
                20.0f * scale,
                height * 0.34f
            )
        );
        ImGui::SetCursorPosX(
            max(
                12.0f * scale,
                (
                    ImGui::GetContentRegionAvail().x -
                    ImGui::CalcTextSize(title).x
                ) *
                0.5f
            )
        );
        ImGui::TextUnformatted(title);
        ImGui::SetCursorPosX(18.0f * scale);
        ImGui::TextDisabled("%s", hint);
        ImGui::EndChild();
        return;
    }

    // a copy, an action drawn below can refresh the catalog and rebuild m_assets under a reference
    const AssetEntry asset = m_assets[m_selected_asset];

    const string display_name =
        asset_display_name(asset.name);
    draw_asset_identity(
        display_name,
        asset.type,
        "catalog asset"
    );

    ImGui::Spacing();
    const char* tabs[] = { "Overview", "Optimize" };
    // a prefab is editable through every mesh it references, so the tab shows for it as well
    const int tab_count =
        m_working_editable &&
        !m_working_vertices.empty()
            ? 2
            : 1;

    // panel sections, a real tab bar rather than toggle buttons, it carries the exclusivity
    // and keeps the selected section attached to the content below it
    if (ImGui::BeginTabBar("##asset_inspector_tabs"))
    {
        for (int tab = 0; tab < tab_count; tab++)
        {
            if (ImGui::BeginTabItem(tabs[tab]))
            {
                m_inspector_tab = tab;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    m_inspector_tab = min(m_inspector_tab, tab_count - 1);

    if (m_inspector_tab == 0)
    {
        if (asset.type == "prefab")
        {
            DrawPrefabOverview(asset);
        }
        else
        {
            section_title("TECHNICAL");
            if (asset.type == "texture")
            {
                detail_row(
                    "Resolution",
                    m_texture
                        ? to_string(m_texture->GetWidth()) +
                          " x " +
                          to_string(m_texture->GetHeight())
                        : "Unknown"
                );
                detail_row(
                    "Channels",
                    m_texture
                        ? to_string(m_texture->GetChannelCount())
                        : "Unknown"
                );
            }
            else
            {
                const auto [vertex_count, index_count] =
                    GetPreviewGeometryCounts();
                detail_row("Vertices", compact_count(vertex_count));
                detail_row(
                    "Triangles",
                    compact_count(index_count / 3)
                );
            }
            if (asset.type == "material" && m_material)
            {
                const vector<string> texture_paths =
                    m_material->GetTexturePaths();
                detail_row(
                    "Textures",
                    texture_paths.empty()
                        ? "None"
                        : to_string(texture_paths.size())
                );
            }
        }

        section_title("CATALOG");
        detail_row("Asset ID", asset.id);
        detail_row(
            "Tags",
            asset.tags.empty()
                ? "None"
                : join_strings(asset.tags, ", ")
        );
        detail_row(
            "Aliases",
            asset.aliases.empty()
                ? "None"
                : join_strings(asset.aliases, ", ")
        );
        if (!asset.path.empty())
        {
            detail_row(
                "Quality",
                to_string(asset.quality_score) +
                    (
                        asset.quality_verified
                            ? " verified"
                            : " unverified"
                    )
            );
        }

        section_title("SOURCE");
        if (!asset.path.empty())
        {
            ImGui::TextWrapped("%s", asset.path.c_str());
            if (ImGuiSp::button("Copy path"))
            {
                ImGui::SetClipboardText(asset.path.c_str());
                m_status = "Copied asset path";
            }
            ImGui::SameLine();
            if (ImGuiSp::button("Show in folder"))
            {
                FileSystem::OpenUrl(
                    FileSystem::GetDirectoryFromFilePath(
                        asset.path
                    )
                );
            }
            ImGui::SameLine();
            if (ImGuiSp::button("Reload"))
            {
                LoadSelectedAsset(false, true);
            }
            if (ImGuiSp::button("Capture"))
            {
                const string path =
                    World::GetLibraryResourceDirectory() +
                    "thumbnails/asset_" +
                    asset.id +
                    ".png";
                m_status = SavePreviewScreenshot(
                    path,
                    1024,
                    1024
                )
                    ? "Queued renderer capture to " + path
                    : "Preview capture failed";
            }
            ImGuiSp::tooltip("Save a 1024 x 1024 preview");
        }
    }
    else
    {
        DrawMeshTools();
    }

    if (m_inspector_tab != 1)
    {
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGuiSp::button("Delete asset"))
        {
            ImGui::OpenPopup("Delete asset?");
        }
    }

    if (
        ImGui::BeginPopupModal(
            "Delete asset?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        ImGui::Text(
            "Permanently delete %s?",
            display_name.c_str()
        );
        ImGui::TextDisabled(
            asset.type == "prefab"
                ? "This removes the prefab, source, thumbnail and owned dependency copies."
                : "This removes the asset path, source and thumbnail."
        );
        ImGui::Spacing();
        if (ImGuiSp::button("Delete permanently"))
        {
            DeleteSelectedAsset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    DrawBakeConfirmation();

    ImGui::EndChild();
}

void AssetViewer::DrawPrefabOverview(const AssetEntry& asset)
{
    const float scale = ui_scale();
    const auto [vertex_count, index_count] =
        GetPreviewGeometryCounts();
    const BakeSummary bake = PreviewBakeSummary();
    const vector<Material*> materials = PreviewMaterials();

    // the numbers that decide whether the asset is affordable, read left to right like a scoreboard
    const float gap = 6.0f * scale;
    const float card_width =
        (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(
        ImGui::Style::color_accent_1
    );
    const ImU32 warning = ImGui::ColorConvertFloat4ToU32(
        ImGui::Style::color_warning
    );
    ImGui::Spacing();
    draw_metric_card(
        "triangles",
        compact_count(index_count / 3),
        card_width
    );
    ImGui::SameLine(0.0f, gap);
    draw_metric_card(
        "draw calls",
        to_string(bake.renderers_before),
        card_width,
        bake.renderers_before > bake.renderers_after ? warning : 0
    );
    ImGui::SameLine(0.0f, gap);
    draw_metric_card(
        "materials",
        to_string(materials.size()),
        card_width
    );
    ImGui::Spacing();
    draw_metric_card(
        "vertices",
        compact_count(vertex_count),
        card_width
    );
    ImGui::SameLine(0.0f, gap);
    draw_metric_card(
        "parts",
        to_string(max(0, m_prefab_entity_count - 1)),
        card_width
    );
    ImGui::SameLine(0.0f, gap);
    draw_metric_card(
        "mesh files",
        to_string(m_working_meshes.size()),
        card_width
    );

    if (!m_missing_dependencies.empty())
    {
        const ImVec4 warning_color = ImGui::Style::color_warning;
        ImGui::Spacing();
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(
                warning_color.x,
                warning_color.y,
                warning_color.z,
                0.1f
            )
        );
        ImGui::BeginChild(
            "##missing_dependencies",
            ImVec2(0.0f, 52.0f * scale),
            ImGuiChildFlags_Borders
        );
        ImGui::Text(
            "%zu missing dependenc%s",
            m_missing_dependencies.size(),
            m_missing_dependencies.size() == 1
                ? "y"
                : "ies"
        );
        ImGui::TextDisabled(
            "The preview may be incomplete"
        );
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    section_title("GAME READY");
    const bool can_bake =
        bake.renderers_before > bake.renderers_after &&
        !m_working_modified &&
        !m_working_lods_built &&
        !asset.path.empty();
    if (bake.renderers_before > bake.renderers_after)
    {
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(
                ImGui::Style::color_accent_1.x,
                ImGui::Style::color_accent_1.y,
                ImGui::Style::color_accent_1.z,
                0.1f
            )
        );
        ImGui::BeginChild(
            "##bake_hint",
            ImVec2(0.0f, 50.0f * scale),
            ImGuiChildFlags_Borders
        );
        ImGui::Text(
            "%u parts share %u material%s",
            bake.renderers_before - bake.skipped,
            bake.materials,
            bake.materials == 1 ? "" : "s"
        );
        ImGui::TextDisabled(
            "Baking draws them in %u call%s instead of %u",
            bake.renderers_after,
            bake.renderers_after == 1 ? "" : "s",
            bake.renderers_before
        );
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextDisabled(
            bake.renderers_before == 0
                ? "No geometry is drawn yet"
                : "One draw call per material, nothing left to bake"
        );
    }
    if (m_working_modified || m_working_lods_built)
    {
        ImGui::TextDisabled(
            "Save or revert the mesh changes in Optimize before baking"
        );
    }
    if (!can_bake)
    {
        ImGui::BeginDisabled();
    }
    if (
        ImGuiSp::button(
            "Bake parts by material",
            ImVec2(-1.0f, 0.0f)
        )
    )
    {
        m_bake_confirmation_open = true;
    }
    if (!can_bake)
    {
        ImGui::EndDisabled();
    }
    ImGuiSp::tooltip(
        "Merges every part that shares a material into one mesh and rewrites the prefab, "
        "the parts that were merged away are removed from it"
    );
    ImGui::TextDisabled(
        "Simplify and LODs for the whole prefab live in the Optimize tab"
    );

    section_title("MATERIALS");
    if (materials.empty())
    {
        ImGui::TextDisabled("No materials are previewed");
    }
    for (Material* material : materials)
    {
        const string normal_path = material->GetTexturePathByType(
            MaterialTextureType::Normal
        );
        const size_t texture_count =
            material->GetTexturePaths().size();
        ImGui::BulletText(
            "%s",
            asset_display_name(material->GetObjectName()).c_str()
        );
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%zu texture%s%s",
            texture_count,
            texture_count == 1 ? "" : "s",
            normal_path.empty() ? "" : ", normal"
        );
    }
}

void AssetViewer::DrawBakeConfirmation()
{
    if (m_bake_confirmation_open)
    {
        ImGui::OpenPopup("Bake prefab?");
        m_bake_confirmation_open = false;
    }
    if (
        !ImGui::BeginPopupModal(
            "Bake prefab?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        return;
    }

    const BakeSummary bake = PreviewBakeSummary();
    ImGui::TextUnformatted(
        "Merge the parts that share a material and overwrite the prefab?"
    );
    ImGui::TextDisabled(
        "%u draw calls become %u, the merged parts are removed from the prefab",
        bake.renderers_before,
        bake.renderers_after
    );
    if (bake.skipped > 0)
    {
        ImGui::TextDisabled(
            "%u part%s carry other components and stay as they are",
            bake.skipped,
            bake.skipped == 1 ? "" : "s"
        );
    }
    ImGui::Checkbox(
        "Generate LODs for the merged mesh",
        &m_bake_generate_lods
    );
    ImGui::Spacing();
    if (ImGuiSp::button("Bake and overwrite"))
    {
        BakePrefabByMaterial(m_bake_generate_lods);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGuiSp::button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetViewer::DrawMeshTools()
{
    if (
        !m_working_editable ||
        m_working_vertices.empty()
    )
    {
        return;
    }

    // the levels already in the mesh are read on first sight of this panel, so a mesh that ships with a
    // chain can be paged through without building anything
    if (!m_working_lods_scanned)
    {
        m_working_lods_scanned = true;
        LoadExistingLods();
    }

    // the mesh wide counts include every lod, so lod 0 is summed instead or the reduction readout
    // compares the edit against geometry it never touched
    uint64_t source_vertices = 0;
    uint64_t source_indices = 0;
    uint64_t working_vertices = 0;
    uint64_t working_indices = 0;
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        source_vertices += working.source_vertex_count;
        source_indices += working.source_index_count;
        working_vertices += working.vertices.size();
        working_indices += working.indices.size();
    }
    const uint64_t source_triangles = source_indices / 3;
    // the lod picker changes what the viewport shows, not what the edit is, so these track the
    // working geometry rather than the flattened preview
    const uint64_t working_triangles = working_indices / 3;
    const float reduction = source_triangles > 0
        ? 1.0f -
            static_cast<float>(working_triangles) /
            static_cast<float>(source_triangles)
        : 0.0f;

    const float scale = ui_scale();
    const float footer_height = 86.0f * scale;
    const float content_height = max(
        80.0f * scale,
        ImGui::GetContentRegionAvail().y -
            footer_height
    );
    ImGui::BeginChild(
        "##mesh_tools_content",
        ImVec2(0.0f, content_height),
        ImGuiChildFlags_None
    );

    if (m_working_modified || m_working_lods_built)
    {
        const ImVec4 accent = ImGui::Style::color_accent_1;
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(accent.x, accent.y, accent.z, 0.1f)
        );
        ImGui::BeginChild(
            "##mesh_changes",
            ImVec2(0.0f, 48.0f * scale),
            ImGuiChildFlags_Borders
        );
        ImGui::TextUnformatted("Unsaved mesh changes");
        ImGui::TextDisabled(
            "%.0f%% fewer triangles",
            reduction * 100.0f
        );
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::TextUnformatted("GEOMETRY");
    ImGui::Separator();
    detail_row(
        "Source",
        compact_count(source_vertices) +
            " vertices, " +
            compact_count(source_triangles) +
            " triangles"
    );
    detail_row(
        "Working",
        compact_count(working_vertices) +
            " vertices, " +
            compact_count(working_triangles) +
            " triangles"
    );
    detail_row(
        "Sub meshes",
        m_working_meshes.size() > 1
            ? to_string(m_working_sub_meshes.size()) +
                " across " +
                to_string(m_working_meshes.size()) +
                " mesh files"
            : to_string(m_working_sub_meshes.size())
    );
    ImGui::TextDisabled(
        "Current reduction %.1f%%",
        reduction * 100.0f
    );
    if (m_working_meshes.size() > 1 || m_working_sub_meshes.size() > 1)
    {
        // one slider for the whole asset, each part keeps the same share of its own triangles so
        // the small parts do not vanish before the large ones lose any detail
        ImGui::TextDisabled(
            "Every part is reduced together, each keeps the same share of its triangles"
        );
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("1  REDUCE GEOMETRY");
    ImGui::Separator();
    ImGui::TextDisabled(
        "Keep this share of the original triangles"
    );

    // the member is a ratio but a ratio shown through a percent format reads as 0%, the slider works in
    // percent so the number on it is the number the user is choosing
    float target_percent = m_target_ratio * 100.0f;
    ImGui::SetNextItemWidth(-1.0f);
    if (
        ImGui::SliderFloat(
            "##triangle_target",
            &target_percent,
            2.0f,
            100.0f,
            "keep %.0f%%",
            ImGuiSliderFlags_AlwaysClamp
        )
    )
    {
        m_target_ratio = target_percent / 100.0f;
    }
    // simplifying every frame of a drag would rebuild the whole mesh per pixel, so it lands on release
    const bool apply_target =
        ImGui::IsItemDeactivatedAfterEdit();

    ImGui::TextDisabled(
        "About %s triangles, %.0f%% removed",
        compact_count(
            static_cast<uint64_t>(
                static_cast<float>(source_triangles) *
                m_target_ratio
            )
        ).c_str(),
        (1.0f - m_target_ratio) * 100.0f
    );

    ImGui::Spacing();

    // the button is drawn first and tested after, an or would short circuit it away on the very frame
    // the slider is released and the row would blink out of the panel
    const bool apply_pressed =
        ImGuiSp::button(
            "Apply simplification",
            ImVec2(-1.0f, 0.0f)
        );
    if (apply_target || apply_pressed)
    {
        SimplifyWorkingGeometry(m_target_ratio);
    }
    if (
        ImGuiSp::button(
            "Weld and optimize",
            ImVec2(-1.0f, 0.0f)
        )
    )
    {
        OptimizeWorkingGeometry();
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("2  BUILD LEVELS OF DETAIL");
    ImGui::Separator();
    ImGui::Checkbox(
        "Generate LODs when saving",
        &m_working_generate_lods
    );
    ImGuiSp::tooltip(
        "Rebuild lower detail levels from the edited geometry"
    );
    if (
        ImGuiSp::button(
            m_working_lods.empty() ? "Build LODs" : "Rebuild LODs",
            ImVec2(-1.0f, 0.0f)
        )
    )
    {
        BuildWorkingLods();
    }
    ImGuiSp::tooltip(
        "Preview the chain that saving would bake, this does not change the mesh"
    );

    if (!m_working_lods.empty())
    {
        const int lod_count =
            static_cast<int>(m_working_lods.size());
        m_preview_lod = clamp(m_preview_lod, 0, lod_count - 1);

        const auto lod_triangles =
            [this](const int lod)
            {
                uint64_t indices = 0;
                for (
                    const WorkingSubMesh& working :
                    m_working_lods[static_cast<size_t>(lod)]
                )
                {
                    indices += working.indices.size();
                }
                return indices / 3;
            };

        // each entry carries its own cost, so choosing a level is a comparison rather than a guess
        const auto lod_label =
            [&](const int lod)
            {
                const uint64_t triangles = lod_triangles(lod);
                string label =
                    "LOD " +
                    to_string(lod) +
                    "  -  " +
                    compact_count(triangles) +
                    " triangles";
                if (lod == 0)
                {
                    return label + "  (full detail)";
                }

                const int removed = source_triangles > 0
                    ? static_cast<int>(
                        (
                            1.0f -
                            static_cast<float>(triangles) /
                            static_cast<float>(source_triangles)
                        ) *
                        100.0f
                    )
                    : 0;
                return label + "  (-" + to_string(removed) + "%)";
            };

        ImGui::TextDisabled(
            m_working_lods_built
                ? "Showing in the viewport, not saved yet"
                : "Showing in the viewport, saved in this mesh"
        );
        ImGui::SetNextItemWidth(-1.0f);
        if (
            ImGui::BeginCombo(
                "##preview_lod",
                lod_label(m_preview_lod).c_str()
            )
        )
        {
            for (int lod = 0; lod < lod_count; lod++)
            {
                const bool selected = lod == m_preview_lod;
                if (
                    ImGui::Selectable(
                        lod_label(lod).c_str(),
                        selected
                    ) &&
                    !selected
                )
                {
                    m_preview_lod = lod;
                    FlattenWorkingGeometry();
                    RefreshPreviewMeshGeometry();
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGuiSp::tooltip(
            "Switch the preview between the levels that saving would bake"
        );

        // stepping is how the levels get compared, hunting the same entry in a dropdown each time is
        // what makes a chain of five feel like work
        const float step_width =
            (
                ImGui::GetContentRegionAvail().x -
                4.0f * ui_scale()
            ) *
            0.5f;
        const bool at_first = m_preview_lod <= 0;
        const bool at_last = m_preview_lod >= lod_count - 1;
        int stepped = m_preview_lod;
        if (at_first)
        {
            ImGui::BeginDisabled();
        }
        if (
            ImGuiSp::button(
                "More detail",
                ImVec2(step_width, 0.0f)
            )
        )
        {
            stepped = m_preview_lod - 1;
        }
        if (at_first)
        {
            ImGui::EndDisabled();
        }
        ImGui::SameLine(0.0f, 4.0f * ui_scale());
        if (at_last)
        {
            ImGui::BeginDisabled();
        }
        if (
            ImGuiSp::button(
                "Less detail",
                ImVec2(step_width, 0.0f)
            )
        )
        {
            stepped = m_preview_lod + 1;
        }
        if (at_last)
        {
            ImGui::EndDisabled();
        }
        if (stepped != m_preview_lod)
        {
            m_preview_lod = clamp(stepped, 0, lod_count - 1);
            FlattenWorkingGeometry();
            RefreshPreviewMeshGeometry();
        }
    }
    else if (m_working_lods_attempted)
    {
        // pressing build and getting nothing back looks identical to never pressing it, which is the
        // one state worth spelling out
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImGui::Style::color_accent_1
        );
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(
            "This geometry is already too simple to reduce further, so there is "
            "no chain to preview. Lower the simplification target first if you "
            "want coarser levels."
        );
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextDisabled(
            "This mesh has no saved levels, build a chain to preview one"
        );
    }

    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextUnformatted("3  REVIEW AND SAVE");

    const bool can_save =
        m_working_modified ||
        m_working_lods_built;
    ImGui::TextDisabled(
        can_save
            ? "Changes are previewed but not saved"
            : "No pending mesh changes"
    );

    const float action_gap = 4.0f * scale;
    const float action_width =
        (
            ImGui::GetContentRegionAvail().x -
            action_gap
        ) *
        0.5f;
    if (!can_save)
    {
        ImGui::BeginDisabled();
    }
    if (
        ImGuiSp::button(
            "Revert",
            ImVec2(action_width, 0.0f)
        )
    )
    {
        LoadWorkingGeometry();
    }
    ImGuiSp::tooltip(
        "Discard simplification and rebuilt LOD previews"
    );
    ImGui::SameLine(0.0f, action_gap);
    if (
        ImGuiSp::button(
            "Save mesh",
            ImVec2(action_width, 0.0f)
        )
    )
    {
        ImGui::OpenPopup("Save mesh changes?");
    }
    if (!can_save)
    {
        ImGui::EndDisabled();
    }

    if (
        ImGui::BeginPopupModal(
            "Save mesh changes?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        // the lod picker only changes what is on screen, the bake always writes the working
        // geometry as lod 0 and derives the rest from it
        ImGui::TextUnformatted(
            m_working_meshes.size() > 1
                ? "Overwrite the prefab's mesh files?"
                : "Overwrite the current mesh?"
        );
        ImGui::TextDisabled(
            "%llu to %llu triangles, %.1f%% reduction",
            static_cast<unsigned long long>(source_triangles),
            static_cast<unsigned long long>(working_triangles),
            reduction * 100.0f
        );
        ImGui::TextDisabled(
            "%zu sub meshes in %zu file%s, LODs %s",
            m_working_sub_meshes.size(),
            m_working_meshes.size(),
            m_working_meshes.size() == 1 ? "" : "s",
            m_working_generate_lods ? "rebuilt" : "dropped"
        );
        ImGui::Spacing();
        if (ImGuiSp::button("Save and overwrite"))
        {
            SaveWorkingGeometry();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void AssetViewer::DrawRevisionBanner()
{
    if (!m_revision.candidate_active)
    {
        return;
    }

    const float scale = ui_scale();
    ImVec4 revision_background =
        ImGui::Style::color_accent_1;
    revision_background.w = 0.12f;
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        revision_background
    );
    ImGui::BeginChild(
        "##asset_revision_banner",
        ImVec2(
            0.0f,
            (
                m_revision.request_error.empty() ?
                78.0f :
                98.0f
            ) * scale
        ),
        ImGuiChildFlags_Borders
    );
    ImGui::TextUnformatted("ASSET REVISION AVAILABLE");
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s  generation %llu",
        m_revision.base_asset_id.c_str(),
        static_cast<unsigned long long>(
            m_revision.generation
        )
    );
    ImGui::TextDisabled(
        "%llu to %llu entities  |  %llu to %llu dependencies",
        static_cast<unsigned long long>(
            m_revision.base_entity_count
        ),
        static_cast<unsigned long long>(
            m_revision.candidate_entity_count
        ),
        static_cast<unsigned long long>(
            m_revision.base_dependency_count
        ),
        static_cast<unsigned long long>(
            m_revision.candidate_dependency_count
        )
    );

    if (m_revision.request_pending)
    {
        ImGui::BeginDisabled();
    }
    if (ImGuiSp::button("Preview"))
    {
        string error;
        if (!LoadRevisionCandidate(error))
        {
            m_status = error;
        }
    }
    ImGui::SameLine();
    if (ImGuiSp::button("Apply revision"))
    {
        m_revision_confirmation_action = "apply";
        m_revision_confirmation_generation =
            m_revision.generation;
        ImGui::OpenPopup("Apply asset revision?");
    }
    ImGui::SameLine();
    if (ImGuiSp::button("Discard revision"))
    {
        m_revision_confirmation_action = "discard";
        m_revision_confirmation_generation =
            m_revision.generation;
        ImGui::OpenPopup("Discard asset revision?");
    }
    if (m_revision.request_pending)
    {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%s request pending",
            m_revision.request_action.c_str()
        );
    }
    if (!m_revision.request_error.empty())
    {
        m_status =
            "Asset revision request failed: " +
            m_revision.request_error;
        ImGui::TextUnformatted(m_status.c_str());
    }

    DrawRevisionConfirmation();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void AssetViewer::DrawRevisionConfirmation()
{
    const bool applying =
        m_revision_confirmation_action == "apply";
    const char* title = applying
        ? "Apply asset revision?"
        : "Discard asset revision?";
    if (
        m_revision_confirmation_action.empty() ||
        !ImGui::BeginPopupModal(
            title,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        return;
    }

    ImGui::TextUnformatted(
        applying
            ? "Request this revision to replace the current asset?"
            : "Request this revision candidate to be discarded?"
    );
    ImGui::TextDisabled(
        "%s  generation %llu",
        m_revision.base_asset_id.c_str(),
        static_cast<unsigned long long>(
            m_revision_confirmation_generation
        )
    );
    ImGui::Spacing();

    if (
        ImGuiSp::button(
            applying
                ? "Request apply"
                : "Request discard"
        )
    )
    {
        string error;
        const bool requested = applying
            ? RequestRevisionApply(
                m_revision.base_asset_id,
                m_revision_confirmation_generation,
                true,
                error
            )
            : RequestRevisionDiscard(
                m_revision.base_asset_id,
                m_revision_confirmation_generation,
                true,
                error
            );
        if (!requested)
        {
            m_status = error;
        }
        m_revision_confirmation_action.clear();
        m_revision_confirmation_generation = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGuiSp::button("Cancel"))
    {
        m_revision_confirmation_action.clear();
        m_revision_confirmation_generation = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetViewer::DrawPreview(float width, float height)
{
    ImGui::BeginChild(
        "##asset_viewer_preview",
        ImVec2(width, height),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse
    );

    DrawRevisionBanner();

    const float scale = ui_scale();
    const float content_right =
        ImGui::GetCursorPosX() +
        ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted("VIEWPORT");
    const auto [vertex_count, index_count] =
        GetPreviewGeometryCounts();
    const string summary =
        compact_count(index_count / 3) +
        " tris  |  " +
        compact_count(vertex_count) +
        " verts";
    const float summary_width =
        ImGui::CalcTextSize(summary.c_str()).x;
    if (
        summary_width + ImGui::GetCursorPosX() <
        content_right
    )
    {
        ImGui::SameLine();
        ImGui::SetCursorPosX(
            content_right -
            summary_width
        );
        ImGui::TextDisabled("%s", summary.c_str());
    }

    if (!m_texture)
    {
        if (ImGui::SmallButton("Display"))
        {
            ImGui::OpenPopup("##asset_preview_display");
        }
        ImGuiSp::tooltip(
            "Shading and backdrop settings"
        );
        if (ImGui::BeginPopup("##asset_preview_display"))
        {
            const char* modes =
                "Solid\0"
                "Wireframe\0"
                "Vertices\0";
            ImGui::TextDisabled("Shading");
            ImGui::SetNextItemWidth(180.0f * scale);
            if (
                ImGui::Combo(
                    "##asset_preview_mode",
                    &m_preview_mode,
                    modes
                )
            )
            {
                m_preview_dirty = true;
            }

            const char* backdrops =
                "Auto\0"
                "Sky\0"
                "Charcoal\0"
                "Slate\0"
                "Paper\0";
            ImGui::TextDisabled("Backdrop");
            ImGui::SetNextItemWidth(180.0f * scale);
            if (
                ImGui::Combo(
                    "##asset_preview_backdrop",
                    &m_preview_backdrop,
                    backdrops
                )
            )
            {
                m_preview_dirty = true;
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine(0.0f, 4.0f * scale);
        if (
            toolbar_toggle(
                "Turntable",
                m_preview_auto_rotate
            )
        )
        {
            m_preview_auto_rotate = !m_preview_auto_rotate;
        }
    }
    else
    {
        ImGui::TextDisabled("Texture preview");
    }

    ImGui::SameLine(0.0f, 4.0f * scale);
    if (toolbar_toggle("Stats", m_preview_show_stats))
    {
        m_preview_show_stats = !m_preview_show_stats;
    }
    ImGui::SameLine(0.0f, 8.0f * scale);
    if (ImGui::SmallButton("Frame"))
    {
        m_preview_zoom = 1.0f;
        m_texture_pan = math::Vector2::Zero;
        m_preview_dirty = true;
    }
    ImGuiSp::tooltip("Frame asset");
    ImGui::SameLine(0.0f, 3.0f * scale);
    if (ImGui::SmallButton("Reset"))
    {
        m_preview_yaw = 0.65f;
        m_preview_pitch = 0.35f;
        m_preview_zoom = 1.0f;
        m_texture_pan = math::Vector2::Zero;
        m_preview_dirty = true;
    }
    ImGuiSp::tooltip("Reset camera");

    const ImVec2 available =
        ImGui::GetContentRegionAvail();
    const ImVec2 size(
        max(1.0f, available.x),
        max(1.0f, available.y)
    );
    const uint32_t preview_width =
        max(1u, static_cast<uint32_t>(size.x));
    const uint32_t preview_height =
        max(1u, static_cast<uint32_t>(size.y));
    RHI_Texture* renderer_output =
        Renderer::GetSecondaryViewOutput();
    if (
        Renderer::IsSecondaryViewReady() &&
        m_preview_settle_frames > 0
    )
    {
        m_preview_settle_frames--;
        m_preview_dirty = true;
    }

    // a prefab commits its parts across frames and a mesh can finish importing late, so the preview
    // re-renders until what it would draw stops changing, a fixed settle count gives up too early and
    // leaves a part missing with nothing to trigger another attempt
    if (!m_texture)
    {
        const uint64_t signature = PreviewSceneSignature();
        if (signature != m_preview_signature)
        {
            m_preview_signature = signature;
            m_preview_dirty = true;
            // what the preview draws just changed, so the framing it was fitted to is out of date
            RefreshPreviewBounds();
        }
    }
    if (
        !m_texture &&
        PreviewRoot() &&
        !Renderer::IsSecondaryViewReady()
    )
    {
        m_preview_dirty = true;
    }
    if (
        !m_texture &&
        renderer_output &&
        (
            renderer_output->GetWidth() != preview_width ||
            renderer_output->GetHeight() != preview_height
        )
    )
    {
        m_preview_dirty = true;
    }

    if (
        !m_texture &&
        Renderer::IsSecondaryViewReady() &&
        renderer_output
    )
    {
        ImGuiSp::image(renderer_output, size);
    }
    else
    {
        ImGui::Dummy(size);
    }
    const ImVec2 minimum =
        ImGui::GetItemRectMin();
    const ImVec2 maximum =
        ImGui::GetItemRectMax();

    // an invisible button on top of the canvas claims the press, without it imgui
    // treats a drag over blank window space as a window move and the preview orbits
    // at the same time
    ImGui::SetCursorScreenPos(minimum);
    ImGui::InvisibleButton(
        "##asset_viewer_canvas",
        size,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonMiddle
    );
    const ImGuiIO& io = ImGui::GetIO();
    const bool preview_hovered = ImGui::IsItemHovered();

    if (
        preview_hovered &&
        (
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Middle)
        )
    )
    {
        m_preview_orbiting = true;
    }
    if (
        !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Middle)
    )
    {
        m_preview_orbiting = false;
    }
    if (
        preview_hovered &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
    )
    {
        m_preview_zoom = 1.0f;
        m_texture_pan = math::Vector2::Zero;
        m_preview_orbiting = false;
        m_preview_dirty = true;
    }
    if (
        m_preview_orbiting &&
        (
            io.MouseDelta.x != 0.0f ||
            io.MouseDelta.y != 0.0f
        )
    )
    {
        if (m_texture)
        {
            m_texture_pan.x += io.MouseDelta.x;
            m_texture_pan.y += io.MouseDelta.y;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        else
        {
            m_preview_yaw -= io.MouseDelta.x * 0.012f;
            m_preview_pitch = clamp(
                m_preview_pitch +
                io.MouseDelta.y * 0.012f,
                -1.45f,
                1.45f
            );
        }
        m_preview_dirty = true;
    }
    if (preview_hovered && io.MouseWheel != 0.0f)
    {
        const float zoom_previous = m_preview_zoom;
        const float step = io.MouseWheel > 0.0f
            ? 1.15f
            : 1.0f / 1.15f;
        m_preview_zoom = clamp(
            m_preview_zoom * step,
            m_texture ? 0.05f : 0.2f,
            m_texture ? 64.0f : 8.0f
        );
        // keep the texel under the cursor pinned while zooming
        if (m_texture && zoom_previous > 0.0f)
        {
            const float ratio =
                m_preview_zoom / zoom_previous - 1.0f;
            const float center_x =
                (minimum.x + maximum.x) * 0.5f +
                m_texture_pan.x;
            const float center_y =
                (minimum.y + maximum.y) * 0.5f +
                m_texture_pan.y;
            m_texture_pan.x -=
                (io.MousePos.x - center_x) * ratio;
            m_texture_pan.y -=
                (io.MousePos.y - center_y) * ratio;
        }
        m_preview_dirty = true;
    }

    if (m_texture)
    {
        DrawTexturePreview(minimum, maximum);
    }
    else if (
        !Renderer::IsSecondaryViewReady() ||
        !renderer_output
    )
    {
        ImGui::GetWindowDrawList()->AddRectFilled(
            minimum,
            maximum,
            IM_COL32(15, 18, 24, 255)
        );
        const char* title = PreviewRoot()
            ? "Preparing preview"
            : "No asset selected";
        const char* hint = PreviewRoot()
            ? "Loading geometry and materials"
            : "Choose an asset from the Library";
        const ImVec2 title_size =
            ImGui::CalcTextSize(title);
        const ImVec2 hint_size =
            ImGui::CalcTextSize(hint);
        const float center_y =
            (minimum.y + maximum.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(
                (minimum.x + maximum.x - title_size.x) *
                    0.5f,
                center_y - 16.0f * scale
            ),
            ImGui::GetColorU32(ImGuiCol_Text),
            title
        );
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(
                (minimum.x + maximum.x - hint_size.x) *
                    0.5f,
                center_y + 6.0f * scale
            ),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            hint
        );
    }
    if (
        m_preview_show_stats &&
        !m_texture &&
        Renderer::IsSecondaryViewReady()
    )
    {
        const ImVec2 text_size =
            ImGui::CalcTextSize(summary.c_str());
        const ImVec2 card_min(
            minimum.x + 10.0f * scale,
            maximum.y -
                text_size.y -
                22.0f * scale
        );
        const ImVec2 card_max(
            card_min.x + text_size.x + 16.0f * scale,
            maximum.y - 8.0f * scale
        );
        ImGui::GetWindowDrawList()->AddRectFilled(
            card_min,
            card_max,
            IM_COL32(10, 13, 18, 190),
            4.0f * scale
        );
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(
                card_min.x + 8.0f * scale,
                card_min.y + 7.0f * scale
            ),
            IM_COL32(225, 230, 235, 220),
            summary.c_str()
        );
    }
    // a pending capture owns the next secondary render, it asked for a size and a shading of its own
    // and a panel refresh in between would hand it the panel's frame instead
    if (
        m_preview_dirty &&
        !m_texture &&
        !Renderer::IsSecondaryScreenshotPending() &&
        chrono::steady_clock::now() >=
            m_next_preview_request
    )
    {
        RequestPreviewRender(
            preview_width,
            preview_height
        );
    }
    if (preview_hovered && !m_preview_orbiting)
    {
        ImGuiSp::tooltip(
            m_texture
                ? "Drag to pan\nWheel to zoom\nDouble click to fit"
                : "Drag to orbit\nWheel to zoom\nDouble click to frame"
        );
    }
    ImGui::EndChild();
}

void AssetViewer::DrawTexturePreview(
    const ImVec2& minimum,
    const ImVec2& maximum
)
{
    if (!m_texture)
    {
        return;
    }

    const float canvas_width =
        maximum.x - minimum.x;
    const float canvas_height =
        maximum.y - minimum.y;
    const float texture_width =
        static_cast<float>(m_texture->GetWidth());
    const float texture_height =
        static_cast<float>(m_texture->GetHeight());
    if (
        canvas_width <= 0.0f ||
        canvas_height <= 0.0f ||
        texture_width <= 0.0f ||
        texture_height <= 0.0f
    )
    {
        return;
    }

    ImDrawList* draw_list =
        ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        minimum,
        maximum,
        IM_COL32(15, 18, 24, 255)
    );

    const float fit_scale =
        min(
            canvas_width / texture_width,
            canvas_height / texture_height
        );
    const float draw_scale =
        fit_scale * m_preview_zoom;
    const ImVec2 image_size(
        texture_width * draw_scale,
        texture_height * draw_scale
    );
    const ImVec2 image_min(
        minimum.x +
            (canvas_width - image_size.x) * 0.5f +
            m_texture_pan.x,
        minimum.y +
            (canvas_height - image_size.y) * 0.5f +
            m_texture_pan.y
    );
    const ImVec2 image_max(
        image_min.x + image_size.x,
        image_min.y + image_size.y
    );
    draw_list->PushClipRect(
        minimum,
        maximum,
        true
    );
    draw_list->AddImage(
        reinterpret_cast<ImTextureID>(m_texture.get()),
        image_min,
        image_max
    );

    if (!m_preview_show_stats)
    {
        draw_list->PopClipRect();
        return;
    }

    const string dimensions =
        to_string(m_texture->GetWidth()) +
        " x " +
        to_string(m_texture->GetHeight());
    draw_list->AddText(
        ImVec2(
            minimum.x + 10.0f,
            maximum.y - 26.0f
        ),
        IM_COL32(225, 230, 235, 220),
        dimensions.c_str()
    );
    draw_list->PopClipRect();
}

bool AssetViewer::Refresh(string& error)
{
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before refreshing";
        return false;
    }
    RefreshCatalog(true);
    return true;
}

bool AssetViewer::ListAssets(
    const ListRequest& request,
    ListResult& result,
    string& error
)
{
    if (
        !m_working_modified &&
        !m_working_lods_built
    )
    {
        RefreshCatalog(false);
    }
    string type = lower_copy(request.type);
    if (type == "all")
    {
        type.clear();
    }
    if (
        !type.empty() &&
        type != "mesh" &&
        type != "material" &&
        type != "prefab" &&
        type != "texture"
    )
    {
        error = "type must be mesh, material, prefab or texture";
        return false;
    }

    const string sort_mode = lower_copy(request.sort);
    if (
        sort_mode != "name" &&
        sort_mode != "quality" &&
        sort_mode != "type"
    )
    {
        error = "sort must be name, quality or type";
        return false;
    }
    if (
        request.limit == 0 ||
        request.limit > 500
    )
    {
        error = "limit must be between 1 and 500";
        return false;
    }

    const string query = lower_copy(request.query);
    vector<const AssetEntry*> matches;
    for (const AssetEntry& asset : m_assets)
    {
        if (
            (!type.empty() && asset.type != type) ||
            (!request.include_disk_only && asset.disk_only)
        )
        {
            continue;
        }

        string searchable =
            asset.id + " " +
            asset.name + " " +
            asset.type + " " +
            join_strings(asset.aliases, " ") + " " +
            join_strings(asset.tags, " ");
        searchable = lower_copy(move(searchable));
        istringstream terms(query);
        string term;
        bool found = true;
        while (terms >> term)
        {
            if (searchable.find(term) == string::npos)
            {
                found = false;
                break;
            }
        }
        if (found)
        {
            matches.push_back(&asset);
        }
    }

    sort(
        matches.begin(),
        matches.end(),
        [this, &sort_mode](
            const AssetEntry* first,
            const AssetEntry* second
        )
        {
            if (sort_mode == "quality")
            {
                const float first_quality =
                    first->quality_score;
                const float second_quality =
                    second->quality_score;
                if (first_quality != second_quality)
                {
                    return first_quality > second_quality;
                }
            }
            else if (
                sort_mode == "type" &&
                first->type != second->type
            )
            {
                return first->type < second->type;
            }
            return lower_copy(first->name) <
                lower_copy(second->name);
        }
    );

    result = ListResult();
    result.total = matches.size();
    result.offset = min<uint64_t>(
        request.offset,
        result.total
    );
    result.limit = request.limit;
    const uint64_t end = min<uint64_t>(
        result.total,
        result.offset + result.limit
    );
    for (uint64_t index = result.offset; index < end; index++)
    {
        const AssetEntry& asset = *matches[index];
        AssetSummary summary;
        summary.id = asset.id;
        summary.name = asset.name;
        summary.type = asset.type;
        summary.path = asset.path;
        summary.source_path = asset.source_path;
        summary.thumbnail_path = asset.thumbnail_path;
        summary.quality_score = asset.quality_score;
        summary.quality_verified = asset.quality_verified;
        summary.disk_only = asset.disk_only;
        result.assets.emplace_back(move(summary));
    }
    return true;
}
bool AssetViewer::InspectAsset(
    const string& asset_id,
    AssetInspection& result,
    string& error
) const
{
    if (asset_id.empty())
    {
        error = "asset_id is required";
        return false;
    }

    const AssetEntry* asset = nullptr;
    for (const AssetEntry& candidate : m_assets)
    {
        if (candidate.id == asset_id)
        {
            asset = &candidate;
            break;
        }
    }
    if (!asset)
    {
        error = "asset was not found in the active catalog";
        return false;
    }

    result = AssetInspection();
    result.asset.id = asset->id;
    result.asset.name = asset->name;
    result.asset.type = asset->type;
    result.asset.path = asset->path;
    result.asset.source_path = asset->source_path;
    result.asset.thumbnail_path = asset->thumbnail_path;
    result.asset.quality_score = asset->quality_score;
    result.asset.quality_verified = asset->quality_verified;
    result.asset.disk_only = asset->disk_only;
    result.aliases = asset->aliases;
    result.tags = asset->tags;
    result.dependencies = asset->dependencies;
    result.source_exists = FileSystem::Exists(asset->path);
    if (result.source_exists)
    {
        error_code size_error;
        result.source_bytes = static_cast<uint64_t>(
            filesystem::file_size(
                filesystem::path(asset->path),
                size_error
            )
        );
        if (size_error)
        {
            result.source_bytes = 0;
        }
    }

    const bool inspecting_loaded =
        normalized_path(asset->path) ==
            normalized_path(m_loaded_path);
    if (inspecting_loaded)
    {
        const auto [vertex_count, index_count] =
            GetPreviewGeometryCounts();
        result.vertex_count = vertex_count;
        result.index_count = index_count;
        result.prefab_entity_count = m_prefab_entity_count;
        result.missing_dependencies = m_missing_dependencies;
        if (m_texture)
        {
            result.texture_width = m_texture->GetWidth();
            result.texture_height = m_texture->GetHeight();
            result.texture_channels = m_texture->GetChannelCount();
        }
    }
    return true;
}

bool AssetViewer::SetSelection(
    const SelectionRequest& request,
    string& error
)
{
    vector<int> indices;
    indices.reserve(request.asset_ids.size());
    for (const string& id : request.asset_ids)
    {
        int found = -1;
        for (size_t index = 0; index < m_assets.size(); index++)
        {
            if (m_assets[index].id == id)
            {
                found = static_cast<int>(index);
                break;
            }
        }
        if (found < 0)
        {
            error = "asset was not found: " + id;
            return false;
        }
        indices.push_back(found);
    }

    int focus = -1;
    if (request.focus_id)
    {
        for (size_t index = 0; index < m_assets.size(); index++)
        {
            if (m_assets[index].id == *request.focus_id)
            {
                focus = static_cast<int>(index);
                break;
            }
        }
        if (focus < 0)
        {
            error = "focus asset was not found";
            return false;
        }
    }
    else if (
        request.mode == SelectionMode::Replace &&
        !indices.empty()
    )
    {
        focus = indices.front();
    }

    unordered_set<string> next_selection =
        request.mode == SelectionMode::Replace
            ? unordered_set<string>()
            : m_selected_assets;
    for (const int index : indices)
    {
        const string& id = m_assets[index].id;
        if (request.mode == SelectionMode::Remove)
        {
            next_selection.erase(id);
        }
        else if (request.mode == SelectionMode::Toggle)
        {
            if (!next_selection.erase(id))
            {
                next_selection.insert(id);
            }
        }
        else
        {
            next_selection.insert(id);
        }
    }

    if (focus < 0)
    {
        const bool keeps_current =
            m_selected_asset >= 0 &&
            m_selected_asset < static_cast<int>(m_assets.size()) &&
            next_selection.count(
                m_assets[m_selected_asset].id
            );
        if (keeps_current)
        {
            focus = m_selected_asset;
        }
        else if (!next_selection.empty())
        {
            for (size_t index = 0; index < m_assets.size(); index++)
            {
                if (next_selection.count(m_assets[index].id))
                {
                    focus = static_cast<int>(index);
                    break;
                }
            }
        }
    }
    if (
        focus >= 0 &&
        (
            request.mode == SelectionMode::Remove ||
            request.mode == SelectionMode::Toggle
        ) &&
        !next_selection.count(m_assets[focus].id)
    )
    {
        error = "focus_id must remain selected";
        return false;
    }
    const bool changes_focus = focus != m_selected_asset;
    if (
        changes_focus &&
        (m_working_modified || m_working_lods_built)
    )
    {
        error =
            "save or revert unsaved mesh changes before changing focus";
        return false;
    }
    if (focus >= 0)
    {
        next_selection.insert(m_assets[focus].id);
    }

    m_selected_assets = move(next_selection);

    if (focus >= 0)
    {
        m_selected_asset = focus;
        m_selection_anchor = focus;
        m_selected_dependency_path.clear();
        if (changes_focus)
        {
            LoadSelectedAsset();
            if (m_loaded_path.empty())
            {
                error = m_status;
                return false;
            }
        }
    }
    else
    {
        m_selected_asset = -1;
        m_selection_anchor = -1;
        m_selected_dependency_path.clear();
        ClearLoadedAsset();
    }

    m_status =
        to_string(m_selected_assets.size()) +
        (
            m_selected_assets.size() == 1
                ? " asset selected"
                : " assets selected"
        );
    return true;
}

bool AssetViewer::SetDisplay(
    const DisplayRequest& request,
    string& error
)
{
    if (request.preview_lod)
    {
        if (!m_working_editable)
        {
            error = "a previewed editable mesh is required";
            return false;
        }
        if (!m_working_lods_scanned)
        {
            m_working_lods_scanned = true;
            LoadExistingLods();
        }
        const int lod_count = max(
            1,
            static_cast<int>(m_working_lods.size())
        );
        if (
            *request.preview_lod < 0 ||
            *request.preview_lod >= lod_count
        )
        {
            error = "preview_lod is out of range";
            return false;
        }
    }
    if (request.frame && !HasPreviewContent())
    {
        error = "load a preview before framing it";
        return false;
    }

    if (request.reset)
    {
        m_preview_mode = 0;
        m_preview_backdrop = 3;
        m_preview_show_stats = true;
        m_preview_auto_rotate = false;
        m_preview_yaw = 0.65f;
        m_preview_pitch = 0.35f;
        m_preview_zoom = 1.0f;
        m_preview_lod = 0;
        m_texture_pan = math::Vector2::Zero;
    }
    if (request.shading)
    {
        m_preview_mode = static_cast<int>(*request.shading);
    }
    if (request.backdrop)
    {
        m_preview_backdrop = static_cast<int>(*request.backdrop);
    }
    if (request.show_stats)
    {
        m_preview_show_stats = *request.show_stats;
    }
    if (request.auto_rotate)
    {
        m_preview_auto_rotate = *request.auto_rotate;
    }
    if (request.preview_lod)
    {
        m_preview_lod = *request.preview_lod;
        FlattenWorkingGeometry();
        RefreshPreviewMeshGeometry();
    }
    if (request.frame)
    {
        RefreshPreviewBounds();
        m_preview_zoom = 1.0f;
    }

    m_visible = true;
    m_preview_dirty = true;
    m_status = "Updated asset preview display";
    return true;
}

bool AssetViewer::PreviewPath(
    const string& path,
    string& error
)
{
    if (path.empty())
    {
        error = "path is required";
        return false;
    }
    if (!path_is_in_viewer_roots(path))
    {
        error =
            "path must be inside the asset library or mcp blockout";
        return false;
    }
    if (!FileSystem::Exists(path))
    {
        error = "linked resource was not found";
        return false;
    }
    if (m_working_modified || m_working_lods_built)
    {
        if (
            normalized_path(path) ==
            normalized_path(m_loaded_path)
        )
        {
            m_visible = true;
            m_status =
                "Preview already loaded with unsaved mesh changes";
            return true;
        }
        error =
            "save or revert unsaved mesh changes before changing preview";
        return false;
    }
    for (size_t index = 0; index < m_assets.size(); index++)
    {
        AssetEntry& asset = m_assets[index];
        if (
            normalized_path(asset.path) !=
            normalized_path(path)
        )
        {
            continue;
        }
        m_selected_asset = static_cast<int>(index);
        m_selected_assets.clear();
        m_selected_assets.insert(asset.id);
        m_selection_anchor = m_selected_asset;
        LoadSelectedAsset();
        if (m_loaded_path.empty())
        {
            error = m_status;
            return false;
        }
        m_visible = true;
        return true;
    }
    const string type = asset_type_from_path(path);
    if (
        type != "mesh" &&
        type != "material" &&
        type != "texture"
    )
    {
        error = "linked resource type is not previewable";
        return false;
    }
    LoadDependencyPreview(path);
    if (m_loaded_path.empty())
    {
        error = m_status;
        return false;
    }
    m_visible = true;
    return true;
}

bool AssetViewer::Reload(string& error)
{
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before reloading";
        return false;
    }
    if (!m_selected_dependency_path.empty())
    {
        const string path = m_selected_dependency_path;
        LoadDependencyPreview(path);
    }
    else if (
        m_selected_asset >= 0 &&
        m_selected_asset < static_cast<int>(m_assets.size())
    )
    {
        LoadSelectedAsset(false, true);
    }
    else
    {
        error = "select an asset or linked path before reloading";
        return false;
    }
    if (m_loaded_path.empty())
    {
        error = m_status;
        return false;
    }
    return true;
}

bool AssetViewer::EditMesh(
    const MeshRequest& request,
    string& error
)
{
    if (
        !m_working_editable ||
        m_working_sub_meshes.empty()
    )
    {
        error = "a previewed editable mesh or prefab is required";
        return false;
    }
    if (
        request.target_ratio &&
        (
            *request.target_ratio < 0.01f ||
            *request.target_ratio > 1.0f
        )
    )
    {
        error = "target_ratio must be between 0.01 and 1";
        return false;
    }
    if (
        request.target_ratio &&
        request.action != MeshAction::Simplify &&
        request.action != MeshAction::SetOptions
    )
    {
        error =
            "target_ratio is only valid for simplify or set_options";
        return false;
    }
    if (
        request.generate_lods &&
        request.action != MeshAction::SetOptions
    )
    {
        error =
            "generate_lods_on_save is only valid for set_options";
        return false;
    }
    if (
        request.preview_lod &&
        request.action != MeshAction::SetOptions
    )
    {
        error =
            "preview_lod is only valid for set_options";
        return false;
    }
    if (
        request.action == MeshAction::Revert &&
        !request.confirm
    )
    {
        error =
            "confirm=true is required to discard mesh changes";
        return false;
    }
    if (request.preview_lod)
    {
        DisplayRequest display;
        display.preview_lod = request.preview_lod;
        if (!SetDisplay(display, error))
        {
            return false;
        }
    }
    if (request.target_ratio)
    {
        m_target_ratio = *request.target_ratio;
    }
    if (request.generate_lods)
    {
        m_working_generate_lods = *request.generate_lods;
    }

    switch (request.action)
    {
        case MeshAction::Simplify:
            SimplifyWorkingGeometry(m_target_ratio);
            m_status = "Simplified working mesh geometry";
            break;
        case MeshAction::Optimize:
            OptimizeWorkingGeometry();
            m_status = "Optimized working mesh geometry";
            break;
        case MeshAction::BuildLods:
            BuildWorkingLods();
            if (m_working_lods.empty())
            {
                error = "the mesh could not produce a reduced lod level";
                return false;
            }
            m_status = "Built working mesh lods";
            break;
        case MeshAction::Revert:
            LoadWorkingGeometry();
            m_status = "Reverted unsaved mesh changes";
            break;
        case MeshAction::SetOptions:
            m_status = "Updated mesh edit options";
            break;
    }

    return true;
}

bool AssetViewer::SaveMesh(
    const bool confirm,
    string& error
)
{
    if (!confirm)
    {
        error = "confirm=true is required to overwrite the mesh";
        return false;
    }
    if (
        !m_working_modified &&
        !m_working_lods_built
    )
    {
        error = "there are no unsaved mesh changes";
        return false;
    }
    if (!SaveWorkingGeometry())
    {
        error =
            m_status.empty()
                ? "mesh changes could not be saved"
                : m_status;
        return false;
    }
    return true;
}

bool AssetViewer::Rename(
    const string& asset_id,
    const string& linked_path,
    const string& new_name,
    string& error
)
{
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before renaming";
        return false;
    }
    if (new_name.empty())
    {
        error = "new_name is required";
        return false;
    }
    if (asset_id.empty() == linked_path.empty())
    {
        error = "provide exactly one of asset_id or linked_path";
        return false;
    }

    if (!linked_path.empty())
    {
        const string library_root =
            FileSystem::GetDirectoryFromFilePath(m_catalog_path);
        if (
            linked_path.find("..") != string::npos ||
            library_root.empty() ||
            !path_is_within(linked_path, library_root) ||
            !resolved_path_is_within(
                linked_path,
                library_root
            )
        )
        {
            error = "linked_path must be inside the active asset library";
            return false;
        }
        if (
            (m_working_modified || m_working_lods_built) &&
            normalized_path(linked_path) ==
                normalized_path(m_loaded_path)
        )
        {
            error =
                "save or revert unsaved mesh changes before renaming it";
            return false;
        }
        const string renamed_path =
            FileSystem::GetDirectoryFromFilePath(linked_path) +
            sanitize_asset_name(new_name) +
            FileSystem::GetExtensionFromFilePath(linked_path);
        const bool previewing =
            normalized_path(linked_path) ==
            normalized_path(m_loaded_path);
        if (!RenameAssetFile(linked_path, new_name))
        {
            error = m_status;
            return false;
        }
        const string status = m_status;
        RefreshCatalog(true);
        if (previewing && FileSystem::Exists(renamed_path))
        {
            LoadDependencyPreview(renamed_path);
        }
        m_status = status;
        return true;
    }

    int index = -1;
    for (size_t candidate = 0; candidate < m_assets.size(); candidate++)
    {
        if (m_assets[candidate].id == asset_id)
        {
            index = static_cast<int>(candidate);
            break;
        }
    }
    if (index < 0)
    {
        error = "asset was not found in the active catalog";
        return false;
    }
    if (
        (m_working_modified || m_working_lods_built) &&
        m_selected_asset == index
    )
    {
        error =
            "save or revert unsaved mesh changes before renaming it";
        return false;
    }
    if (!RenameAsset(index, new_name))
    {
        error = m_status;
        return false;
    }
    return true;
}

bool AssetViewer::Delete(
    const vector<string>& asset_ids,
    const string& linked_path,
    const bool confirm,
    string& error
)
{
    if (!confirm)
    {
        error = "confirm=true is required to delete assets";
        return false;
    }
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before deleting";
        return false;
    }
    if (asset_ids.empty() == linked_path.empty())
    {
        error = "provide asset_ids or linked_path, but not both";
        return false;
    }

    if (!linked_path.empty())
    {
        const string library_root =
            FileSystem::GetDirectoryFromFilePath(m_catalog_path);
        if (
            linked_path.find("..") != string::npos ||
            library_root.empty() ||
            !path_is_within(linked_path, library_root) ||
            !resolved_path_is_within(
                linked_path,
                library_root
            )
        )
        {
            error = "linked_path must be inside the active asset library";
            return false;
        }
        if (!FileSystem::Exists(linked_path))
        {
            error = "linked file was not found";
            return false;
        }
        if (
            (m_working_modified || m_working_lods_built) &&
            normalized_path(linked_path) ==
                normalized_path(m_loaded_path)
        )
        {
            error =
                "save or revert unsaved mesh changes before deleting it";
            return false;
        }
        FileSystem::Delete(linked_path);
        if (FileSystem::Exists(linked_path))
        {
            error = "failed to delete linked file";
            return false;
        }
        if (
            normalized_path(m_loaded_path) ==
            normalized_path(linked_path)
        )
        {
            ClearLoadedAsset();
        }
        RefreshCatalog(true);
        m_status = "Deleted " + linked_path;
        return true;
    }

    for (const string& id : asset_ids)
    {
        const auto found = find_if(
            m_assets.begin(),
            m_assets.end(),
            [&id](const AssetEntry& asset)
            {
                return asset.id == id;
            }
        );
        if (found == m_assets.end())
        {
            error = "asset was not found: " + id;
            return false;
        }
        if (
            (m_working_modified || m_working_lods_built) &&
            normalized_path(found->path) ==
                normalized_path(m_loaded_path)
        )
        {
            error =
                "save or revert unsaved mesh changes before deleting it";
            return false;
        }
    }
    if (!DeleteAssets(asset_ids))
    {
        error = m_status;
        return false;
    }
    return true;
}

bool AssetViewer::ScanCleanup(
    CleanupSummary& result,
    string& error
)
{
    ScanLibraryCleanup();
    if (!m_cleanup.error.empty())
    {
        error = m_cleanup.error;
        return false;
    }

    result = CleanupSummary();
    result.orphan_files = m_cleanup.orphan_files;
    result.bytes = m_cleanup.bytes;
    result.generation = m_cleanup_generation;
    return true;
}

bool AssetViewer::ApplyCleanup(
    const uint64_t generation,
    const bool confirm,
    string& error
)
{
    if (!confirm)
    {
        error = "confirm=true is required to apply cleanup";
        return false;
    }
    if (
        generation == 0 ||
        generation != m_cleanup_generation
    )
    {
        error =
            "cleanup generation is stale, scan again before applying";
        return false;
    }
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before applying cleanup";
        return false;
    }
    RefreshCatalog(false);
    if (!m_cleanup.scanned)
    {
        error = "run asset_viewer_cleanup_scan before applying cleanup";
        return false;
    }
    if (generation != m_cleanup_generation)
    {
        error =
            "cleanup generation is stale, scan again before applying";
        return false;
    }
    if (
        CleanupFileSignatures() !=
        m_cleanup.file_signatures
    )
    {
        error =
            "cleanup files changed since the scan, scan again";
        return false;
    }
    if (!ApplyLibraryCleanup())
    {
        error =
            m_cleanup.error.empty()
                ? m_status
                : m_cleanup.error;
        return false;
    }
    m_cleanup_generation++;
    return true;
}

void AssetViewer::SetPanelVisible(bool visible)
{
    m_visible = visible;
    if (!m_visible)
    {
        // the preview survives the panel being hidden, see OnInvisible, a driver that closes the panel is
        // not saying it is finished with the asset it was reviewing
        Renderer::InvalidateSecondaryView();
    }
    if (!m_working_modified && !m_working_lods_built)
    {
        RefreshCatalog(m_visible);
    }
}

bool AssetViewer::SelectAsset(
    const string& query,
    string& error
)
{
    if (m_working_modified || m_working_lods_built)
    {
        const bool same_asset =
            m_selected_asset >= 0 &&
            m_selected_asset < static_cast<int>(m_assets.size()) &&
            (
                m_assets[m_selected_asset].id == query ||
                m_assets[m_selected_asset].name == query
            );
        if (same_asset)
        {
            m_visible = true;
            return true;
        }
        error =
            "save or revert unsaved mesh changes before selecting another asset";
        return false;
    }

    m_visible = true;
    RefreshCatalog(true);
    if (query.empty())
    {
        error = "asset_id or name is required";
        return false;
    }

    m_selected_asset = -1;
    for (size_t index = 0; index < m_assets.size(); index++)
    {
        const AssetEntry& asset = m_assets[index];
        if (
            asset.id == query ||
            asset.name == query
        )
        {
            m_selected_asset = static_cast<int>(index);
            break;
        }
    }
    if (m_selected_asset < 0)
    {
        error = "asset was not found in the active catalog";
        return false;
    }

    m_selected_assets.clear();
    m_selected_assets.insert(
        m_assets[m_selected_asset].id
    );
    m_selection_anchor = m_selected_asset;
    LoadSelectedAsset(true, true);
    if (m_loaded_path.empty())
    {
        error = m_status;
        return false;
    }
    return true;
}

bool AssetViewer::PreviewEntityById(
    uint64_t entity_id,
    string& error
)
{
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before previewing an entity";
        return false;
    }
    Entity* entity = World::GetEntityById(entity_id);
    if (!entity)
    {
        error = "preview entity was not found";
        return false;
    }

    PreviewEntity(entity);
    return true;
}

void AssetViewer::SetPreviewView(const ViewRequest& request)
{
    if (request.view)
    {
        switch (*request.view)
        {
            case PreviewView::Front:
                m_preview_yaw = 0.0f;
                m_preview_pitch = 0.0f;
                break;
            case PreviewView::Back:
                m_preview_yaw = 3.14159265f;
                m_preview_pitch = 0.0f;
                break;
            case PreviewView::Left:
                m_preview_yaw = -1.57079633f;
                m_preview_pitch = 0.0f;
                break;
            case PreviewView::Right:
                m_preview_yaw = 1.57079633f;
                m_preview_pitch = 0.0f;
                break;
            case PreviewView::Top:
                m_preview_yaw = 0.0f;
                m_preview_pitch = 1.45f;
                break;
            case PreviewView::Bottom:
                m_preview_yaw = 0.0f;
                m_preview_pitch = -1.45f;
                break;
            case PreviewView::Perspective:
                m_preview_yaw = 0.65f;
                m_preview_pitch = 0.35f;
                break;
        }
    }

    // an explicit angle wins over the preset, a caller can nudge one axis of a named view
    if (request.yaw)
    {
        m_preview_yaw = *request.yaw;
    }
    if (request.pitch)
    {
        m_preview_pitch = clamp(
            *request.pitch,
            -1.45f,
            1.45f
        );
    }
    if (request.zoom)
    {
        m_preview_zoom = clamp(
            *request.zoom,
            0.2f,
            8.0f
        );
    }

    m_visible = true;
    m_preview_dirty = true;
}

bool AssetViewer::HasPreviewContent() const
{
    return
        m_mesh ||
        !m_working_meshes.empty() ||
        m_material ||
        m_texture ||
        PreviewRoot();
}

bool AssetViewer::CapturePreview(
    const CaptureRequest& request,
    CaptureResult& result,
    string& error
)
{
    if (!HasPreviewContent())
    {
        error =
            "load an asset preview before taking a screenshot";
        return false;
    }

    result.width = clamp(request.width, 256u, 2048u);
    result.height = clamp(request.height, 256u, 2048u);

    // the render lands on a later frame, so the caller has nothing to look at except this file. an older
    // capture at the same path has to be gone before the request is made, otherwise whoever is waiting for
    // the image reads the previous one and believes it is looking at the asset as it is now. the previous
    // capture is saved on a worker thread and can still hold the file, which is why the delete is checked
    if (FileSystem::Exists(request.path))
    {
        FileSystem::Delete(request.path);
        if (FileSystem::Exists(request.path))
        {
            error =
                "the previous screenshot at this path is still being written, "
                "capture again in a moment or use a different path";
            return false;
        }
    }

    const int mode = static_cast<int>(request.shading);
    const int backdrop = static_cast<int>(request.backdrop);
    const int mode_restore = m_preview_mode;
    const int backdrop_restore = m_preview_backdrop;
    m_preview_mode = mode;
    m_preview_backdrop = backdrop;
    const bool saved = SavePreviewScreenshot(
        request.path,
        result.width,
        result.height
    );
    m_preview_mode = mode_restore;
    m_preview_backdrop = backdrop_restore;

    // the request the renderer holds already carries the capture settings, marking the panel dirty
    // brings it back to what the user had once that capture has been consumed
    if (
        mode != mode_restore ||
        backdrop != backdrop_restore
    )
    {
        m_preview_dirty = true;
    }
    if (!saved)
    {
        error =
            "asset preview screenshot could not be saved";
        return false;
    }

    // a texture is written from the copy already in memory, geometry needs a render first
    result.ready = m_texture != nullptr;
    return true;
}

AssetViewer::PreviewStatus AssetViewer::GetPreviewStatus() const
{
    PreviewStatus status;
    status.visible = m_visible;
    status.loaded_path = m_loaded_path;
    status.dependency_path = m_selected_dependency_path;
    status.catalog_path = m_catalog_path;
    status.catalog_count = m_assets.size();
    status.status_message = m_status;
    status.yaw = m_preview_yaw;
    status.pitch = m_preview_pitch;
    status.zoom = m_preview_zoom;
    status.shading =
        static_cast<PreviewShading>(m_preview_mode);
    status.backdrop =
        static_cast<PreviewBackdrop>(m_preview_backdrop);
    status.show_stats = m_preview_show_stats;
    status.auto_rotate = m_preview_auto_rotate;
    status.preview_lod = m_preview_lod;
    status.lod_count = max(
        1,
        static_cast<int>(m_working_lods.size())
    );
    status.mesh_target_ratio = m_target_ratio;
    status.mesh_editable = m_working_editable;
    status.mesh_modified = m_working_modified;
    status.mesh_lods_built = m_working_lods_built;
    status.mesh_lods_attempted = m_working_lods_attempted;
    status.mesh_generate_lods = m_working_generate_lods;
    status.has_preview_content = HasPreviewContent();
    status.selected_asset_ids.assign(
        m_selected_assets.begin(),
        m_selected_assets.end()
    );
    sort(
        status.selected_asset_ids.begin(),
        status.selected_asset_ids.end()
    );
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        status.mesh_source_vertices +=
            working.source_vertex_count;
        status.mesh_source_indices +=
            working.source_index_count;
        status.mesh_working_vertices += working.vertices.size();
        status.mesh_working_indices += working.indices.size();
    }

    if (
        m_selected_asset >= 0 &&
        m_selected_asset < static_cast<int>(m_assets.size())
    )
    {
        const AssetEntry& asset = m_assets[m_selected_asset];
        status.selected_asset_id = asset.id;
        status.selected_asset_name = asset.name;
        if (
            find(
                status.selected_asset_ids.begin(),
                status.selected_asset_ids.end(),
                asset.id
            ) == status.selected_asset_ids.end()
        )
        {
            status.selected_asset_ids.push_back(asset.id);
            sort(
                status.selected_asset_ids.begin(),
                status.selected_asset_ids.end()
            );
        }
    }

    if (PreviewRoot() && !m_preview_root_owned)
    {
        status.previewed_entity_id = m_preview_root_id;
    }

    const auto [vertex_count, index_count] =
        GetPreviewGeometryCounts();
    status.vertex_count = vertex_count;
    status.index_count = index_count;
    return status;
}

bool AssetViewer::SavePreviewScreenshot(
    const string& path,
    const uint32_t width,
    const uint32_t height
)
{
    // a texture is already an image, resampling it would only lose detail
    if (m_texture && !m_loaded_path.empty())
    {
        const string directory =
            FileSystem::GetDirectoryFromFilePath(path);
        if (!directory.empty())
        {
            FileSystem::CreateDirectory_(directory);
        }

        std::error_code error;
        std::filesystem::copy_file(
            m_loaded_path,
            path,
            std::filesystem::copy_options::overwrite_existing,
            error
        );
        return !error && FileSystem::Exists(path);
    }

    if (!PreviewRoot())
    {
        return false;
    }

    // the capture is satisfied by the first secondary frame at or after the generation it was registered
    // against, and the renderer consumes requests on its own thread. registering the capture after its render
    // had already been consumed leaves it waiting for a frame that is in the past, and nothing re-requests
    // one unless the panel happens to be open and dirty, so a closed panel waits forever
    //
    // so the render is asked for, the capture is registered against it, and one more render is queued behind
    // the capture, which is the frame that is guaranteed to arrive after it and write the file
    if (!RequestPreviewRender(width, height))
    {
        return false;
    }
    if (!Renderer::ScreenshotSecondary(path))
    {
        return false;
    }
    return RequestPreviewRender(width, height);
}

bool AssetViewer::AssetMatchesFilter(
    const AssetEntry& asset
) const
{
    if (
        m_type_filter == 1 &&
        asset.type != "mesh"
    )
    {
        return false;
    }
    if (
        m_type_filter == 2 &&
        asset.type != "material"
    )
    {
        return false;
    }
    if (
        m_type_filter == 3 &&
        asset.type != "prefab"
    )
    {
        return false;
    }
    if (
        m_type_filter == 4 &&
        asset.type != "texture"
    )
    {
        return false;
    }

    const string query = lower_copy(m_search.data());

    // packed maps are hidden rather than removed, searching for them is the way back in, the file
    // on disk is checked as well because a catalog entry can carry any display name it likes
    if (query.find("packed") == string::npos)
    {
        if (
            is_packed_texture(asset.id) ||
            is_packed_texture(asset.name) ||
            is_packed_texture(asset.path)
        )
        {
            return false;
        }
    }

    if (query.empty())
    {
        return true;
    }

    string searchable =
        asset.id +
        " " +
        asset.name +
        " " +
        asset.type +
        " " +
        join_strings(asset.aliases, " ") +
        " " +
        join_strings(asset.tags, " ");
    searchable = lower_copy(move(searchable));

    istringstream stream(query);
    string term;
    while (stream >> term)
    {
        if (searchable.find(term) == string::npos)
        {
            return false;
        }
    }
    return true;
}