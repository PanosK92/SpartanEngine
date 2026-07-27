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

#include "pch.h"
#include "AssetViewer.h"
#include "../ImGui/ImGui_Extension.h"
#include "../ImGui/ImGui_Style.h"
#include "FileSystem/FileSystem.h"
#include "Geometry/GeometryProcessing.h"
#include "Geometry/Mesh.h"
#include "IO/pugixml.hpp"
#include "MCP/McpCommands.h"
#include "RHI/RHI_Texture.h"
#include "Rendering/Material.h"
#include "Resource/Import/ImageImporter.h"
#include "Resource/ResourceCache.h"
#include "World/Entity.h"
#include "World/Prefab.h"
#include "World/World.h"
#include "World/Components/Render.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <sstream>

using namespace std;
using namespace spartan;

namespace
{
    string json_string(const string& value)
    {
        string result = "\"";
        for (const char character : value)
        {
            if (
                character == '\\' ||
                character == '"'
            )
            {
                result += '\\';
            }
            result += character;
        }
        result += '"';
        return result;
    }

    optional<string> argument(
        const McpRequest& request,
        const string& name
    )
    {
        const auto iterator = request.arguments.find(name);
        return iterator == request.arguments.end()
            ? nullopt
            : optional<string>(iterator->second);
    }

    string screenshot_path(const optional<string>& requested)
    {
        string name = requested
            ? FileSystem::GetFileNameFromFilePath(*requested)
            : "asset_viewer.png";
        if (FileSystem::GetExtensionFromFilePath(name) != ".png")
        {
            name += ".png";
        }
        return
            World::GetMcpResourceDirectory() +
            "thumbnails/" +
            name;
    }

    void set_pixel(
        vector<uint8_t>& pixels,
        const uint32_t width,
        const uint32_t height,
        const int x,
        const int y,
        const array<uint8_t, 4>& color
    )
    {
        if (
            x < 0 ||
            y < 0 ||
            x >= static_cast<int>(width) ||
            y >= static_cast<int>(height)
        )
        {
            return;
        }
        const size_t index =
            (
                static_cast<size_t>(y) * width +
                static_cast<size_t>(x)
            ) *
            4;
        for (size_t channel = 0; channel < 4; channel++)
        {
            pixels[index + channel] = color[channel];
        }
    }

    void draw_line(
        vector<uint8_t>& pixels,
        const uint32_t width,
        const uint32_t height,
        int x0,
        int y0,
        const int x1,
        const int y1,
        const array<uint8_t, 4>& color
    )
    {
        const int delta_x = abs(x1 - x0);
        const int step_x = x0 < x1 ? 1 : -1;
        const int delta_y = -abs(y1 - y0);
        const int step_y = y0 < y1 ? 1 : -1;
        int error = delta_x + delta_y;
        while (true)
        {
            set_pixel(
                pixels,
                width,
                height,
                x0,
                y0,
                color
            );
            if (x0 == x1 && y0 == y1)
            {
                break;
            }
            const int doubled_error = error * 2;
            if (doubled_error >= delta_y)
            {
                error += delta_y;
                x0 += step_x;
            }
            if (doubled_error <= delta_x)
            {
                error += delta_x;
                y0 += step_y;
            }
        }
    }

    // rasterizes into the screenshot buffer, drops segments that project far
    // outside the image so bresenham never walks a huge span
    void draw_line_bounded(
        vector<uint8_t>& pixels,
        const uint32_t width,
        const uint32_t height,
        const ImVec2& a,
        const ImVec2& b,
        const array<uint8_t, 4>& color
    )
    {
        const float limit =
            static_cast<float>(max(width, height)) * 4.0f;
        if (
            !isfinite(a.x) ||
            !isfinite(a.y) ||
            !isfinite(b.x) ||
            !isfinite(b.y) ||
            abs(a.x - b.x) > limit ||
            abs(a.y - b.y) > limit ||
            (a.x < -limit && b.x < -limit) ||
            (a.y < -limit && b.y < -limit) ||
            (
                a.x > static_cast<float>(width) + limit &&
                b.x > static_cast<float>(width) + limit
            ) ||
            (
                a.y > static_cast<float>(height) + limit &&
                b.y > static_cast<float>(height) + limit
            )
        )
        {
            return;
        }

        draw_line(
            pixels,
            width,
            height,
            static_cast<int>(a.x),
            static_cast<int>(a.y),
            static_cast<int>(b.x),
            static_cast<int>(b.y),
            color
        );
    }

    // a piece of geometry to wireframe, transform is world space for prefab parts
    struct PreviewPart
    {
        const vector<RHI_Vertex_PosTexNorTan>* vertices = nullptr;
        const vector<uint32_t>* indices = nullptr;
        math::Matrix transform = math::Matrix::Identity;
    };

    // perspective orbit camera, perspective is what makes yaw visible on
    // rotationally symmetric shapes, an orthographic spin looks static
    struct OrbitCamera
    {
        math::Vector3 position;
        math::Vector3 right;
        math::Vector3 up;
        math::Vector3 forward;
        float focal = 1.0f;
        float distance = 1.0f;
        ImVec2 screen_center = ImVec2(0.0f, 0.0f);

        bool project(
            const math::Vector3& point,
            ImVec2& projected,
            float& depth
        ) const
        {
            const math::Vector3 local = point - position;
            depth = math::Vector3::Dot(local, forward);
            if (depth < 0.001f)
            {
                return false;
            }

            const float x = math::Vector3::Dot(local, right);
            const float y = math::Vector3::Dot(local, up);
            projected = ImVec2(
                screen_center.x + x / depth * focal,
                screen_center.y - y / depth * focal
            );
            return true;
        }
    };

    OrbitCamera make_orbit_camera(
        const math::Vector3& center,
        const float radius,
        const float yaw,
        const float pitch,
        const float zoom,
        const ImVec2& minimum,
        const ImVec2& maximum
    )
    {
        OrbitCamera camera;
        const math::Vector3 direction(
            cos(pitch) * sin(yaw),
            sin(pitch),
            -cos(pitch) * cos(yaw)
        );
        camera.distance = max(
            0.001f,
            radius * 3.2f / max(0.05f, zoom)
        );
        camera.position = center + direction * camera.distance;
        camera.forward =
            (center - camera.position).Normalized();

        math::Vector3 world_up(0.0f, 1.0f, 0.0f);
        if (
            abs(math::Vector3::Dot(camera.forward, world_up)) >
            0.999f
        )
        {
            world_up = math::Vector3(0.0f, 0.0f, 1.0f);
        }
        camera.right = math::Vector3::Cross(
            world_up,
            camera.forward
        ).Normalized();
        camera.up = math::Vector3::Cross(
            camera.forward,
            camera.right
        ).Normalized();

        const float extent = min(
            maximum.x - minimum.x,
            maximum.y - minimum.y
        );
        camera.focal = max(1.0f, extent * 0.5f) / 0.4228f;
        camera.screen_center = ImVec2(
            (minimum.x + maximum.x) * 0.5f,
            (minimum.y + maximum.y) * 0.5f
        );
        return camera;
    }

