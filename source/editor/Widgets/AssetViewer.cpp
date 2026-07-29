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
#include "../ImGui/ImGui_Extension.h"
#include "../ImGui/ImGui_Style.h"
#include "../ImGui/Source/imgui_stdlib.h"
#include "FileSystem/FileSystem.h"
#include "Geometry/GeometryProcessing.h"
#include "Geometry/Mesh.h"
#include "IO/pugixml.hpp"
#include "RHI/RHI_Texture.h"
#include "Rendering/Material.h"
#include "Rendering/Renderer.h"
#include "Resource/ResourceCache.h"
#include "World/Entity.h"
#include "World/Prefab.h"
#include "World/World.h"
#include "World/Components/Camera.h"
#include "World/Components/Light.h"
#include "World/Components/Render.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <sstream>
//======================================

using namespace std;
using namespace spartan;

namespace
{
    struct JsonValue
    {
        enum class Type
        {
            Null,
            Boolean,
            Number,
            String,
            Array,
            Object
        };

        const JsonValue* Find(const string& key) const
        {
            const auto iterator = object.find(key);
            return iterator == object.end() ? nullptr : &iterator->second;
        }

        string String(const string& fallback = "") const
        {
            return type == Type::String ? text : fallback;
        }

        double Number(double fallback = 0.0) const
        {
            return type == Type::Number ? number : fallback;
        }

        bool Boolean(bool fallback = false) const
        {
            return type == Type::Boolean ? boolean : fallback;
        }

        Type type = Type::Null;
        bool boolean = false;
        double number = 0.0;
        string text;
        vector<JsonValue> array;
        map<string, JsonValue> object;
    };

    class JsonParser
    {
    public:
        explicit JsonParser(const string& source)
        {
            m_source = &source;
        }

        bool Parse(JsonValue& value, string& error)
        {
            SkipWhitespace();
            if (!ParseValue(value))
            {
                error = m_error;
                return false;
            }

            SkipWhitespace();
            if (m_position != m_source->size())
            {
                error = "unexpected data after catalog root";
                return false;
            }

            return true;
        }

    private:
        void SkipWhitespace()
        {
            while (
                m_position < m_source->size() &&
                isspace(
                    static_cast<unsigned char>(
                        (*m_source)[m_position]
                    )
                )
            )
            {
                m_position++;
            }
        }

        bool ParseValue(JsonValue& value)
        {
            SkipWhitespace();
            if (m_position >= m_source->size())
            {
                return Fail("unexpected end of catalog");
            }

            const char token = (*m_source)[m_position];
            if (token == '{')
            {
                return ParseObject(value);
            }
            if (token == '[')
            {
                return ParseArray(value);
            }
            if (token == '"')
            {
                value.type = JsonValue::Type::String;
                return ParseString(value.text);
            }
            if (token == 't' || token == 'f')
            {
                return ParseBoolean(value);
            }
            if (token == 'n')
            {
                return ParseNull(value);
            }
            if (token == '-' || isdigit(static_cast<unsigned char>(token)))
            {
                return ParseNumber(value);
            }

            return Fail("unexpected catalog token");
        }

        bool ParseObject(JsonValue& value)
        {
            value.type = JsonValue::Type::Object;
            m_position++;
            SkipWhitespace();
            if (Consume('}'))
            {
                return true;
            }

            while (m_position < m_source->size())
            {
                string key;
                if (!ParseString(key))
                {
                    return false;
                }
                SkipWhitespace();
                if (!Consume(':'))
                {
                    return Fail("expected colon after catalog key");
                }

                JsonValue child;
                if (!ParseValue(child))
                {
                    return false;
                }
                value.object.emplace(move(key), move(child));

                SkipWhitespace();
                if (Consume('}'))
                {
                    return true;
                }
                if (!Consume(','))
                {
                    return Fail("expected comma in catalog object");
                }
                SkipWhitespace();
            }

            return Fail("unterminated catalog object");
        }

        bool ParseArray(JsonValue& value)
        {
            value.type = JsonValue::Type::Array;
            m_position++;
            SkipWhitespace();
            if (Consume(']'))
            {
                return true;
            }

            while (m_position < m_source->size())
            {
                JsonValue child;
                if (!ParseValue(child))
                {
                    return false;
                }
                value.array.emplace_back(move(child));

                SkipWhitespace();
                if (Consume(']'))
                {
                    return true;
                }
                if (!Consume(','))
                {
                    return Fail("expected comma in catalog array");
                }
                SkipWhitespace();
            }

            return Fail("unterminated catalog array");
        }

        bool ParseString(string& value)
        {
            if (!Consume('"'))
            {
                return Fail("expected catalog string");
            }

            value.clear();
            while (m_position < m_source->size())
            {
                const char character = (*m_source)[m_position++];
                if (character == '"')
                {
                    return true;
                }
                if (character != '\\')
                {
                    value += character;
                    continue;
                }
                if (m_position >= m_source->size())
                {
                    return Fail("unterminated catalog escape");
                }

                const char escaped = (*m_source)[m_position++];
                switch (escaped)
                {
                    case '"':  value += '"';  break;
                    case '\\': value += '\\'; break;
                    case '/':  value += '/';  break;
                    case 'b':  value += '\b'; break;
                    case 'f':  value += '\f'; break;
                    case 'n':  value += '\n'; break;
                    case 'r':  value += '\r'; break;
                    case 't':  value += '\t'; break;
                    case 'u':
                    {
                        if (m_position + 4 > m_source->size())
                        {
                            return Fail("invalid catalog unicode escape");
                        }
                        value += '?';
                        m_position += 4;
                        break;
                    }
                    default:
                        return Fail("invalid catalog string escape");
                }
            }

            return Fail("unterminated catalog string");
        }

        bool ParseNumber(JsonValue& value)
        {
            const size_t start = m_position;
            if ((*m_source)[m_position] == '-')
            {
                m_position++;
            }
            while (
                m_position < m_source->size() &&
                isdigit(
                    static_cast<unsigned char>(
                        (*m_source)[m_position]
                    )
                )
            )
            {
                m_position++;
            }
            if (
                m_position < m_source->size() &&
                (*m_source)[m_position] == '.'
            )
            {
                m_position++;
                while (
                    m_position < m_source->size() &&
                    isdigit(
                        static_cast<unsigned char>(
                            (*m_source)[m_position]
                        )
                    )
                )
                {
                    m_position++;
                }
            }
            if (
                m_position < m_source->size() &&
                (
                    (*m_source)[m_position] == 'e' ||
                    (*m_source)[m_position] == 'E'
                )
            )
            {
                m_position++;
                if (
                    m_position < m_source->size() &&
                    (
                        (*m_source)[m_position] == '+' ||
                        (*m_source)[m_position] == '-'
                    )
                )
                {
                    m_position++;
                }
                while (
                    m_position < m_source->size() &&
                    isdigit(
                        static_cast<unsigned char>(
                            (*m_source)[m_position]
                        )
                    )
                )
                {
                    m_position++;
                }
            }

            try
            {
                value.type = JsonValue::Type::Number;
                value.number = stod(
                    m_source->substr(
                        start,
                        m_position - start
                    )
                );
                return true;
            }
            catch (...)
            {
                return Fail("invalid catalog number");
            }
        }

