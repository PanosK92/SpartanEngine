/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "Hud.h"
#include "ImGui.h"
#include "../core/Engine.h"
#include "imgui/source/imgui.h"
#include "imgui/source/imgui_internal.h"
#include "widgets/Viewport.h"
#include <sol/sol.hpp>
using namespace std;
using namespace spartan::math;

namespace spartan::hud
{
    namespace
    {
        unordered_map<string, ImFont*> fonts;

        // the editor viewport while the editor shows, the whole window otherwise
        bool region(ImVec2& position, ImVec2& size)
        {
            ImGuiContext* context = ImGui::GetCurrentContext();
            if (!context)
            {
                return false;
            }

            const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
            position = main_viewport->Pos;
            size     = main_viewport->Size;
            const Vector2& viewport_position = Viewport::GetScreenPosition();
            const Vector2& viewport_size     = Viewport::GetScreenSize();
            if (Engine::IsFlagSet(EngineMode::EditorVisible) && viewport_size.x > 100.0f && viewport_size.y > 100.0f)
            {
                position = ImVec2(viewport_position.x, viewport_position.y);
                size     = ImVec2(viewport_size.x, viewport_size.y);
            }
            return size.x > 0.0f && size.y > 0.0f;
        }

        // primitives only land while imgui is between new frame and render, outside it they are dropped
        ImDrawList* begin_draw(ImVec2& origin)
        {
            ImGuiContext* context = ImGui::GetCurrentContext();
            if (!context || !context->WithinFrameScope)
            {
                return nullptr;
            }

            ImVec2 size;
            if (!region(origin, size))
            {
                return nullptr;
            }

            ImDrawList* draw_list = ImGui::GetForegroundDrawList();
            draw_list->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), false);
            return draw_list;
        }

        void end_draw(ImDrawList* draw_list)
        {
            draw_list->PopClipRect();
        }

        ImU32 to_color(const Vector4& color)
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w));
        }

        ImFont* get_font(const sol::optional<string>& name)
        {
            const string key = name.value_or("regular");
            auto it = fonts.find(key);
            if (it != fonts.end() && it->second)
            {
                return it->second;
            }
            if (key == "bold" && gui::font_bold)
            {
                return gui::font_bold;
            }
            if (key == "mono" && gui::font_mono)
            {
                return gui::font_mono;
            }
            return gui::font_normal ? gui::font_normal : ImGui::GetFont();
        }

        // letter spacing walks the glyphs one by one, tracked capitals are what titles and menus are made of
        float text_width(ImFont* font, float size, const string& text, float spacing)
        {
            if (spacing == 0.0f)
            {
                return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
            }

            ImFontBaked* baked = font->GetFontBaked(size);
            float width        = 0.0f;
            uint32_t count     = 0;
            const char* cursor = text.c_str();
            const char* end    = cursor + text.size();
            while (cursor < end)
            {
                unsigned int c = 0;
                cursor += ImTextCharFromUtf8(&c, cursor, end);
                width  += baked->GetCharAdvance(static_cast<ImWchar>(c));
                count++;
            }
            return width + spacing * static_cast<float>(count > 0 ? count - 1 : 0);
        }
    }

    void register_for_scripting(sol::state_view state)
    {
        sol::table table = state.create_named_table("Hud");

        table["GetSize"] = []() -> tuple<float, float>
        {
            ImVec2 position, size;
            if (!region(position, size))
            {
                return { 0.0f, 0.0f };
            }
            return { size.x, size.y };
        };

        table["GetMousePosition"] = []() -> tuple<float, float>
        {
            ImVec2 position, size;
            if (!region(position, size))
            {
                return { -1.0f, -1.0f };
            }
            const ImVec2 mouse = ImGui::GetMousePos();
            return { mouse.x - position.x, mouse.y - position.y };
        };

        // path is relative to the working directory (binaries), the name is what Text and TextWidth take
        table["LoadFont"] = [](const string& name, const string& path) -> bool
        {
            if (fonts.count(name))
            {
                return fonts[name] != nullptr;
            }
            if (!ImGui::GetCurrentContext() || !FileSystem::Exists(path))
            {
                SP_LOG_WARNING("hud font %s not found at %s", name.c_str(), path.c_str());
                return false;
            }
            fonts[name] = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 32.0f);
            return fonts[name] != nullptr;
        };

        table["Rect"] = [](float x, float y, float w, float h, const Vector4& color, sol::optional<float> rounding)
        {
            ImVec2 origin;
            if (ImDrawList* draw_list = begin_draw(origin))
            {
                draw_list->AddRectFilled(ImVec2(origin.x + x, origin.y + y), ImVec2(origin.x + x + w, origin.y + y + h), to_color(color), rounding.value_or(0.0f));
                end_draw(draw_list);
            }
        };

        table["RectOutline"] = [](float x, float y, float w, float h, const Vector4& color, sol::optional<float> thickness, sol::optional<float> rounding)
        {
            ImVec2 origin;
            if (ImDrawList* draw_list = begin_draw(origin))
            {
                draw_list->AddRect(ImVec2(origin.x + x, origin.y + y), ImVec2(origin.x + x + w, origin.y + y + h), to_color(color), rounding.value_or(0.0f), thickness.value_or(1.0f));
                end_draw(draw_list);
            }
        };

        // corners clockwise from the top left
        table["RectGradient"] = [](float x, float y, float w, float h, const Vector4& top_left, const Vector4& top_right, const Vector4& bottom_right, const Vector4& bottom_left)
        {
            ImVec2 origin;
            if (ImDrawList* draw_list = begin_draw(origin))
            {
                draw_list->AddRectFilledMultiColor(ImVec2(origin.x + x, origin.y + y), ImVec2(origin.x + x + w, origin.y + y + h), to_color(top_left), to_color(top_right), to_color(bottom_right), to_color(bottom_left));
                end_draw(draw_list);
            }
        };

        table["Line"] = [](float x1, float y1, float x2, float y2, const Vector4& color, sol::optional<float> thickness)
        {
            ImVec2 origin;
            if (ImDrawList* draw_list = begin_draw(origin))
            {
                draw_list->AddLine(ImVec2(origin.x + x1, origin.y + y1), ImVec2(origin.x + x2, origin.y + y2), to_color(color), thickness.value_or(1.0f));
                end_draw(draw_list);
            }
        };

        table["Circle"] = [](float x, float y, float radius, const Vector4& color, sol::optional<float> thickness)
        {
            ImVec2 origin;
            if (ImDrawList* draw_list = begin_draw(origin))
            {
                if (thickness && *thickness > 0.0f)
                {
                    draw_list->AddCircle(ImVec2(origin.x + x, origin.y + y), radius, to_color(color), 0, *thickness);
                }
                else
                {
                    draw_list->AddCircleFilled(ImVec2(origin.x + x, origin.y + y), radius, to_color(color));
                }
                end_draw(draw_list);
            }
        };

        table["Triangle"] = [](float x1, float y1, float x2, float y2, float x3, float y3, const Vector4& color)
        {
            ImVec2 origin;
            if (ImDrawList* draw_list = begin_draw(origin))
            {
                draw_list->AddTriangleFilled(ImVec2(origin.x + x1, origin.y + y1), ImVec2(origin.x + x2, origin.y + y2), ImVec2(origin.x + x3, origin.y + y3), to_color(color));
                end_draw(draw_list);
            }
        };

        table["TextWidth"] = [](const string& text, float size, sol::optional<string> font, sol::optional<float> spacing) -> float
        {
            if (!ImGui::GetCurrentContext())
            {
                return 0.0f;
            }
            return text_width(get_font(font), size, text, spacing.value_or(0.0f));
        };

        // align 0 left, 0.5 centre, 1 right of x; y is the top of the line; returns the drawn width
        table["Text"] = [](const string& text, float x, float y, float size, const Vector4& color, sol::optional<string> font_name, sol::optional<float> align, sol::optional<float> spacing) -> float
        {
            ImVec2 origin;
            ImDrawList* draw_list = begin_draw(origin);
            if (!draw_list)
            {
                return 0.0f;
            }

            ImFont* font            = get_font(font_name);
            const float tracking    = spacing.value_or(0.0f);
            const float width       = text_width(font, size, text, tracking);
            const ImU32 color_u32   = to_color(color);
            ImVec2 position(origin.x + x - width * align.value_or(0.0f), origin.y + y);

            if (tracking == 0.0f)
            {
                draw_list->AddText(font, size, position, color_u32, text.c_str());
            }
            else
            {
                ImFontBaked* baked = font->GetFontBaked(size);
                const char* cursor = text.c_str();
                const char* end    = cursor + text.size();
                while (cursor < end)
                {
                    unsigned int c         = 0;
                    const char* glyph_end  = cursor + ImTextCharFromUtf8(&c, cursor, end);
                    draw_list->AddText(font, size, position, color_u32, cursor, glyph_end);
                    position.x            += baked->GetCharAdvance(static_cast<ImWchar>(c)) + tracking;
                    cursor                 = glyph_end;
                }
            }

            end_draw(draw_list);
            return width;
        };
    }
}