    bool compute_preview_bounds(
        const vector<PreviewPart>& parts,
        math::Vector3& minimum,
        math::Vector3& maximum,
        size_t& triangle_count
    )
    {
        minimum = math::Vector3::Infinity;
        maximum = math::Vector3::InfinityNeg;
        triangle_count = 0;
        for (const PreviewPart& part : parts)
        {
            if (
                !part.vertices ||
                !part.indices ||
                part.vertices->empty() ||
                part.indices->size() < 3
            )
            {
                continue;
            }

            triangle_count += part.indices->size() / 3;
            for (
                const RHI_Vertex_PosTexNorTan& vertex :
                *part.vertices
            )
            {
                const math::Vector3 position =
                    part.transform * vertex.get_position();
                minimum = math::Vector3::Min(minimum, position);
                maximum = math::Vector3::Max(maximum, position);
            }
        }
        return triangle_count > 0;
    }

    float preview_radius_from_bounds(
        const math::Vector3& minimum,
        const math::Vector3& maximum
    )
    {
        const math::Vector3 extent = maximum - minimum;
        return max(
            0.0005f,
            max(extent.x, max(extent.y, extent.z)) * 0.5f
        );
    }

    // emits triangle edges, the callback receives screen positions, a 0 to 1
    // proximity factor and whether the triangle faces the camera
    template<typename LineFunction>
    void emit_wireframe(
        const vector<PreviewPart>& parts,
        const OrbitCamera& camera,
        const float radius,
        const size_t triangle_count,
        const size_t triangle_budget,
        const LineFunction& emit
    )
    {
        const size_t stride = max<size_t>(
            1,
            triangle_count / max<size_t>(1, triangle_budget)
        );
        size_t triangle_index = 0;
        for (const PreviewPart& part : parts)
        {
            if (!part.vertices || !part.indices)
            {
                continue;
            }

            const vector<RHI_Vertex_PosTexNorTan>& vertices =
                *part.vertices;
            const vector<uint32_t>& indices = *part.indices;
            for (
                size_t index = 0;
                index + 2 < indices.size();
                index += 3
            )
            {
                if (triangle_index++ % stride != 0)
                {
                    continue;
                }

                const uint32_t first = indices[index];
                const uint32_t second = indices[index + 1];
                const uint32_t third = indices[index + 2];
                if (
                    first >= vertices.size() ||
                    second >= vertices.size() ||
                    third >= vertices.size()
                )
                {
                    continue;
                }

                ImVec2 a;
                ImVec2 b;
                ImVec2 c;
                float depth_a = 0.0f;
                float depth_b = 0.0f;
                float depth_c = 0.0f;
                if (
                    !camera.project(
                        part.transform *
                        vertices[first].get_position(),
                        a,
                        depth_a
                    ) ||
                    !camera.project(
                        part.transform *
                        vertices[second].get_position(),
                        b,
                        depth_b
                    ) ||
                    !camera.project(
                        part.transform *
                        vertices[third].get_position(),
                        c,
                        depth_c
                    )
                )
                {
                    continue;
                }

                const float area =
                    (b.x - a.x) * (c.y - a.y) -
                    (c.x - a.x) * (b.y - a.y);
                const float depth =
                    (depth_a + depth_b + depth_c) / 3.0f;
                const float proximity = clamp(
                    (camera.distance + radius - depth) /
                    max(0.0001f, radius * 2.0f),
                    0.0f,
                    1.0f
                );
                emit(a, b, c, proximity, area < 0.0f);
            }
        }
    }

    // ground grid and axes, gives the orbit a fixed reference to rotate against
    template<typename LineFunction>
    void emit_orbit_grid(
        const OrbitCamera& camera,
        const math::Vector3& center,
        const float ground_height,
        const float radius,
        const LineFunction& emit
    )
    {
        const int divisions = 10;
        const float half = radius * 2.0f;
        const float step = half * 2.0f / divisions;
        for (int line = 0; line <= divisions; line++)
        {
            const float offset = -half + step * line;
            const math::Vector3 points[4] =
            {
                math::Vector3(
                    center.x + offset,
                    ground_height,
                    center.z - half
                ),
                math::Vector3(
                    center.x + offset,
                    ground_height,
                    center.z + half
                ),
                math::Vector3(
                    center.x - half,
                    ground_height,
                    center.z + offset
                ),
                math::Vector3(
                    center.x + half,
                    ground_height,
                    center.z + offset
                )
            };

            for (int segment = 0; segment < 2; segment++)
            {
                ImVec2 a;
                ImVec2 b;
                float depth_a = 0.0f;
                float depth_b = 0.0f;
                if (
                    camera.project(
                        points[segment * 2],
                        a,
                        depth_a
                    ) &&
                    camera.project(
                        points[segment * 2 + 1],
                        b,
                        depth_b
                    )
                )
                {
                    emit(a, b, line == divisions / 2);
                }
            }
        }
    }

    void draw_geometry(
        const vector<PreviewPart>& parts,
        const float yaw,
        const float pitch,
        const float zoom,
        const ImVec2& minimum,
        const ImVec2& maximum,
        const int mode,
        const bool show_grid
    )
    {
        math::Vector3 bounds_min;
        math::Vector3 bounds_max;
        size_t triangle_count = 0;
        if (
            !compute_preview_bounds(
                parts,
                bounds_min,
                bounds_max,
                triangle_count
            )
        )
        {
            return;
        }

        const math::Vector3 center =
            (bounds_min + bounds_max) * 0.5f;
        const float radius = preview_radius_from_bounds(
            bounds_min,
            bounds_max
        );
        const OrbitCamera camera = make_orbit_camera(
            center,
            radius,
            yaw,
            pitch,
            zoom,
            minimum,
            maximum
        );

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        if (show_grid)
        {
            emit_orbit_grid(
                camera,
                center,
                bounds_min.y,
                radius,
                [draw_list](
                    const ImVec2& a,
                    const ImVec2& b,
                    const bool axis
                )
                {
                    draw_list->AddLine(
                        a,
                        b,
                        axis
                            ? IM_COL32(105, 125, 150, 120)
                            : IM_COL32(62, 72, 88, 85)
                    );
                }
            );
        }

        emit_wireframe(
            parts,
            camera,
            radius,
            triangle_count,
            14000,
            [draw_list, mode](
                const ImVec2& a,
                const ImVec2& b,
                const ImVec2& c,
                const float proximity,
                const bool front_facing
            )
            {
                if (mode == 1)
                {
                    const int shade = static_cast<int>(
                        proximity * 55.0f
                    );
                    const ImU32 fill = front_facing
                        ? IM_COL32(
                            45 + shade,
                            125 + shade,
                            190 + shade,
                            255
                        )
                        : IM_COL32(
                            30 + shade / 3,
                            48 + shade / 3,
                            68 + shade / 3,
                            255
                        );
                    draw_list->AddTriangleFilled(a, b, c, fill);
                    return;
                }

                if (mode == 2)
                {
                    if (front_facing)
                    {
                        const float size =
                            1.0f + proximity * 1.5f;
                        const ImU32 color =
                            IM_COL32(125, 205, 255, 210);
                        draw_list->AddCircleFilled(a, size, color, 6);
                        draw_list->AddCircleFilled(b, size, color, 6);
                        draw_list->AddCircleFilled(c, size, color, 6);
                    }
                    return;
                }

                const int alpha = front_facing
                    ? static_cast<int>(
                        80.0f +
                        proximity * 145.0f
                    )
                    : static_cast<int>(
                        20.0f + proximity * 40.0f
                    );
                const ImU32 color = front_facing
                    ? IM_COL32(120, 195, 255, alpha)
                    : IM_COL32(90, 120, 150, alpha);
                draw_list->AddLine(a, b, color);
                draw_list->AddLine(b, c, color);
                draw_list->AddLine(c, a, color);
            }
        );
    }

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
            // strip trailing separators so catalog roots like
            // mcp_resources/ still match child asset files
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