        bool ParseBoolean(JsonValue& value)
        {
            if (m_source->compare(m_position, 4, "true") == 0)
            {
                m_position += 4;
                value.type = JsonValue::Type::Boolean;
                value.boolean = true;
                return true;
            }
            if (m_source->compare(m_position, 5, "false") == 0)
            {
                m_position += 5;
                value.type = JsonValue::Type::Boolean;
                value.boolean = false;
                return true;
            }

            return Fail("invalid catalog boolean");
        }

        bool ParseNull(JsonValue& value)
        {
            if (m_source->compare(m_position, 4, "null") != 0)
            {
                return Fail("invalid catalog null");
            }

            m_position += 4;
            value.type = JsonValue::Type::Null;
            return true;
        }

        bool Consume(char character)
        {
            if (
                m_position >= m_source->size() ||
                (*m_source)[m_position] != character
            )
            {
                return false;
            }

            m_position++;
            return true;
        }

        bool Fail(const char* message)
        {
            m_error =
                string(message) +
                " at byte " +
                to_string(m_position);
            return false;
        }

        const string* m_source = nullptr;
        size_t m_position = 0;
        string m_error;
    };

    vector<string> json_strings(const JsonValue* value)
    {
        vector<string> strings;
        if (!value || value->type != JsonValue::Type::Array)
        {
            return strings;
        }

        for (const JsonValue& item : value->array)
        {
            if (item.type == JsonValue::Type::String)
            {
                strings.emplace_back(item.text);
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
        case JsonValue::Type::Null:
            return "null";
        case JsonValue::Type::Boolean:
            return value.boolean ? "true" : "false";
        case JsonValue::Type::Number:
        {
            ostringstream stream;
            stream.precision(17);
            stream << value.number;
            return stream.str();
        }
        case JsonValue::Type::String:
            return serialize_json_string(value.text);
        case JsonValue::Type::Array:
        {
            if (value.array.empty())
            {
                return "[]";
            }
            string result = "[\n";
            for (size_t index = 0; index < value.array.size(); index++)
            {
                result +=
                    child_indentation +
                    serialize_json(value.array[index], depth + 1);
                result +=
                    index + 1 < value.array.size() ?
                    ",\n" :
                    "\n";
            }
            return result + indentation + "]";
        }
        case JsonValue::Type::Object:
        {
            if (value.object.empty())
            {
                return "{}";
            }
            string result = "{\n";
            size_t index = 0;
            for (const auto& [key, child] : value.object)
            {
                result +=
                    child_indentation +
                    serialize_json_string(key) +
                    ": " +
                    serialize_json(child, depth + 1);
                result +=
                    ++index < value.object.size() ?
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
        if (
            !filesystem::path(value).is_absolute() &&
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

AssetViewer::AssetViewer(Editor* editor) : Widget(editor)
{
    m_title        = "Asset Viewer";
    m_visible      = false;
    m_size_initial = math::Vector2(1280.0f, 760.0f);
    m_size_min     = math::Vector2(720.0f, 480.0f);
    m_next_refresh_check = chrono::steady_clock::now();
}

AssetViewer::~AssetViewer()
{
    DestroyPreviewScene();
}

void AssetViewer::OnVisible()
{
    RefreshCatalog(true);
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
        // unsaved mesh edits win over the auto reload, otherwise a background
        // write would silently throw the edits away
        if (
            !m_loaded_path.empty() &&
            !m_working_modified &&
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

    DrawToolbar();
    ImGui::Spacing();

    const float status_height =
        ImGui::GetTextLineHeightWithSpacing() +
        8.0f * ui_scale();
    const float available_width =
        ImGui::GetContentRegionAvail().x;
    const float available_height =
        max(
            1.0f,
            ImGui::GetContentRegionAvail().y -
            status_height
        );
    const float splitter_width = 5.0f * ui_scale();
    const bool compact_layout =
        available_width < 1040.0f * ui_scale();

    m_library_width = clamp(
        m_library_width,
        220.0f * ui_scale(),
        min(
            360.0f * ui_scale(),
            available_width * 0.42f
        )
    );
    DrawAssetList(m_library_width, available_height);
    ImGui::SameLine(0.0f, 0.0f);
    horizontal_splitter(
        "##asset_library_splitter",
        m_library_width,
        220.0f * ui_scale(),
        360.0f * ui_scale(),
        available_height,
        1.0f
    );
    ImGui::SameLine(0.0f, 0.0f);

    if (compact_layout)
    {
        ImGui::BeginGroup();
        const float details_height = clamp(
            available_height * 0.38f,
            190.0f * ui_scale(),
            330.0f * ui_scale()
        );
        DrawPreview(
            0.0f,
            max(
                170.0f * ui_scale(),
                available_height - details_height
            )
        );
        DrawDetails(details_height);
        ImGui::EndGroup();
    }
    else
    {
        m_inspector_width = clamp(
            m_inspector_width,
            300.0f * ui_scale(),
            min(
                420.0f * ui_scale(),
                available_width * 0.36f
            )
        );
        const float preview_width =
            available_width -
            m_library_width -
            m_inspector_width -
            splitter_width * 2.0f;
        ImGui::BeginGroup();
        DrawPreview(preview_width, available_height);
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 0.0f);
        horizontal_splitter(
            "##asset_inspector_splitter",
            m_inspector_width,
            300.0f * ui_scale(),
            420.0f * ui_scale(),
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
        World::GetGeneratedResourceDirectory() +
        "catalog.json";
    // the directories are part of the signature, files can land on disk without the
    // catalog ever being rewritten
    string write_time =
        FileSystem::Exists(catalog_path) ?
        FileSystem::GetLastWriteTime(catalog_path) :
        "";
    for (
        const char* folder :
        { "meshes", "materials", "textures", "prefabs" }
    )
    {
        const string directory =
            FileSystem::GetDirectoryFromFilePath(catalog_path) +
            folder;
        if (FileSystem::Exists(directory))
        {
            write_time +=
                "|" +
                FileSystem::GetLastWriteTime(directory);
        }
    }

    if (
        !force &&
        world_file_path == m_world_file_path &&
        catalog_path == m_catalog_path &&
        write_time == m_catalog_write_time
    )
    {
        return;
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
        JsonParser parser(source);
        if (!parser.Parse(root, parse_error))
        {
            catalog_error = "invalid catalog, " + parse_error;
        }
        else
        {
            const JsonValue* schema_version =
                root.Find("schema_version");
            assets = root.Find("assets");
            if (
                root.type != JsonValue::Type::Object ||
                !schema_version ||
                static_cast<int>(schema_version->Number()) != 1 ||
                !assets ||
                assets->type != JsonValue::Type::Object
            )
            {
                catalog_error =
                    "unsupported or invalid asset catalog schema";
                assets = nullptr;
            }
        }
    }

    static const map<string, JsonValue> no_assets;
    for (
        const auto& [catalog_id, value] :
        assets ? assets->object : no_assets
    )
    {
        if (value.type != JsonValue::Type::Object)
        {
            continue;
        }

        AssetEntry asset;
        asset.id =
            value.Find("id") ?
            value.Find("id")->String(catalog_id) :
            catalog_id;
        asset.name =
            value.Find("name") ?
            value.Find("name")->String(asset.id) :
            asset.id;
        asset.type =
            value.Find("type") ?
            lower_copy(value.Find("type")->String()) :
            "";
        asset.active_version =
            value.Find("active_version") ?
            value.Find("active_version")->String() :
            "";
        asset.aliases = json_strings(value.Find("aliases"));
        asset.tags = json_strings(value.Find("tags"));

        const JsonValue* versions = value.Find("versions");
        if (
            versions &&
            versions->type == JsonValue::Type::Array
        )
        {
            for (const JsonValue& version_value : versions->array)
            {
                if (version_value.type != JsonValue::Type::Object)
                {
                    continue;
                }

                AssetVersion version;
                version.id =
                    version_value.Find("id") ?
                    version_value.Find("id")->String() :
                    "";
                version.path =
                    version_value.Find("path") ?
                    version_value.Find("path")->String() :
                    "";
                version.notes =
                    version_value.Find("notes") ?
                    version_value.Find("notes")->String() :
                    "";
                version.dependencies = json_strings(
                    version_value.Find("dependencies")
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
                                version.path,
                                attribute
                            )
                        )
                        {
                            if (
                                find(
                                    version.dependencies.begin(),
                                    version.dependencies.end(),
                                    dependency
                                ) ==
                                version.dependencies.end()
                            )
                            {
                                version.dependencies.push_back(
                                    dependency
                                );
                            }
                        }
                    }
                }
                const vector<string> direct_dependencies =
                    version.dependencies;
                for (
                    const string& dependency :
                    direct_dependencies
                )
                {
                    if (
                        lower_copy(
                            FileSystem::
                                GetExtensionFromFilePath(
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
                                version.dependencies.begin(),
                                version.dependencies.end(),
                                texture_path
                            ) == version.dependencies.end()
                        )
                        {
                            version.dependencies.push_back(
                                texture_path
                            );
                        }
                    }
                }
                version.number =
                    version_value.Find("number") ?
                    static_cast<int>(
                        version_value.Find("number")->Number()
                    ) :
                    0;

                const JsonValue* quality =
                    version_value.Find("quality");
                if (
                    quality &&
                    quality->type == JsonValue::Type::Object
                )
                {
                    version.quality_score =
                        quality->Find("score") ?
                        static_cast<float>(
                            quality->Find("score")->Number()
                        ) :
                        0.0f;
                    version.quality_verified =
                        quality->Find("verified") ?
                        quality->Find("verified")->Boolean() :
                        false;
                }

                asset.versions.emplace_back(move(version));
            }
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
            for (const AssetVersion& version : entry.versions)
            {
                known.insert(
                    "path:" + normalized_path(version.path)
                );
            }
        }

        const string root =
            FileSystem::GetDirectoryFromFilePath(m_catalog_path);
        const auto scan =
            [this, &known, &root](
                const char* folder,
                const char* type
            )
        {
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
                entry.active_version = "disk";

                AssetVersion version;
                version.id = "disk";
                version.number = 1;
                version.path = path;
                version.notes = "unregistered file found on disk";
                entry.versions.emplace_back(move(version));

                known.insert(type + string(":") + lower_copy(stem));
                known.insert("path:" + normalized_path(path));
                m_assets.emplace_back(move(entry));
            }
        };

        scan("meshes", "mesh");
        scan("materials", "material");
        scan("textures", "texture");
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
                const AssetVersion* first_version =
                    GetActiveVersion(first);
                const AssetVersion* second_version =
                    GetActiveVersion(second);
                const float first_quality = first_version
                    ? first_version->quality_score
                    : -1.0f;
                const float second_quality = second_version
                    ? second_version->quality_score
                    : -1.0f;
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
    DestroyPreviewScene();
    m_mesh.reset();
    m_preview_meshes.clear();
    m_preview_meshes_source = nullptr;
    m_material.reset();
    m_texture.reset();
    m_loaded_path.clear();
    m_loaded_write_time.clear();
    m_prefab_entity_count = 0;
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
    const AssetVersion* version = GetActiveVersion(asset);
    if (!version || version->path.empty())
    {
        m_status = "The selected asset has no active version.";
        return;
    }
    string expected_root =
        FileSystem::GetDirectoryFromFilePath(m_catalog_path);
    if (
        version->path.find("..") != string::npos ||
        expected_root.empty() ||
        !path_is_within(version->path, expected_root)
    )
    {
        m_status = "The selected version has an unsafe catalog path.";
        return;
    }
    if (!FileSystem::Exists(version->path))
    {
        m_status =
            "Asset file not found: " +
            version->path;
        return;
    }

    if (asset.type == "mesh")
    {
        m_mesh = ResourceCache::Load<Mesh>(version->path);
        if (m_mesh && force_reload)
        {
            m_mesh->LoadFromFile(version->path);
        }
        if (!m_mesh || m_mesh->GetVertexCount() == 0)
        {
            m_mesh.reset();
            m_preview_meshes.clear();
            m_preview_meshes_source = nullptr;
            m_status =
                "Mesh could not be loaded: " +
                version->path;
            return;
        }
        LoadWorkingGeometry();
    }
    else if (asset.type == "material")
    {
        m_material =
            ResourceCache::Load<Material>(version->path);
        if (m_material && force_reload)
        {
            m_material->LoadFromFile(version->path);
        }
        if (!m_material)
        {
            m_status =
                "Material could not be loaded: " +
                version->path;
            return;
        }
    }
    else if (asset.type == "texture")
    {
        m_texture =
            ResourceCache::Load<RHI_Texture>(version->path);
        if (!m_texture)
        {
            m_status =
                "Texture could not be loaded: " +
                version->path;
            return;
        }
        // load only produces a cpu texture, the preview needs it on the gpu
        m_texture->PrepareForGpu();
        if (!m_texture->GetRhiResource())
        {
            m_texture.reset();
            m_status =
                "Texture could not be uploaded: " +
                version->path;
            return;
        }
    }
    else
    {
        pugi::xml_document document;
        const pugi::xml_parse_result result =
            document.load_file(version->path.c_str());
        const pugi::xml_node prefab =
            document.child("Prefab");
        if (!result || !prefab)
        {
            m_status =
                "Prefab could not be parsed: " +
                version->path;
            return;
        }
        m_prefab_entity_count =
            1 +
            count_prefab_entities(prefab);
        CollectPrefabDependencies(version->path);
    }

    m_loaded_path = version->path;
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
        m_mesh = ResourceCache::Load<Mesh>(path);
        if (!m_mesh || m_mesh->GetVertexCount() == 0)
        {
            m_mesh.reset();
            m_status =
                "Linked mesh could not be loaded: " +
                path;
            return;
        }
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
    if (!m_mesh)
    {
        return;
    }

    // only lod 0, the lods sit back to back with lod local indices so drawing them raw misplaces every triangle
    for (
        uint32_t sub_mesh = 0;
        sub_mesh < m_mesh->GetSubMeshCount();
        sub_mesh++
    )
    {
        if (m_mesh->GetSubMesh(sub_mesh).lods.empty())
        {
            continue;
        }

        WorkingSubMesh working;
        m_mesh->GetGeometry(
            sub_mesh,
            &working.indices,
            &working.vertices
        );
        if (working.vertices.empty() || working.indices.size() < 3)
        {
            continue;
        }

        working.source_sub_mesh = sub_mesh;
        working.source_vertex_count =
            static_cast<uint32_t>(working.vertices.size());
        working.source_index_count =
            static_cast<uint32_t>(working.indices.size());
        m_working_sub_meshes.emplace_back(move(working));
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
    if (!m_mesh || m_working_sub_meshes.empty())
    {
        return;
    }

    // the chain is only as deep as the shallowest sub mesh, a level that exists for one part and not
    // another would silently drop the parts that ran out of levels
    uint32_t depth =
        m_mesh->GetLodCount(
            m_working_sub_meshes.front().source_sub_mesh
        );
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        depth = min(
            depth,
            m_mesh->GetLodCount(working.source_sub_mesh)
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
            working.source_sub_mesh = source.source_sub_mesh;
            working.source_vertex_count = source.source_vertex_count;
            working.source_index_count = source.source_index_count;
            if (
                !m_mesh->GetGeometryLod(
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

    // the pool survives simplify, revert and lod switches, rebuilding it per edit would grow the
    // global geometry buffer without bound because it only ever appends
    if (
        m_preview_meshes.size() == m_working_sub_meshes.size() &&
        m_preview_meshes_source == m_mesh.get() &&
        m_preview_meshes_capacity >= required_capacity
    )
    {
        return !m_preview_meshes.empty();
    }

    m_preview_meshes.clear();
    m_preview_meshes_source = m_mesh.get();
    m_preview_meshes_capacity = required_capacity;
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        // dynamic rounds the reservation up to a power of two, every later edit only shrinks the
        // geometry so it always fits back into this one allocation
        shared_ptr<Mesh> scratch = make_shared<Mesh>();
        scratch->SetObjectName(
            "asset_viewer_working_" +
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
        m_mesh->GetGeometry(
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
    if (!m_mesh || m_working_sub_meshes.empty())
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
        for (
            const pair<uint64_t, uint32_t>& slot :
            m_preview_render_slots
        )
        {
            Entity* entity = World::GetEntityById(slot.first);
            Render* render =
                entity ?
                entity->GetComponent<Render>() :
                nullptr;
            if (
                !render ||
                render->GetMesh() == m_mesh.get()
            )
            {
                continue;
            }

            render->SetMesh(m_mesh.get(), slot.second);
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
    for (
        const pair<uint64_t, uint32_t>& slot :
        m_preview_render_slots
    )
    {
        Entity* entity = World::GetEntityById(slot.first);
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
                m_working_sub_meshes[index].source_sub_mesh !=
                slot.second
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
        !m_mesh ||
        !m_working_editable ||
        m_loaded_path.empty() ||
        m_working_sub_meshes.empty()
    )
    {
        return false;
    }

    // bake into a standalone mesh so the cached resource is untouched until the
    // file is reloaded below, lods are rebuilt from the edited geometry
    Mesh baked;
    baked.SetObjectName(
        FileSystem::GetFileNameWithoutExtensionFromFilePath(
            m_loaded_path
        )
    );

    // inherit the import flags so the rebuilt file keeps the behaviour the original was authored
    // with, only the lod flag follows the checkbox
    baked.SetFlags(m_mesh->GetFlags());
    baked.SetFlag(
        static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods),
        m_working_generate_lods
    );

    // sub mesh order has to survive the bake, the prefab and the materials reference sub meshes by
    // index, so slots are reserved up front and written in place
    baked.ReserveSubMeshes(m_mesh->GetSubMeshCount());
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        if (working.source_sub_mesh >= m_mesh->GetSubMeshCount())
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
    }
    baked.SaveToFile(m_loaded_path);
    if (!FileSystem::Exists(m_loaded_path))
    {
        m_status =
            "Failed to write " +
            m_loaded_path;
        return false;
    }

    m_working_modified = false;
    m_loaded_write_time.clear();
    LoadSelectedAsset(false, true);
    m_status =
        "Saved " +
        FileSystem::GetFileNameFromFilePath(m_loaded_path) +
        " with " +
        to_string(m_working_vertices.size()) +
        " vertices";
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
            m_preview_render_slots.emplace_back(
                root->GetObjectId(),
                0u
            );
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
                m_preview_render_slots.emplace_back(
                    part->GetObjectId(),
                    i
                );
            }
        }

        // the scratch meshes hold the edit in progress, the source is only the layout donor
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
            chrono::milliseconds(66);
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
    m_preview_meshes.clear();
    m_preview_meshes_source = nullptr;
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
    if (m_mesh)
    {
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
            mesh->GetVertices().size()
        );
        index_count += static_cast<uint64_t>(
            mesh->GetIndices().size()
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

void AssetViewer::DrawToolbar()
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

    ImGui::SetNextItemWidth(
        min(
            420.0f * scale,
            max(
                180.0f * scale,
                ImGui::GetContentRegionAvail().x * 0.36f
            )
        )
    );
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
    ImGui::SameLine();

    // one type at a time, a dropdown instead of five toggles so the toolbar keeps room for the
    // search field on narrow layouts
    const char* filters =
        "All types\0"
        "Meshes\0"
        "Materials\0"
        "Prefabs\0"
        "Textures\0";
    ImGui::SetNextItemWidth(126.0f * scale);
    ImGui::Combo(
        "##asset_type_filter",
        &m_type_filter,
        filters
    );
    ImGuiSp::tooltip("Filter by asset type");

    const float actions_width = 210.0f * scale;
    const float right =
        ImGui::GetCursorPosX() +
        ImGui::GetContentRegionAvail().x;
    if (ImGui::GetCursorPosX() + actions_width < right)
    {
        ImGui::SameLine();
        ImGui::SetCursorPosX(right - actions_width);
    }
    if (
        ImGuiSp::button(
            "Clean up",
            ImVec2(76.0f * scale, 0.0f)
        )
    )
    {
        ScanLibraryCleanup();
    }
    ImGuiSp::tooltip(
        "Delete superseded asset versions and unreferenced leftovers"
    );
    ImGui::SameLine();
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
    ImGui::SameLine();

    const char* sort_labels[] =
    {
        "Name",
        "Quality",
        "Type"
    };
    ImGui::SetNextItemWidth(92.0f * scale);
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
                    const AssetVersion* first_version =
                        GetActiveVersion(first);
                    const AssetVersion* second_version =
                        GetActiveVersion(second);
                    const float first_quality = first_version
                        ? first_version->quality_score
                        : -1.0f;
                    const float second_quality = second_version
                        ? second_version->quality_score
                        : -1.0f;
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
    ImGui::PopStyleVar(2);
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
        "%d of %zu catalog resources",
        visible_count,
        m_assets.size()
    );
    if (m_selected_assets.size() > 1)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(
            "  |  %zu selected",
            m_selected_assets.size()
        );
    }
    ImGui::SameLine();
    ImGui::TextDisabled("  |  %s", m_status.c_str());
    if (!m_catalog_path.empty())
    {
        ImGuiSp::tooltip(m_catalog_path.c_str());
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

        const AssetVersion* version = GetActiveVersion(prefab);
        if (!version)
        {
            continue;
        }
        for (const string& dependency : version->dependencies)
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
                const AssetVersion* child_version =
                    GetActiveVersion(m_assets[index]);
                if (
                    child_version &&
                    normalized_path(child_version->path) ==
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
                const AssetVersion* child_version =
                    GetActiveVersion(m_assets[index]);
                const string child_file = child_version
                    ? lower_copy(
                        FileSystem::
                            GetFileNameWithoutExtensionFromFilePath(
                                child_version->path
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

    ImGui::SameLine();
    ImGui::TextDisabled("%zu", visible_roots.size());
    ImGui::Separator();

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
            const AssetVersion* version = GetActiveVersion(asset);
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
                ImGui::SetCursorScreenPos(cursor_below_row);
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
            else if (version && version->id == "disk")
            {
                metadata += "  unregistered";
            }
            else if (version)
            {
                char quality[32] = {};
                snprintf(
                    quality,
                    sizeof(quality),
                    "  q %.1f%s",
                    version->quality_score,
                    version->quality_verified
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
                ImGui::SetCursorScreenPos(cursor_below_dependency);
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
            ? "No catalog assets"
            : "No matching assets";
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
    const AssetVersion* version = GetActiveVersion(asset);

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
    const bool has_path = version && !version->path.empty();
    if (ImGui::MenuItem("Copy path", nullptr, false, has_path))
    {
        ImGui::SetClipboardText(version->path.c_str());
        m_status = "Copied " + version->path;
    }
    if (ImGui::MenuItem("Show in explorer", nullptr, false, has_path))
    {
        error_code error;
        const filesystem::path absolute = filesystem::absolute(
            filesystem::path(
                FileSystem::GetDirectoryFromFilePath(version->path)
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
            ImGui::Text(
                "Permanently delete %zu %s?",
                ids.size(),
                ids.size() == 1 ? "asset" : "assets"
            );
            ImGui::TextDisabled(
                "Every version, source and thumbnail they own goes with them."
            );
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

                    const AssetVersion* version =
                        GetActiveVersion(asset);
                    ImGui::TextDisabled(
                        "%s",
                        (
                            asset_display_name(asset.name) +
                            "  -  " +
                            (
                                version && version->id == "disk"
                                ? asset.type + ", file only"
                                : asset.type +
                                    ", " +
                                    to_string(asset.versions.size()) +
                                    (
                                        asset.versions.size() == 1
                                        ? " version"
                                        : " versions"
                                    )
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
        const AssetVersion* version = GetActiveVersion(asset);
        if (version && version->id == "disk")
        {
            ImGui::TextDisabled("%s", version->path.c_str());
        }
        else
        {
            ImGui::TextDisabled(
                "This removes all %zu versions, sources and thumbnails.",
                asset.versions.size()
            );
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

void AssetViewer::ScanLibraryCleanup()
{
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

    string parse_error;
    JsonValue root;
    JsonParser parser(source);
    const bool parsed = parser.Parse(root, parse_error);
    const JsonValue* assets = root.Find("assets");
    if (
        !parsed ||
        !assets ||
        assets->type != JsonValue::Type::Object
    )
    {
        m_cleanup.error =
            "The catalog could not be parsed, cleanup aborted.";
        m_cleanup.scanned = true;
        return;
    }

    // the reachable set starts from what the project still needs, everything else in the library is
    // a leftover, references are followed because a kept prefab pulls in meshes, materials and
    // textures that live outside the versioned folders
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

    // collect the versions that stay and the ones that go, an asset without an active version keeps
    // its highest numbered one so an unfinished asset is never wiped out entirely
    vector<pair<string, string>> superseded;
    for (const auto& [asset_id, entry] : assets->object)
    {
        const JsonValue* versions = entry.Find("versions");
        if (!versions || versions->type != JsonValue::Type::Array)
        {
            continue;
        }

        const JsonValue* active = entry.Find("active_version");
        const string active_id = active ? active->String() : "";
        const JsonValue* kept = nullptr;
        double highest = -1.0;
        for (const JsonValue& version : versions->array)
        {
            const JsonValue* id = version.Find("id");
            if (!active_id.empty() && id && id->String() == active_id)
            {
                kept = &version;
                break;
            }

            const JsonValue* number = version.Find("number");
            const double value = number ? number->Number(-1.0) : -1.0;
            if (value > highest)
            {
                highest = value;
                kept = &version;
            }
        }

        for (const JsonValue& version : versions->array)
        {
            const bool is_kept = &version == kept;
            const JsonValue* id = version.Find("id");
            vector<string> owned;
            for (
                const char* key :
                {
                    "path",
                    "source_path",
                    "thumbnail_path"
                }
            )
            {
                if (const JsonValue* value = version.Find(key))
                {
                    if (!value->String().empty())
                    {
                        owned.push_back(value->String());
                    }
                }
            }
            if (
                const JsonValue* dependencies =
                    version.Find("dependencies");
                dependencies &&
                dependencies->type == JsonValue::Type::Array
            )
            {
                for (const JsonValue& dependency : dependencies->array)
                {
                    if (!dependency.String().empty())
                    {
                        owned.push_back(dependency.String());
                    }
                }
            }

            if (is_kept)
            {
                for (const string& path : owned)
                {
                    mark(path);
                }
                continue;
            }

            for (const string& path : owned)
            {
                superseded.emplace_back(
                    asset_id + " " + (id ? id->String() : "?"),
                    path
                );
            }
            if (id && !id->String().empty())
            {
                m_cleanup.directories.push_back(
                    library_root +
                    "dependencies/" +
                    asset_id +
                    "/" +
                    id->String()
                );
            }
        }
    }

    // disk only assets were never versioned, they are the working files the catalog entries point at
    // and the tool itself lists them, so they are roots as well
    for (const AssetEntry& entry : m_assets)
    {
        for (const AssetVersion& version : entry.versions)
        {
            if (version.id == "disk")
            {
                mark(version.path);
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

    for (const auto& [label, path] : superseded)
    {
        if (
            reachable.count(normalized_path(path)) ||
            !path_is_within(path, library_root) ||
            !FileSystem::Exists(path) ||
            !planned.insert(normalized_path(path)).second
        )
        {
            continue;
        }

        m_cleanup.superseded_labels.push_back(label);
        m_cleanup.superseded_files.push_back(path);
        m_cleanup.bytes += file_size(path);
    }

    {
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
        if (!path_is_within(path, library_root))
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

    for (const string& path : m_cleanup.superseded_files)
    {
        remove_file(path);
    }
    if (m_cleanup_include_orphans)
    {
        for (const string& path : m_cleanup.orphan_files)
        {
            remove_file(path);
        }
    }
    for (const string& directory : m_cleanup.directories)
    {
        if (
            !path_is_within(directory, library_root) ||
            !FileSystem::Exists(directory)
        )
        {
            continue;
        }

        error_code error;
        filesystem::remove_all(
            filesystem::path(directory),
            error
        );
        if (error)
        {
            failed++;
        }
    }

    // drop the superseded records so the catalog stops advertising files that are gone
    string source;
    if (FileSystem::ReadFile(m_catalog_path, source))
    {
        string parse_error;
        JsonValue root;
        JsonParser parser(source);
        const bool parsed = parser.Parse(root, parse_error);
        auto assets = root.object.find("assets");
        if (
            parsed &&
            assets != root.object.end() &&
            assets->second.type == JsonValue::Type::Object
        )
        {
            for (auto& [asset_id, entry] : assets->second.object)
            {
                auto versions = entry.object.find("versions");
                if (
                    versions == entry.object.end() ||
                    versions->second.type != JsonValue::Type::Array
                )
                {
                    continue;
                }

                vector<JsonValue> surviving;
                for (const JsonValue& version : versions->second.array)
                {
                    const JsonValue* path = version.Find("path");
                    if (
                        path &&
                        !path->String().empty() &&
                        !FileSystem::Exists(path->String())
                    )
                    {
                        continue;
                    }
                    surviving.push_back(version);
                }

                // never leave an asset with no versions at all, that reads as corruption rather than
                // as a cleanup
                if (!surviving.empty())
                {
                    versions->second.array = move(surviving);
                }
            }

            const string temporary_path =
                m_catalog_path + ".cleanup.tmp";
            FileSystem::Delete(temporary_path);
            if (
                FileSystem::WriteFile(
                    temporary_path,
                    serialize_json(root) + "\n"
                )
            )
            {
                error_code error;
                filesystem::rename(
                    temporary_path,
                    m_catalog_path,
                    error
                );
                if (error)
                {
                    FileSystem::Delete(temporary_path);
                }
            }
        }
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

        const size_t total =
            m_cleanup.superseded_files.size() +
            (
                m_cleanup_include_orphans
                    ? m_cleanup.orphan_files.size()
                    : 0
            );
        if (total == 0)
        {
            ImGui::TextUnformatted(
                "The asset library is already clean."
            );
            ImGui::TextDisabled(
                "Every file is reachable from a world or an active version."
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
            "Active versions, and anything a world or an active version "
            "references, are kept."
        );
        ImGui::Spacing();

        ImGui::Checkbox(
            "Also delete unreferenced leftovers",
            &m_cleanup_include_orphans
        );
        ImGuiSp::tooltip(
            "Staging copies and stale thumbnails that nothing points at"
        );

        ImGui::Spacing();
        ImGui::BeginChild(
            "##cleanup_list",
            ImVec2(560.0f * ui_scale(), 240.0f * ui_scale()),
            ImGuiChildFlags_Borders
        );
        if (!m_cleanup.superseded_files.empty())
        {
            ImGui::TextUnformatted("SUPERSEDED VERSIONS");
            ImGui::Separator();
            for (
                size_t index = 0;
                index < m_cleanup.superseded_files.size();
                index++
            )
            {
                ImGui::TextDisabled(
                    "%s  %s",
                    m_cleanup.superseded_labels[index].c_str(),
                    m_cleanup.superseded_files[index].c_str()
                );
            }
            ImGui::Spacing();
        }
        if (
            m_cleanup_include_orphans &&
            !m_cleanup.orphan_files.empty()
        )
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
    const AssetVersion* version = GetActiveVersion(asset);
    if (version && version->id == "disk")
    {
        const string old_path = version->path;
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
            const AssetVersion* renamed = GetActiveVersion(m_assets[i]);
            if (renamed && normalized_path(renamed->path) == wanted)
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
    JsonParser parser(source);
    if (!parser.Parse(root, parse_error))
    {
        m_status = "The asset catalog is invalid: " + parse_error;
        return false;
    }

    auto assets_iterator = root.object.find("assets");
    if (
        root.type != JsonValue::Type::Object ||
        assets_iterator == root.object.end() ||
        assets_iterator->second.type != JsonValue::Type::Object
    )
    {
        m_status = "The asset catalog has no assets object.";
        return false;
    }

    const auto asset_iterator =
        assets_iterator->second.object.find(asset.id);
    if (asset_iterator == assets_iterator->second.object.end())
    {
        m_status = "The selected asset is no longer in the catalog.";
        RefreshCatalog(true);
        return false;
    }

    JsonValue& name_value =
        asset_iterator->second.object["name"];
    name_value.type = JsonValue::Type::String;
    name_value.text = name;

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

        const AssetVersion* version = GetActiveVersion(*entry);
        if (version && version->id == "disk")
        {
            disk_paths.push_back(version->path);
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
    JsonParser parser(source);
    if (!parser.Parse(root, parse_error))
    {
        m_status =
            "The asset catalog is invalid: " +
            parse_error;
        return false;
    }

    auto assets_iterator = root.object.find("assets");
    if (
        root.type != JsonValue::Type::Object ||
        assets_iterator == root.object.end() ||
        assets_iterator->second.type !=
            JsonValue::Type::Object
    )
    {
        m_status = "The asset catalog has no assets object.";
        return false;
    }

    JsonValue& assets = assets_iterator->second;
    vector<string> owned_paths;
    vector<string> erased_ids;
    for (const string& asset_id : catalog_ids)
    {
        const auto asset_iterator =
            assets.object.find(asset_id);
        if (asset_iterator == assets.object.end())
        {
            continue;
        }

        if (
            const JsonValue* versions =
                asset_iterator->second.Find("versions");
            versions &&
            versions->type == JsonValue::Type::Array
        )
        {
            for (const JsonValue& version : versions->array)
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
                    if (const JsonValue* path = version.Find(key))
                    {
                        if (!path->String().empty())
                        {
                            owned_paths.push_back(path->String());
                        }
                    }
                }
            }
        }

        assets.object.erase(asset_iterator);
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
        ImGui::TextUnformatted(
            asset_display_name(
                FileSystem::
                    GetFileNameWithoutExtensionFromFilePath(
                        m_selected_dependency_path
                    )
            ).c_str()
        );
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(
                asset_type_color(type)
            ),
            "%s  linked file",
            type.c_str()
        );
        ImGui::Separator();

        // a mesh reached by expanding a prefab is still a mesh, without this the simplify and lod
        // controls are only reachable when the mesh happens to be a top level catalog entry
        const bool mesh_tools_available =
            m_mesh &&
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

        ImGui::Spacing();
        ImGui::TextUnformatted("SOURCE");
        ImGui::Separator();
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

    const AssetEntry& asset = m_assets[m_selected_asset];
    const AssetVersion* version = GetActiveVersion(asset);

    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImVec4(
            ImGui::GetStyleColorVec4(ImGuiCol_FrameBg).x,
            ImGui::GetStyleColorVec4(ImGuiCol_FrameBg).y,
            ImGui::GetStyleColorVec4(ImGuiCol_FrameBg).z,
            0.42f
        )
    );
    ImGui::BeginChild(
        "##asset_identity",
        ImVec2(0.0f, 68.0f * scale),
        ImGuiChildFlags_None
    );
    ImGui::SetCursorPos(
        ImVec2(12.0f * scale, 15.0f * scale)
    );
    ImGuiSp::image(
        asset_type_icon(asset.type),
        30.0f * scale,
        ImVec4(0.82f, 0.9f, 1.0f, 1.0f)
    );
    ImGui::SameLine();
    ImGui::BeginGroup();
    const string display_name =
        asset_display_name(asset.name);
    ImGui::TextUnformatted(display_name.c_str());
    ImGui::TextColored(
        ImGui::ColorConvertU32ToFloat4(
            asset_type_color(asset.type)
        ),
        "%s asset",
        asset.type.c_str()
    );
    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    const char* tabs[] = { "Overview", "Versions", "Optimize" };
    const int tab_count =
        m_mesh &&
        m_working_editable &&
        !m_working_vertices.empty()
            ? 3
            : 2;

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
        detail_row(
            "Versions",
            to_string(asset.versions.size())
        );

        ImGui::Spacing();
        ImGui::TextUnformatted("TECHNICAL");
        ImGui::Separator();
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
        if (asset.type == "prefab")
        {
            detail_row(
                "Entities",
                to_string(m_prefab_entity_count)
            );
            detail_row(
                "Dependencies",
                m_missing_dependencies.empty()
                    ? "All resolved"
                    : to_string(m_missing_dependencies.size()) +
                        " missing"
            );

            // a prefab can reference several meshes, so the geometry tools live on the mesh itself
            ImGui::TextDisabled(
                "Expand this prefab and pick a mesh to simplify it or build LODs"
            );
        }
        if (version)
        {
            detail_row(
                "Quality",
                to_string(version->quality_score) +
                    (
                        version->quality_verified
                            ? " verified"
                            : " unverified"
                    )
            );
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("SOURCE");
        ImGui::Separator();
        if (version)
        {
            ImGui::TextWrapped("%s", version->path.c_str());
            if (ImGuiSp::button("Copy path"))
            {
                ImGui::SetClipboardText(version->path.c_str());
                m_status = "Copied asset path";
            }
            ImGui::SameLine();
            if (ImGuiSp::button("Show in folder"))
            {
                FileSystem::OpenUrl(
                    FileSystem::GetDirectoryFromFilePath(
                        version->path
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
                    World::GetGeneratedResourceDirectory() +
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
    else if (m_inspector_tab == 1)
    {
        ImGui::TextDisabled(
            "%zu saved version%s",
            asset.versions.size(),
            asset.versions.size() == 1 ? "" : "s"
        );
        ImGui::Spacing();
        for (
            int index =
                static_cast<int>(asset.versions.size()) - 1;
            index >= 0;
            index--
        )
        {
            const AssetVersion& item = asset.versions[index];
            const bool active = item.id == asset.active_version;
            ImGui::PushID(index);
            ImGui::BeginChild(
                "##version_card",
                ImVec2(0.0f, 76.0f * scale),
                ImGuiChildFlags_Borders
            );
            ImGui::Text(
                "Version %d",
                item.number
            );
            ImGui::SameLine();
            if (active)
            {
                ImGui::TextColored(
                    ImGui::Style::color_accent_1,
                    "ACTIVE"
                );
            }
            ImGui::TextDisabled(
                "%s  |  quality %.1f%s",
                item.id.c_str(),
                item.quality_score,
                item.quality_verified ? " verified" : ""
            );
            if (!item.notes.empty())
            {
                ImGui::TextWrapped("%s", item.notes.c_str());
            }
            ImGui::EndChild();
            ImGui::PopID();
        }
    }
    else
    {
        DrawMeshTools();
    }

    if (m_inspector_tab != 2)
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
            "This removes all %zu versions, sources and thumbnails.",
            asset.versions.size()
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

    ImGui::EndChild();
}

void AssetViewer::DrawMeshTools()
{
    if (
        !m_mesh ||
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

    if (m_working_modified)
    {
        const ImVec4 accent = ImGui::Style::color_accent_1;
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(accent.x, accent.y, accent.z, 0.1f)
        );
        ImGui::BeginChild(
            "##mesh_changes",
            ImVec2(0.0f, 48.0f * ui_scale()),
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
        to_string(m_working_sub_meshes.size())
    );
    ImGui::TextDisabled(
        "Current reduction %.1f%%",
        reduction * 100.0f
    );

    ImGui::Spacing();
    ImGui::TextUnformatted("SIMPLIFICATION");
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
    if (
        ImGuiSp::button(
            "Revert to source",
            ImVec2(-1.0f, 0.0f)
        )
    )
    {
        LoadWorkingGeometry();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("LEVELS OF DETAIL");
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

    ImGui::Spacing();

    // a chain built here counts as something worth saving, a mesh that ships without lods is the
    // one case where nothing about lod 0 has to change for the bake to be useful. the mesh's own saved
    // chain does not count, it is already in the file
    const bool can_save =
        m_working_modified ||
        m_working_lods_built;
    if (!can_save)
    {
        ImGui::BeginDisabled();
    }
    if (
        ImGuiSp::button(
            "Save mesh",
            ImVec2(-1.0f, 0.0f)
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
        ImGui::TextUnformatted("Overwrite the current mesh version?");
        ImGui::TextDisabled(
            "%llu to %llu triangles, %.1f%% reduction",
            static_cast<unsigned long long>(source_triangles),
            static_cast<unsigned long long>(working_triangles),
            reduction * 100.0f
        );
        ImGui::TextDisabled(
            "%zu sub meshes, LODs %s",
            m_working_sub_meshes.size(),
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

void AssetViewer::DrawPreview(float width, float height)
{
    ImGui::BeginChild(
        "##asset_viewer_preview",
        ImVec2(width, height),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse
    );

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
        // shading and backdrop are both one of many, dropdowns instead of toggle rows so the
        // exclusivity is visible in the control and the toolbar stays narrow
        const char* modes =
            "Solid\0"
            "Wireframe\0"
            "Vertices\0";
        ImGui::SetNextItemWidth(104.0f * scale);
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
        ImGuiSp::tooltip("Shading mode");

        ImGui::SameLine(0.0f, 3.0f * scale);
        const char* backdrops =
            "Auto\0"
            "Sky\0"
            "Charcoal\0"
            "Slate\0"
            "Paper\0";
        ImGui::SetNextItemWidth(98.0f * scale);
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
        ImGuiSp::tooltip(
            "Backdrop, auto uses the sky for solid shading and a dark "
            "studio tone for wireframe and vertices"
        );
    }
    else
    {
        ImGui::TextDisabled("Texture");
    }
    ImGui::SameLine(0.0f, 3.0f * scale);
    if (toolbar_toggle("Stats", m_preview_show_stats))
    {
        m_preview_show_stats = !m_preview_show_stats;
    }
    ImGui::SameLine(0.0f, 3.0f * scale);
    if (toolbar_toggle("Orbit", m_preview_auto_rotate))
    {
        m_preview_auto_rotate = !m_preview_auto_rotate;
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
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(
                minimum.x + 12.0f,
                minimum.y + 12.0f
            ),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            PreviewRoot()
                ? "Rendering preview"
                : "Select an asset"
        );
    }
    if (
        m_preview_show_stats &&
        !m_texture &&
        Renderer::IsSecondaryViewReady()
    )
    {
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(
                minimum.x + 10.0f,
                maximum.y - 26.0f
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

void AssetViewer::SetPanelVisible(bool visible)
{
    m_visible = visible;
    if (!m_visible)
    {
        // the preview survives the panel being hidden, see OnInvisible, a driver that closes the panel is
        // not saying it is finished with the asset it was reviewing
        Renderer::InvalidateSecondaryView();
    }
    RefreshCatalog(m_visible);
}

bool AssetViewer::SelectAsset(
    const string& query,
    const string& version,
    string& error
)
{
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

    if (!version.empty())
    {
        AssetEntry& asset = m_assets[m_selected_asset];
        const auto match = find_if(
            asset.versions.begin(),
            asset.versions.end(),
            [&version](const AssetVersion& candidate)
            {
                return
                    candidate.id == version ||
                    to_string(candidate.number) == version;
            }
        );
        if (match == asset.versions.end())
        {
            error = "asset version was not found";
            return false;
        }
        asset.active_version = match->id;
    }

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
    status.yaw = m_preview_yaw;
    status.pitch = m_preview_pitch;
    status.zoom = m_preview_zoom;

    if (
        m_selected_asset >= 0 &&
        m_selected_asset < static_cast<int>(m_assets.size())
    )
    {
        const AssetEntry& asset = m_assets[m_selected_asset];
        status.selected_asset_id = asset.id;
        status.selected_asset_name = asset.name;
        if (
            const AssetVersion* version =
                GetActiveVersion(asset)
        )
        {
            status.selected_version_id = version->id;
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
        const AssetVersion* version = GetActiveVersion(asset);
        if (
            is_packed_texture(asset.id) ||
            is_packed_texture(asset.name) ||
            (version && is_packed_texture(version->path))
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

const AssetViewer::AssetVersion* AssetViewer::GetActiveVersion(
    const AssetEntry& asset
) const
{
    if (asset.active_version.empty())
    {
        return asset.versions.empty() ?
            nullptr :
            &asset.versions.back();
    }

    for (const AssetVersion& version : asset.versions)
    {
        if (version.id == asset.active_version)
        {
            return &version;
        }
    }

    return nullptr;
}