    const auto handler = [this](const McpRequest& request)
    {
        return HandleMcpCommand(request);
    };
    RegisterMcpCommand("asset_viewer_open", handler);
    RegisterMcpCommand("asset_viewer_status", handler);
    RegisterMcpCommand("asset_viewer_select", handler);
    RegisterMcpCommand("asset_viewer_preview_entity", handler);
    RegisterMcpCommand("asset_viewer_set_view", handler);
    RegisterMcpCommand("asset_viewer_screenshot", handler);
}

AssetViewer::~AssetViewer()
{
    UnregisterMcpCommand("asset_viewer_open");
    UnregisterMcpCommand("asset_viewer_status");
    UnregisterMcpCommand("asset_viewer_select");
    UnregisterMcpCommand("asset_viewer_preview_entity");
    UnregisterMcpCommand("asset_viewer_set_view");
    UnregisterMcpCommand("asset_viewer_screenshot");
}

void AssetViewer::OnVisible()
{
    RefreshCatalog(true);
    Entity* focused_workspace = nullptr;
    for (Entity* entity : World::GetEntities())
    {
        if (
            entity &&
            entity->HasTag("mcp_focused_asset")
        )
        {
            entity->SetActive(false);
            entity->SetTransient(true);
            focused_workspace = entity;
        }
    }
    if (!PreviewRoot() && focused_workspace)
    {
        PreviewEntity(focused_workspace);
    }
}

void AssetViewer::OnTickVisible()
{
    const string world_file_path = World::GetFilePath();
    if (world_file_path != m_world_file_path)
    {
        m_preview_root_id = 0;
        m_preview_root_owned = false;
        RefreshCatalog(true);
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
                LoadSelectedAsset(false, true);
            }
        }
    }

    // the world can remove the previewed entity at any time, drop the stale id
    // before anything walks the preview hierarchy this frame
    const bool preview_entity_alive = PreviewRoot() != nullptr;
    if (m_preview_root_id != 0 && !preview_entity_alive)
    {
        m_preview_root_id = 0;
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

    DrawStatusBar();
}

void AssetViewer::RefreshCatalog(bool force)
{
    m_next_refresh_check =
        chrono::steady_clock::now() +
        chrono::seconds(1);

    const string world_file_path = World::GetFilePath();
    const string catalog_path =
        World::GetMcpResourceDirectory() +
        "catalog.json";
    const string write_time =
        FileSystem::Exists(catalog_path) ?
        FileSystem::GetLastWriteTime(catalog_path) :
        "";

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

    if (!FileSystem::Exists(m_catalog_path))
    {
        m_status = "The shared asset library is empty.";
        return;
    }

    string source;
    if (!FileSystem::ReadFile(m_catalog_path, source))
    {
        m_status =
            "Catalog could not be read from " +
            m_catalog_path;
        return;
    }

    JsonValue root;
    string parse_error;
    JsonParser parser(source);
    if (!parser.Parse(root, parse_error))
    {
        m_status =
            "Invalid catalog: " +
            parse_error;
        return;
    }

    const JsonValue* schema_version = root.Find("schema_version");
    const JsonValue* assets = root.Find("assets");
    if (
        root.type != JsonValue::Type::Object ||
        !schema_version ||
        static_cast<int>(schema_version->Number()) != 1 ||
        !assets ||
        assets->type != JsonValue::Type::Object
    )
    {
        m_status =
            "Unsupported or invalid asset catalog schema.";
        return;
    }

    for (const auto& [catalog_id, value] : assets->object)
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
            " catalog asset" :
            " catalog assets"
        );
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
    m_material.reset();
    m_texture.reset();
    m_loaded_path.clear();
    m_loaded_write_time.clear();
    m_prefab_entity_count = 0;
    m_working_vertices.clear();
    m_working_indices.clear();
    m_missing_dependencies.clear();
    m_working_modified = false;
    m_working_editable = false;
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
    m_working_vertices.clear();
    m_working_indices.clear();
    m_working_modified = false;
    m_working_editable = false;
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

        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;
        m_mesh->GetGeometry(
            sub_mesh,
            &indices,
            &vertices
        );
        const uint32_t base =
            static_cast<uint32_t>(m_working_vertices.size());
        for (const uint32_t index : indices)
        {
            m_working_indices.push_back(base + index);
        }
        m_working_vertices.insert(
            m_working_vertices.end(),
            vertices.begin(),
            vertices.end()
        );
    }

    // baking rebuilds a single sub-mesh, so editing is limited to those
    m_working_editable = m_mesh->GetSubMeshCount() == 1;
    m_preview_dirty = true;
}

void AssetViewer::SimplifyWorkingGeometry(const float ratio)
{
    // always start from the source so the slider stays absolute, welding first
    // because simplification cannot collapse edges across duplicated vertices
    LoadWorkingGeometry();
    if (m_working_indices.size() < 36)
    {
        return;
    }

    geometry_processing::optimize(
        m_working_vertices,
        m_working_indices
    );
    m_working_modified = true;
    m_preview_dirty = true;

    const size_t target = max<size_t>(
        24,
        static_cast<size_t>(
            static_cast<float>(m_working_indices.size()) *
            clamp(ratio, 0.01f, 1.0f)
        )
    );
    if (target >= m_working_indices.size())
    {
        return;
    }

    geometry_processing::simplify(
        m_working_indices,
        m_working_vertices,
        target,
        true,
        false
    );
}

void AssetViewer::OptimizeWorkingGeometry()
{
    if (
        m_working_vertices.empty() ||
        m_working_indices.size() < 3
    )
    {
        return;
    }

    geometry_processing::optimize(
        m_working_vertices,
        m_working_indices
    );
    m_working_modified = true;
    m_preview_dirty = true;
}

bool AssetViewer::SaveWorkingGeometry()
{
    if (
        !m_mesh ||
        !m_working_editable ||
        m_loaded_path.empty() ||
        m_working_vertices.empty() ||
        m_working_indices.size() < 3
    )
    {
        return false;
    }

    // bake into a standalone mesh so the cached resource is untouched until the
    // file is reloaded below, lods are rebuilt from the edited geometry
    vector<RHI_Vertex_PosTexNorTan> vertices = m_working_vertices;
    vector<uint32_t> indices = m_working_indices;
    Mesh baked;
    baked.SetObjectName(
        FileSystem::GetFileNameWithoutExtensionFromFilePath(
            m_loaded_path
        )
    );
    baked.AddGeometry(
        vertices,
        indices,
        m_working_generate_lods
    );
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
    if (Entity* root = PreviewRoot())
    {
        if (m_preview_root_owned)
        {
            World::RemoveEntity(root);
        }
    }
    m_preview_root_id = 0;
    m_preview_root_owned = false;
    m_preview_dirty = false;
    m_preview_orbiting = false;
}

void AssetViewer::RebuildPreviewScene()
{
    DestroyPreviewScene();
    if (m_loaded_path.empty())
    {
        return;
    }

    // meshes, materials and textures are drawn straight from their own data,
    // only prefabs need entities so their hierarchy can be evaluated
    if (m_mesh || m_material || m_texture)
    {
        return;
    }

    const math::Vector3 preview_center(
        0.0f,
        0.0f,
        0.0f
    );

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

    if (!Prefab::LoadFromFile(
        m_loaded_path,
        root
    ))
    {
        m_status =
            "Prefab could not be loaded into the preview.";
        DestroyPreviewScene();
        return;
    }
    root->SetPosition(preview_center);
    root->SetActive(false);
    m_preview_dirty = false;
}

void AssetViewer::RequestPreviewRender()
{
    m_preview_dirty = false;
}

void AssetViewer::PreviewEntity(Entity* entity)
{
    if (!entity)
    {
        return;
    }

    DestroyPreviewScene();
    m_mesh.reset();
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
    m_visible = true;
    m_preview_yaw = 0.65f;
    m_preview_pitch = 0.35f;
    m_preview_zoom = 1.0f;
    m_status =
        "Previewing focused asset workspace";
    m_preview_dirty = false;
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

    const char* filters[] =
    {
        "All",
        "Meshes",
        "Materials",
        "Prefabs",
        "Textures"
    };
    for (int filter = 0; filter < IM_ARRAYSIZE(filters); filter++)
    {
        if (
            toolbar_toggle(
                filters[filter],
                m_type_filter == filter
            )
        )
        {
            m_type_filter = filter;
        }
        if (filter + 1 < IM_ARRAYSIZE(filters))
        {
            ImGui::SameLine(0.0f, 3.0f * scale);
        }
    }

    const float actions_width = 126.0f * scale;
    const float right =
        ImGui::GetCursorPosX() +
        ImGui::GetContentRegionAvail().x;
    if (ImGui::GetCursorPosX() + actions_width < right)
    {
        ImGui::SameLine();
        ImGui::SetCursorPosX(right - actions_width);
    }
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

    const auto select_asset =
        [this](const int index)
        {
            m_selected_asset = index;
            m_inspector_tab = 0;
            LoadSelectedAsset();
        };
    const auto draw_asset_row =
        [this, scale, &select_asset](
            const int index,
            const bool nested,
            const int child_count
        )
        {
            const AssetEntry& asset = m_assets[index];
            const AssetVersion* version = GetActiveVersion(asset);
            const float row_height =
                (nested ? 44.0f : 58.0f) * scale;
            const bool has_children = child_count > 0;

            ImGui::PushID(asset.id.c_str());
            bool open = false;
            if (has_children)
            {
                ImGui::PushStyleVar(
                    ImGuiStyleVar_FramePadding,
                    ImVec2(
                        4.0f * scale,
                        max(
                            1.0f,
                            (
                                row_height -
                                ImGui::GetTextLineHeight()
                            ) *
                            0.5f
                        )
                    )
                );
                ImGuiTreeNodeFlags flags =
                    ImGuiTreeNodeFlags_SpanAvailWidth |
                    ImGuiTreeNodeFlags_OpenOnArrow |
                    ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (m_selected_asset == index)
                {
                    flags |= ImGuiTreeNodeFlags_Selected;
                }
                open = ImGui::TreeNodeEx(
                    "##asset_row",
                    flags
                );
                ImGui::PopStyleVar();
                if (ImGui::IsItemClicked())
                {
                    select_asset(index);
                }
            }
            else if (
                ImGui::Selectable(
                    "##asset_row",
                    m_selected_asset == index,
                    ImGuiSelectableFlags_None,
                    ImVec2(0.0f, row_height)
                )
            )
            {
                select_asset(index);
            }

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const ImVec2 row_start = ImGui::GetItemRectMin();
            const ImVec2 row_end = ImGui::GetItemRectMax();
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
            draw_list->AddText(
                ImVec2(
                    text_x,
                    row_start.y +
                        (nested ? 4.0f : 8.0f) * scale
                ),
                ImGui::GetColorU32(ImGuiCol_Text),
                display_name.c_str()
            );

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
            if (ImGui::IsItemHovered())
            {
                ImGuiSp::tooltip(
                    (
                        display_name +
                        "\nCatalog id: " +
                        asset.id
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
            ImGui::SetNextItemOpen(
                true,
                ImGuiCond_Always
            );
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
            ImGui::PushID(dependency.c_str());
            ImGui::Dummy(
                ImVec2(0.0f, dependency_height)
            );
            const ImVec2 minimum = ImGui::GetItemRectMin();
            ImDrawList* draw_list =
                ImGui::GetWindowDrawList();
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
            draw_list->AddText(
                ImVec2(
                    minimum.x + 23.0f * scale,
                    minimum.y + 2.0f * scale
                ),
                ImGui::GetColorU32(ImGuiCol_Text),
                name.c_str()
            );
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
            if (ImGui::IsItemHovered())
            {
                ImGuiSp::tooltip(dependency.c_str());
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
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

    ImGui::EndChild();
}

bool AssetViewer::DeleteSelectedAsset()
{
    if (
        m_selected_asset < 0 ||
        m_selected_asset >= static_cast<int>(m_assets.size()) ||
        m_catalog_path.empty()
    )
    {
        m_status = "Select a catalog asset before deleting.";
        return false;
    }

    const string asset_id =
        m_assets[m_selected_asset].id;
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
    const auto asset_iterator =
        assets.object.find(asset_id);
    if (asset_iterator == assets.object.end())
    {
        m_status = "The selected asset is no longer in the catalog.";
        RefreshCatalog(true);
        return false;
    }

    vector<string> owned_paths;
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
    uint32_t failed_file_count = 0;
    for (const string& owned_path : owned_paths)
    {
        if (
            path_is_within(owned_path, library_root) &&
            FileSystem::Exists(owned_path) &&
            !FileSystem::Delete(owned_path)
        )
        {
            failed_file_count++;
        }
    }
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
                failed_file_count++;
            }
        }
    }

    RefreshCatalog(true);
    m_status =
        failed_file_count == 0 ?
        "Deleted asset " + asset_id :
        "Deleted asset, but " +
            to_string(failed_file_count) +
            " owned files could not be removed";
    return true;
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
    for (int tab = 0; tab < tab_count; tab++)
    {
        if (toolbar_toggle(tabs[tab], m_inspector_tab == tab))
        {
            m_inspector_tab = tab;
        }
        if (tab + 1 < tab_count)
        {
            ImGui::SameLine(0.0f, 3.0f * scale);
        }
    }
    ImGui::Separator();

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
                    World::GetMcpResourceDirectory() +
                    "thumbnails/asset_" +
                    asset.id +
                    ".png";
                m_status = SavePreviewScreenshot(
                    path,
                    1024,
                    1024
                )
                    ? "Saved preview to " + path
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

    const uint32_t source_indices = m_mesh->GetIndexCount();
    const uint64_t source_triangles = source_indices / 3;
    const uint64_t working_triangles =
        m_working_indices.size() / 3;
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
        compact_count(m_mesh->GetVertexCount()) +
            " vertices, " +
            compact_count(source_triangles) +
            " triangles"
    );
    detail_row(
        "Working",
        compact_count(m_working_vertices.size()) +
            " vertices, " +
            compact_count(working_triangles) +
            " triangles"
    );
    ImGui::TextDisabled(
        "Current reduction %.1f%%",
        reduction * 100.0f
    );

    ImGui::Spacing();
    ImGui::TextUnformatted("SIMPLIFICATION TARGET");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat(
        "##triangle_target",
        &m_target_ratio,
        0.02f,
        1.0f,
        "%.0f%%",
        ImGuiSliderFlags_AlwaysClamp
    );
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        SimplifyWorkingGeometry(m_target_ratio);
    }
    const float preset_width =
        (
            ImGui::GetContentRegionAvail().x -
            6.0f * ui_scale()
        ) /
        3.0f;
    const float presets[] = { 0.25f, 0.5f, 0.75f };
    const char* preset_labels[] = { "25%", "50%", "75%" };
    for (int index = 0; index < 3; index++)
    {
        if (
            toolbar_toggle(
                preset_labels[index],
                abs(m_target_ratio - presets[index]) < 0.001f,
                ImVec2(preset_width, 0.0f)
            )
        )
        {
            m_target_ratio = presets[index];
            SimplifyWorkingGeometry(m_target_ratio);
        }
        if (index < 2)
        {
            ImGui::SameLine(0.0f, 3.0f * ui_scale());
        }
    }

    ImGui::Spacing();
    if (
        ImGuiSp::button(
            "Apply simplification",
            ImVec2(-1.0f, 0.0f)
        )
    )
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
    ImGui::Checkbox(
        "Generate LODs when saving",
        &m_working_generate_lods
    );
    ImGuiSp::tooltip(
        "Rebuild lower detail levels from the edited geometry"
    );

    if (!m_working_modified)
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
    if (!m_working_modified)
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
        ImGui::TextUnformatted("Overwrite the current mesh version?");
        ImGui::TextDisabled(
            "%llu to %llu triangles, %.1f%% reduction",
            static_cast<unsigned long long>(source_triangles),
            static_cast<unsigned long long>(working_triangles),
            reduction * 100.0f
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

    if (!m_material)
    {
        const char* modes[] = { "Wire", "Solid", "Points" };
        for (int mode = 0; mode < IM_ARRAYSIZE(modes); mode++)
        {
            if (toolbar_toggle(modes[mode], m_preview_mode == mode))
            {
                m_preview_mode = mode;
            }
            if (mode + 1 < IM_ARRAYSIZE(modes))
            {
                ImGui::SameLine(0.0f, 3.0f * scale);
            }
        }
        ImGui::SameLine(0.0f, 8.0f * scale);
        if (toolbar_toggle("Grid", m_preview_show_grid))
        {
            m_preview_show_grid = !m_preview_show_grid;
        }
    }
    else
    {
        ImGui::TextDisabled("Material sphere");
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
        m_preview_dirty = true;
    }
    ImGuiSp::tooltip("Frame asset");
    ImGui::SameLine(0.0f, 3.0f * scale);
    if (ImGui::SmallButton("Reset"))
    {
        m_preview_yaw = 0.65f;
        m_preview_pitch = 0.35f;
        m_preview_zoom = 1.0f;
        m_preview_dirty = true;
    }
    ImGuiSp::tooltip("Reset camera");

    const ImVec2 available =
        ImGui::GetContentRegionAvail();
    const ImVec2 size(
        max(1.0f, available.x),
        max(1.0f, available.y)
    );
    ImGui::InvisibleButton(
        "##asset_preview_canvas",
        size,
        ImGuiButtonFlags_MouseButtonLeft
    );
    const ImVec2 minimum =
        ImGui::GetItemRectMin();
    const ImVec2 maximum =
        ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(
        minimum,
        maximum,
        IM_COL32(15, 18, 24, 255)
    );
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
        minimum,
        maximum,
        IM_COL32(31, 39, 52, 255),
        IM_COL32(24, 30, 41, 255),
        IM_COL32(13, 16, 22, 255),
        IM_COL32(17, 21, 28, 255)
    );
    ImGui::GetWindowDrawList()->PushClipRect(
        minimum,
        maximum,
        true
    );
    if (m_mesh)
    {
        DrawMeshPreview(minimum, maximum);
    }
    else if (m_texture)
    {
        DrawTexturePreview(minimum, maximum);
    }
    else if (m_material)
    {
        DrawMaterialPreview(minimum, maximum);
    }
    else if (PreviewRoot())
    {
        DrawPrefabPreview(minimum, maximum);
    }
    else
    {
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(
                minimum.x + 12.0f,
                minimum.y + 12.0f
            ),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            "Select an asset"
        );
    }
    ImGui::GetWindowDrawList()->PopClipRect();
    const bool preview_hovered =
        ImGui::IsItemHovered();
    if (
        preview_hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)
    )
    {
        m_preview_orbiting = true;
    }
    if (
        preview_hovered &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
    )
    {
        m_preview_zoom = 1.0f;
        m_preview_dirty = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        m_preview_orbiting = false;
    }
    if (preview_hovered)
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f)
        {
            m_preview_zoom = clamp(
                m_preview_zoom *
                (
                    io.MouseWheel > 0.0f ?
                    1.12f :
                    0.89f
                ),
                0.2f,
                8.0f
            );
            m_preview_dirty = true;
        }
    }
    if (
        m_preview_orbiting &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)
    )
    {
        const ImGuiIO& io = ImGui::GetIO();
        // dragging moves the model with the cursor, so the camera goes the other way
        m_preview_yaw -= io.MouseDelta.x * 0.012f;
        m_preview_pitch = clamp(
            m_preview_pitch +
            io.MouseDelta.y * 0.012f,
            -1.45f,
            1.45f
        );
        m_preview_dirty = true;
    }
    if (m_preview_dirty)
    {
        RequestPreviewRender();
    }
    if (preview_hovered)
    {
        ImGuiSp::tooltip(
            "Drag to orbit\nWheel to zoom\nDouble click to frame"
        );
    }
    ImGui::EndChild();
}

void AssetViewer::DrawMeshPreview(
    const ImVec2& minimum,
    const ImVec2& maximum
)
{
    const bool use_working =
        !m_working_vertices.empty() &&
        m_working_indices.size() >= 3;
    vector<PreviewPart> parts(1);
    parts[0].vertices = use_working
        ? &m_working_vertices
        : &m_mesh->GetVertices();
    parts[0].indices = use_working
        ? &m_working_indices
        : &m_mesh->GetIndices();

    draw_geometry(
        parts,
        m_preview_yaw,
        m_preview_pitch,
        m_preview_zoom,
        minimum,
        maximum,
        m_preview_mode,
        m_preview_show_grid
    );

    if (m_preview_show_stats)
    {
        const string statistics =
            to_string(parts[0].vertices->size()) +
            " vertices  " +
            to_string(parts[0].indices->size() / 3) +
            " triangles" +
            (m_working_modified ? "  unsaved" : "");
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(minimum.x + 10.0f, maximum.y - 26.0f),
            IM_COL32(200, 210, 220, 220),
            statistics.c_str()
        );
    }
}

static void draw_texture_image(
    RHI_Texture* texture,
    const ImVec2& minimum,
    const ImVec2& maximum,
    float zoom,
    bool show_stats
);

void AssetViewer::DrawMaterialPreview(
    const ImVec2& minimum,
    const ImVec2& maximum
)
{
    // a textured material is better judged by its color map than by a sphere
    if (
        RHI_Texture* color_map =
            m_material->GetTexture(
                MaterialTextureType::Color,
                0
            )
    )
    {
        draw_texture_image(
            color_map,
            minimum,
            maximum,
            m_preview_zoom,
            m_preview_show_stats
        );
        return;
    }

    const float red =
        m_material->GetProperty(MaterialProperty::ColorR);
    const float green =
        m_material->GetProperty(MaterialProperty::ColorG);
    const float blue =
        m_material->GetProperty(MaterialProperty::ColorB);
    const float roughness =
        m_material->GetProperty(MaterialProperty::Roughness);
    const float metalness =
        m_material->GetProperty(MaterialProperty::Metalness);
    const Color base_color(red, green, blue, 1.0f);
    const ImVec2 center =
        ImVec2(
            (minimum.x + maximum.x) * 0.5f,
            (minimum.y + maximum.y) * 0.5f
        );
    const float radius =
        min(
            maximum.x - minimum.x,
            maximum.y - minimum.y
        ) *
        0.34f *
        m_preview_zoom;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    for (int layer = 24; layer >= 0; layer--)
    {
        const float factor =
            static_cast<float>(layer) / 24.0f;
        const float light =
            0.24f +
            (1.0f - factor) * (0.7f - roughness * 0.3f);
        const Color shaded(
            clamp(red * light + metalness * 0.08f, 0.0f, 1.0f),
            clamp(green * light + metalness * 0.08f, 0.0f, 1.0f),
            clamp(blue * light + metalness * 0.08f, 0.0f, 1.0f),
            1.0f
        );
        const ImVec2 offset =
            ImVec2(
                -radius * 0.16f * (1.0f - factor),
                -radius * 0.2f * (1.0f - factor)
            );
        draw_list->AddCircleFilled(
            ImVec2(
                center.x + offset.x,
                center.y + offset.y
            ),
            radius * factor,
            color_with_alpha(shaded, 1.0f),
            64
        );
    }

    const float orbit_x = cos(m_preview_yaw) * radius * 0.62f;
    const float orbit_y = sin(m_preview_pitch) * radius * 0.38f;
    draw_list->AddCircle(
        center,
        radius,
        color_with_alpha(base_color, 0.9f),
        64,
        2.0f
    );
    draw_list->AddCircleFilled(
        ImVec2(
            center.x - orbit_x * 0.42f,
            center.y - orbit_y - radius * 0.25f
        ),
        max(2.0f, radius * (0.08f + (1.0f - roughness) * 0.08f)),
        IM_COL32(255, 255, 255, 180),
        24
    );

    if (m_preview_show_stats)
    {
        char statistics[128];
        snprintf(
            statistics,
            sizeof(statistics),
            "roughness %.2f  metalness %.2f",
            roughness,
            metalness
        );
        draw_list->AddText(
            ImVec2(minimum.x + 10.0f, maximum.y - 26.0f),
            IM_COL32(200, 210, 220, 220),
            statistics
        );
    }
}

static void draw_texture_image(
    RHI_Texture* texture,
    const ImVec2& minimum,
    const ImVec2& maximum,
    const float zoom,
    const bool show_stats
)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float available_width = maximum.x - minimum.x;
    const float available_height = maximum.y - minimum.y;
    const float texture_width = static_cast<float>(
        max(1u, texture->GetWidth())
    );
    const float texture_height = static_cast<float>(
        max(1u, texture->GetHeight())
    );
    const float aspect = texture_width / texture_height;
    float draw_width = min(
        available_width - 32.0f,
        available_height - 56.0f
    ) * aspect;
    float draw_height = draw_width / aspect;
    if (draw_width > available_width - 32.0f)
    {
        draw_width = available_width - 32.0f;
        draw_height = draw_width / aspect;
    }
    draw_width = max(16.0f, draw_width);
    draw_height = max(16.0f, draw_height);

    const ImVec2 center(
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f - 8.0f
    );
    const ImVec2 image_min(
        center.x - draw_width * 0.5f,
        center.y - draw_height * 0.5f
    );
    const ImVec2 image_max(
        center.x + draw_width * 0.5f,
        center.y + draw_height * 0.5f
    );

    // a checkerboard behind the image so transparent labels read correctly
    constexpr float square = 12.0f;
    for (
        float y = image_min.y;
        y < image_max.y;
        y += square
    )
    {
        for (
            float x = image_min.x;
            x < image_max.x;
            x += square
        )
        {
            const int column = static_cast<int>(
                (x - image_min.x) / square
            );
            const int row = static_cast<int>(
                (y - image_min.y) / square
            );
            draw_list->AddRectFilled(
                ImVec2(x, y),
                ImVec2(
                    min(x + square, image_max.x),
                    min(y + square, image_max.y)
                ),
                ((column + row) & 1)
                    ? IM_COL32(58, 62, 70, 255)
                    : IM_COL32(44, 48, 55, 255)
            );
        }
    }

    // zooming out repeats the texture so tiling seams become obvious
    const float tiling = clamp(
        1.0f / max(0.05f, zoom),
        1.0f,
        6.0f
    );
    draw_list->AddImage(
        reinterpret_cast<ImTextureID>(texture),
        image_min,
        image_max,
        ImVec2(0.0f, 0.0f),
        ImVec2(tiling, tiling)
    );
    draw_list->AddRect(
        image_min,
        image_max,
        IM_COL32(90, 100, 115, 200),
        0.0f,
        1.0f
    );

    if (show_stats)
    {
        char statistics[160];
        snprintf(
            statistics,
            sizeof(statistics),
            "%ux%u  %.1fx tiling",
            texture->GetWidth(),
            texture->GetHeight(),
            tiling
        );
        draw_list->AddText(
            ImVec2(minimum.x + 10.0f, maximum.y - 26.0f),
            IM_COL32(200, 210, 220, 220),
            statistics
        );
    }
}

void AssetViewer::DrawTexturePreview(
    const ImVec2& minimum,
    const ImVec2& maximum
)
{
    draw_texture_image(
        m_texture.get(),
        minimum,
        maximum,
        m_preview_zoom,
        m_preview_show_stats
    );
}

void AssetViewer::DrawPrefabPreview(
    const ImVec2& minimum,
    const ImVec2& maximum
)
{
    vector<Entity*> entities;
    CollectPreviewEntities(entities);
    vector<PreviewPart> parts;
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
        if (
            !mesh ||
            mesh->GetVertices().empty() ||
            mesh->GetIndices().size() < 3
        )
        {
            continue;
        }

        PreviewPart part;
        part.vertices = &mesh->GetVertices();
        part.indices = &mesh->GetIndices();
        part.transform = entity->GetMatrix();
        parts.push_back(part);
    }

    ImDrawList* draw_list =
        ImGui::GetWindowDrawList();
    if (parts.empty())
    {
        float line_y = minimum.y + 12.0f;
        draw_list->AddText(
            ImVec2(minimum.x + 12.0f, line_y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            m_missing_dependencies.empty()
                ? "The prefab has no render mesh geometry"
                : "The prefab references mesh files that no longer exist"
        );
        for (const string& missing : m_missing_dependencies)
        {
            line_y += ImGui::GetTextLineHeightWithSpacing();
            if (line_y > maximum.y - 30.0f)
            {
                break;
            }
            draw_list->AddText(
                ImVec2(minimum.x + 12.0f, line_y),
                IM_COL32(220, 150, 120, 220),
                missing.c_str()
            );
        }
        return;
    }

    draw_geometry(
        parts,
        m_preview_yaw,
        m_preview_pitch,
        m_preview_zoom,
        minimum,
        maximum,
        m_preview_mode,
        m_preview_show_grid
    );

    const auto [vertex_count, index_count] =
        GetPreviewGeometryCounts();
    const string statistics =
        to_string(vertex_count) +
        " vertices  " +
        to_string(index_count / 3) +
        " triangles  " +
        to_string(parts.size()) +
        " parts";
    if (m_preview_show_stats)
    {
        draw_list->AddText(
            ImVec2(
                minimum.x + 10.0f,
                maximum.y - 26.0f
            ),
            IM_COL32(200, 210, 220, 220),
            statistics.c_str()
        );
    }
}

string AssetViewer::HandleMcpCommand(
    const McpRequest& request
)
{
    if (request.command == "asset_viewer_open")
    {
        m_visible = true;
        RefreshCatalog(true);
    }
    else if (request.command == "asset_viewer_select")
    {
        m_visible = true;
        RefreshCatalog(true);
        const optional<string> query =
            argument(request, "asset_id")
                ? argument(request, "asset_id")
                : argument(request, "id")
                    ? argument(request, "id")
                    : argument(request, "name");
        if (!query || query->empty())
        {
            return
                "{\"ok\":false,\"error\":\"asset_id or name is required\"}";
        }

        m_selected_asset = -1;
        for (size_t index = 0; index < m_assets.size(); index++)
        {
            const AssetEntry& asset = m_assets[index];
            if (
                asset.id == *query ||
                asset.name == *query
            )
            {
                m_selected_asset =
                    static_cast<int>(index);
                break;
            }
        }
        if (m_selected_asset < 0)
        {
            return
                "{\"ok\":false,\"error\":\"asset was not found in the active catalog\"}";
        }
        LoadSelectedAsset(true, true);
        if (m_loaded_path.empty())
        {
            return
                "{\"ok\":false,\"error\":" +
                json_string(m_status) +
                "}";
        }
    }
    else if (
        request.command ==
        "asset_viewer_preview_entity"
    )
    {
        const optional<string> id = argument(request, "id");
        if (!id)
        {
            return
                "{\"ok\":false,\"error\":\"id is required\"}";
        }
        uint64_t parsed_id = 0;
        try
        {
            parsed_id = stoull(*id);
        }
        catch (...)
        {
            return
                "{\"ok\":false,\"error\":\"invalid entity id\"}";
        }
        Entity* entity = World::GetEntityById(parsed_id);
        if (!entity)
        {
            return
                "{\"ok\":false,\"error\":\"preview entity was not found\"}";
        }

        PreviewEntity(entity);
    }
    else if (request.command == "asset_viewer_set_view")
    {
        if (
            const optional<string> view =
                argument(request, "view")
        )
        {
            const string normalized = lower_copy(*view);
            if (normalized == "front")
            {
                m_preview_yaw = 0.0f;
                m_preview_pitch = 0.0f;
            }
            else if (normalized == "back")
            {
                m_preview_yaw = 3.14159265f;
                m_preview_pitch = 0.0f;
            }
            else if (normalized == "left")
            {
                m_preview_yaw = -1.57079633f;
                m_preview_pitch = 0.0f;
            }
            else if (normalized == "right")
            {
                m_preview_yaw = 1.57079633f;
                m_preview_pitch = 0.0f;
            }
            else if (normalized == "top")
            {
                m_preview_yaw = 0.0f;
                m_preview_pitch = 1.45f;
            }
            else if (normalized == "bottom")
            {
                m_preview_yaw = 0.0f;
                m_preview_pitch = -1.45f;
            }
            else if (normalized == "perspective")
            {
                m_preview_yaw = 0.65f;
                m_preview_pitch = 0.35f;
            }
            else
            {
                return
                    "{\"ok\":false,\"error\":\"invalid asset viewer view\"}";
            }
        }
        try
        {
            if (
                const optional<string> yaw =
                    argument(request, "yaw")
            )
            {
                m_preview_yaw = stof(*yaw);
            }
            if (
                const optional<string> pitch =
                    argument(request, "pitch")
            )
            {
                m_preview_pitch = clamp(
                    stof(*pitch),
                    -1.45f,
                    1.45f
                );
            }
            if (
                const optional<string> zoom =
                    argument(request, "zoom")
            )
            {
                m_preview_zoom = clamp(
                    stof(*zoom),
                    0.2f,
                    8.0f
                );
            }
        }
        catch (...)
        {
            return
                "{\"ok\":false,\"error\":\"yaw, pitch and zoom must be finite numbers\"}";
        }
        m_visible = true;
        m_preview_dirty = true;
        RequestPreviewRender();
    }
    else if (request.command == "asset_viewer_screenshot")
    {
        if (
            !m_mesh &&
            !m_material &&
            !m_texture &&
            !PreviewRoot()
        )
        {
            return
                "{\"ok\":false,\"error\":\"load an asset preview before taking a screenshot\"}";
        }
        optional<string> requested = argument(request, "path");
        for (
            const char* alias :
            {
                "filename",
                "file",
                "file_path",
                "name"
            }
        )
        {
            if (requested)
            {
                break;
            }
            requested = argument(request, alias);
        }
        const string path = screenshot_path(requested);
        uint32_t width = 768;
        uint32_t height = 768;
        try
        {
            if (
                const optional<string> value =
                    argument(request, "width")
            )
            {
                width = clamp(
                    static_cast<uint32_t>(stoul(*value)),
                    256u,
                    2048u
                );
            }
            if (
                const optional<string> value =
                    argument(request, "height")
            )
            {
                height = clamp(
                    static_cast<uint32_t>(stoul(*value)),
                    256u,
                    2048u
                );
            }
        }
        catch (...)
        {
            return
                "{\"ok\":false,\"error\":\"screenshot dimensions must be integers\"}";
        }
        if (FileSystem::Exists(path))
        {
            FileSystem::Delete(path);
        }
        if (
            !SavePreviewScreenshot(
                path,
                width,
                height
            )
        )
        {
            return
                "{\"ok\":false,\"error\":\"asset preview screenshot could not be saved\"}";
        }
        return
            "{\"ok\":true,\"ready\":true,\"async\":false,\"path\":" +
            json_string(path) +
            ",\"width\":" +
            to_string(width) +
            ",\"height\":" +
            to_string(height) +
            "}";
    }

    string selected_id;
    string selected_name;
    if (
        m_selected_asset >= 0 &&
        m_selected_asset < static_cast<int>(m_assets.size())
    )
    {
        selected_id = m_assets[m_selected_asset].id;
        selected_name = m_assets[m_selected_asset].name;
    }
    const auto [vertex_count, index_count] =
        GetPreviewGeometryCounts();
    return
        "{\"ok\":true,\"visible\":" +
        string(m_visible ? "true" : "false") +
        ",\"selected_asset_id\":" +
        json_string(selected_id) +
        ",\"selected_asset_name\":" +
        json_string(selected_name) +
        ",\"loaded_path\":" +
        json_string(m_loaded_path) +
        ",\"preview_entity_id\":" +
        json_string(
            PreviewRoot() && !m_preview_root_owned
                ? to_string(m_preview_root_id)
                : ""
        ) +
        ",\"yaw\":" +
        to_string(m_preview_yaw) +
        ",\"pitch\":" +
        to_string(m_preview_pitch) +
        ",\"zoom\":" +
        to_string(m_preview_zoom) +
        ",\"vertex_count\":" +
        to_string(vertex_count) +
        ",\"index_count\":" +
        to_string(index_count) +
        "}";
}

bool AssetViewer::SavePreviewScreenshot(
    const string& path,
    const uint32_t width,
    const uint32_t height
) const
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

    vector<uint8_t> pixels(
        static_cast<size_t>(width) * height * 4,
        255
    );
    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            const size_t index =
                (
                    static_cast<size_t>(y) * width +
                    static_cast<size_t>(x)
                ) *
                4;
            const uint8_t grid =
                (
                    x % 32 == 0 ||
                    y % 32 == 0
                ) ?
                34 :
                24;
            pixels[index] = grid;
            pixels[index + 1] = grid + 2;
            pixels[index + 2] = grid + 6;
        }
    }

    vector<PreviewPart> parts;
    if (m_mesh)
    {
        const bool use_working =
            !m_working_vertices.empty() &&
            m_working_indices.size() >= 3;
        PreviewPart part;
        part.vertices = use_working
            ? &m_working_vertices
            : &m_mesh->GetVertices();
        part.indices = use_working
            ? &m_working_indices
            : &m_mesh->GetIndices();
        parts.push_back(part);
    }
    else if (!m_material)
    {
        vector<Entity*> entities;
        CollectPreviewEntities(entities);
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
            if (
                !mesh ||
                mesh->GetVertices().empty() ||
                mesh->GetIndices().size() < 3
            )
            {
                continue;
            }

            PreviewPart part;
            part.vertices = &mesh->GetVertices();
            part.indices = &mesh->GetIndices();
            part.transform = entity->GetMatrix();
            parts.push_back(part);
        }
    }

    if (!parts.empty())
    {
        math::Vector3 bounds_min;
        math::Vector3 bounds_max;
        size_t triangle_count = 0;
        if (
            !compute_preview_bounds(
                parts,
                bounds_min,
                bounds_max,
                triangle_count
            )
        )
        {
            return false;
        }

        const math::Vector3 center =
            (bounds_min + bounds_max) * 0.5f;
        const float radius = preview_radius_from_bounds(
            bounds_min,
            bounds_max
        );
        const OrbitCamera camera = make_orbit_camera(
            center,
            radius,
            m_preview_yaw,
            m_preview_pitch,
            m_preview_zoom,
            ImVec2(0.0f, 0.0f),
            ImVec2(
                static_cast<float>(width),
                static_cast<float>(height)
            )
        );

        emit_orbit_grid(
            camera,
            center,
            bounds_min.y,
            radius,
            [&pixels, width, height](
                const ImVec2& a,
                const ImVec2& b,
                const bool axis
            )
            {
                const array<uint8_t, 4> color = axis
                    ? array<uint8_t, 4>{ 96, 104, 118, 255 }
                    : array<uint8_t, 4>{ 58, 64, 74, 255 };
                draw_line_bounded(
                    pixels,
                    width,
                    height,
                    a,
                    b,
                    color
                );
            }
        );

        emit_wireframe(
            parts,
            camera,
            radius,
            triangle_count,
            16000,
            [&pixels, width, height](
                const ImVec2& a,
                const ImVec2& b,
                const ImVec2& c,
                const float proximity,
                const bool front_facing
            )
            {
                const uint8_t intensity = static_cast<uint8_t>(
                    front_facing
                        ? 120.0f + proximity * 135.0f
                        : 50.0f + proximity * 40.0f
                );
                const array<uint8_t, 4> color =
                {
                    static_cast<uint8_t>(intensity / 2),
                    static_cast<uint8_t>(
                        min<int>(255, intensity)
                    ),
                    255,
                    255
                };
                draw_line_bounded(
                    pixels,
                    width,
                    height,
                    a,
                    b,
                    color
                );
                draw_line_bounded(
                    pixels,
                    width,
                    height,
                    b,
                    c,
                    color
                );
                draw_line_bounded(
                    pixels,
                    width,
                    height,
                    c,
                    a,
                    color
                );
            }
        );
    }
    else if (m_material)
    {
        const int center_x = static_cast<int>(width / 2);
        const int center_y = static_cast<int>(height / 2);
        const int radius =
            static_cast<int>(
                min(width, height) *
                0.34f *
                m_preview_zoom
            );
        const array<uint8_t, 4> color =
        {
            static_cast<uint8_t>(
                clamp(
                    m_material->GetProperty(
                        MaterialProperty::ColorR
                    ),
                    0.0f,
                    1.0f
                ) *
                255.0f
            ),
            static_cast<uint8_t>(
                clamp(
                    m_material->GetProperty(
                        MaterialProperty::ColorG
                    ),
                    0.0f,
                    1.0f
                ) *
                255.0f
            ),
            static_cast<uint8_t>(
                clamp(
                    m_material->GetProperty(
                        MaterialProperty::ColorB
                    ),
                    0.0f,
                    1.0f
                ) *
                255.0f
            ),
            255
        };
        for (int y = -radius; y <= radius; y++)
        {
            const int span = static_cast<int>(
                sqrt(
                    static_cast<float>(
                        radius * radius -
                        y * y
                    )
                )
            );
            for (int x = -span; x <= span; x++)
            {
                set_pixel(
                    pixels,
                    width,
                    height,
                    center_x + x,
                    center_y + y,
                    color
                );
            }
        }
    }
    else
    {
        // an empty placeholder image would be reviewed as if it were the asset
        return false;
    }

    const string directory =
        FileSystem::GetDirectoryFromFilePath(path);
    if (!directory.empty())
    {
        FileSystem::CreateDirectory_(directory);
    }
    ImageImporter::SaveSdrRgba8(
        path,
        width,
        height,
        pixels.data()
    );
    return FileSystem::Exists(path);
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
