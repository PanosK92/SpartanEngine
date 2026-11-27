/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =============================
#include "pch.h"
#include "editor/Selection.h"
#include "world/CameraController.h"
#include "../EditorHistory.h"
#include "Viewport.h"
#include "AssetBrowser.h"
#include "WorldViewer.h"
#include "Properties.h"
#include "TerrainEditor.h"
#include "rhi/RHI_Device.h"
#include "rendering/Renderer.h"
#include "rendering/Material.h"
#include "profiling/Profiler.h"
#include "resource/ResourceCache.h"
#include "world/Prefab.h"
#include "world/components/Render.h"
#include "world/components/Camera.h"
#include "math/Ray.h"
#include "../GeneralWindows.h"
#include "../WorldPreviews.h"
#include "../windows/WorldSelector.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Style.h"
#include "../imgui/ImGui_TransformGizmo.h"
#include "core/Timer.h"
//========================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

namespace
{
    bool first_frame           = true;
    bool initial_output_synced = false;
    uint32_t width_previous    = 0;
    uint32_t height_previous   = 0;
    double resize_settle_timer = 0.0;

    // drag preview state, the entity is tracked by id so a deletion mid-drag cannot dangle on revert
    uint64_t             preview_entity_id            = 0;
    shared_ptr<Material> preview_original_material;
    bool                 preview_original_was_default = false;
    bool                 preview_drag_was_active      = false;

    // triangle precision picking, aabbs alone fail because a gltf scene wrapper swallows the whole world
    Entity* pick_entity_under_cursor()
    {
        Camera* camera = World::GetCamera();
        if (!camera)
        {
            return nullptr;
        }

        return spartan::Selection::FindEntityUnderCursor(*camera);
    }

    struct WelcomeCarouselState
    {
        float scroll_x            = 0.0f;
        float scroll_target_x     = 0.0f;
        float scroll_velocity     = 0.0f;
        bool  use_target          = false;
        bool  dragging            = false;
        bool  drag_moved          = false;
        int   pressed_card_index  = -1;
        float drag_start_mouse_x  = 0.0f;
        float drag_start_scroll_x = 0.0f;
        float drag_last_mouse_x   = 0.0f;
        float drag_smoothed_vel   = 0.0f;
    };
    WelcomeCarouselState welcome_carousel;

    bool draw_carousel_arrow_button(const char* id, const ImVec2& center, float radius, bool points_right, float visibility)
    {
        if (visibility <= 0.02f)
        {
            return false;
        }

        const ImVec2 btn_min(IM_ROUND(center.x - radius), IM_ROUND(center.y - radius));
        const ImVec2 btn_size(IM_ROUND(radius * 2.0f), IM_ROUND(radius * 2.0f));
        ImGui::SetCursorScreenPos(btn_min);
        const bool clicked = ImGui::InvisibleButton(id, btn_size);
        const bool hovered = ImGui::IsItemHovered();
        const bool held    = ImGui::IsItemActive();

        const float hover_t = ImGui::EditorUi::animate(ImGui::GetID(id), hovered ? 1.0f : 0.0f, 14.0f);
        if (hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        ImDrawList* draw     = ImGui::GetWindowDrawList();
        const float border_w = max(1.0f, IM_ROUND(ImGui::EditorUi::scaled(1.0f)));
        const ImVec4 accent  = ImGui::Style::color_accent_1;

        if (hover_t > 0.01f)
        {
            ImGui::EditorUi::draw_glow(draw, btn_min, ImVec2(btn_min.x + btn_size.x, btn_min.y + btn_size.y), accent, radius, ImGui::EditorUi::scaled(10.0f), 0.35f * hover_t * visibility);
        }

        const ImVec4 bg  = ImGui::Style::lerp(ImGui::Style::color_panel, ImGui::Style::color_surface_hover, held ? 1.0f : (0.25f + 0.65f * hover_t));
        const ImVec4 rim = ImGui::Style::lerp(ImGui::Style::color_border_strong, ImGui::EditorUi::alpha(accent, 0.85f), hover_t);
        draw->AddCircleFilled(center, radius, ImGui::EditorUi::color(ImGui::EditorUi::alpha(bg, 0.90f * visibility)), 32);
        draw->AddCircle(center, radius, ImGui::EditorUi::color(ImGui::EditorUi::alpha(rim, visibility)), 32, border_w);

        // crisp vector chevron
        const float dir     = points_right ? 1.0f : -1.0f;
        const float half_h  = radius * 0.34f;
        const float half_w  = radius * 0.20f;
        const float shift_x = dir * radius * 0.04f;
        const ImVec2 pts[3] =
        {
            ImVec2(center.x - dir * half_w + shift_x, center.y - half_h),
            ImVec2(center.x + dir * half_w + shift_x, center.y),
            ImVec2(center.x - dir * half_w + shift_x, center.y + half_h)
        };
        const ImVec4 chevron_col = ImGui::Style::lerp(ImGui::Style::color_text_muted, ImGui::Style::color_accent_hi, 0.35f + 0.65f * hover_t);
        const ImU32 chevron_u32  = ImGui::EditorUi::color(ImGui::EditorUi::alpha(chevron_col, visibility));
        const float chevron_w    = max(1.5f, ImGui::EditorUi::scaled(1.75f));
        draw->AddLine(pts[0], pts[1], chevron_u32, chevron_w);
        draw->AddLine(pts[1], pts[2], chevron_u32, chevron_w);

        return clicked;
    }

    string format_world_display_name(const string& raw_name)
    {
        string name = raw_name;
        const string suffix = "_example";
        if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            name.resize(name.size() - suffix.size());
        }

        string formatted;
        formatted.reserve(name.size());
        bool capitalize_next = true;
        for (char c : name)
        {
            if (c == '_' || c == '-')
            {
                if (!formatted.empty() && formatted.back() != ' ')
                {
                    formatted.push_back(' ');
                }
                capitalize_next = true;
            }
            else
            {
                formatted.push_back(capitalize_next ? static_cast<char>(toupper(static_cast<unsigned char>(c))) : c);
                capitalize_next = false;
            }
        }
        return formatted.empty() ? raw_name : formatted;
    }

    string truncate_single_line(ImFont* font, float font_size, const string& text, float max_width)
    {
        if (text.empty() || max_width <= 0.0f)
        {
            return {};
        }

        if (font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text.c_str()).x <= max_width)
        {
            return text;
        }

        const char* ellipsis = "...";
        const float ellipsis_w = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, ellipsis).x;
        if (ellipsis_w >= max_width)
        {
            return {};
        }

        size_t len = text.size();
        while (len > 0)
        {
            const float w = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text.c_str(), text.c_str() + len).x;
            if (w + ellipsis_w <= max_width)
            {
                break;
            }
            --len;
        }
        while (len > 0 && text[len - 1] == ' ')
        {
            --len;
        }
        return len > 0 ? text.substr(0, len) + ellipsis : string();
    }

    bool draw_welcome_action_button(const char* id, const ImVec2& pos, const ImVec2& size, const char* label, const char* badge, bool primary)
    {
        ImGui::SetCursorScreenPos(pos);
        const bool clicked = ImGui::InvisibleButton(id, size);
        const bool hovered = ImGui::IsItemHovered();
        const bool held    = ImGui::IsItemActive();

        const float hover_t = ImGui::EditorUi::animate(ImGui::GetID(id), hovered ? 1.0f : 0.0f, 12.0f);
        if (hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        ImDrawList* draw     = ImGui::GetWindowDrawList();
        const ImVec2 min_pos = ImVec2(IM_ROUND(pos.x), IM_ROUND(pos.y));
        const ImVec2 max_pos = ImVec2(IM_ROUND(pos.x + size.x), IM_ROUND(pos.y + size.y));
        const float rounding = ImGui::EditorUi::scaled(7.0f);
        const float border_w = max(1.0f, IM_ROUND(ImGui::EditorUi::scaled(1.0f)));

        const ImVec4 accent  = ImGui::Style::color_accent_1;
        if (primary)
        {
            if (hover_t > 0.01f)
            {
                ImGui::EditorUi::draw_glow(draw, min_pos, max_pos, accent, rounding, ImGui::EditorUi::scaled(10.0f), 0.35f * hover_t);
            }
            const float fill_a   = held ? 0.28f : (0.14f + 0.09f * hover_t);
            const float border_a = held ? 0.90f : (0.48f + 0.38f * hover_t);
            draw->AddRectFilled(min_pos, max_pos, ImGui::EditorUi::color(ImGui::EditorUi::alpha(accent, fill_a)), rounding);
            draw->AddRect(min_pos, max_pos, ImGui::EditorUi::color(ImGui::EditorUi::alpha(accent, border_a)), rounding, border_w);
        }
        else
        {
            const ImVec4 base_bg = ImGui::Style::lerp(ImGui::Style::color_panel, ImGui::Style::color_surface_hover, held ? 1.0f : hover_t * 0.85f);
            const ImVec4 rim     = ImGui::Style::lerp(ImGui::Style::color_border_strong, ImGui::EditorUi::alpha(accent, 0.55f), hover_t);
            draw->AddRectFilled(min_pos, max_pos, ImGui::EditorUi::color(base_bg), rounding);
            draw->AddRect(min_pos, max_pos, ImGui::EditorUi::color(rim), rounding, border_w);
        }

        ImFont* font          = Editor::font_bold ? Editor::font_bold : ImGui::GetFont();
        const float font_size = ImGui::GetFontSize();
        const ImVec2 label_sz = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label);

        const bool has_badge  = badge && *badge;
        const float badge_gap = has_badge ? ImGui::EditorUi::scaled(8.0f) : 0.0f;
        const float badge_px  = ImGui::EditorUi::scaled(6.0f);
        const float badge_fs  = font_size * 0.84f;
        const ImVec2 badge_ts = has_badge ? ImGui::GetFont()->CalcTextSizeA(badge_fs, FLT_MAX, 0.0f, badge) : ImVec2(0.0f, 0.0f);
        const float badge_w   = has_badge ? badge_ts.x + badge_px * 2.0f : 0.0f;
        const float badge_h   = has_badge ? badge_ts.y + ImGui::EditorUi::scaled(4.0f) : 0.0f;

        const float content_w = label_sz.x + badge_gap + badge_w;
        const float start_x   = IM_ROUND(min_pos.x + (size.x - content_w) * 0.5f);
        const float text_y    = IM_ROUND(min_pos.y + (size.y - label_sz.y) * 0.5f);

        const ImVec4 text_col = primary ? ImGui::Style::lerp(ImGui::Style::color_text, accent, 0.25f) : ImGui::Style::color_text;
        draw->AddText(font, font_size, ImVec2(start_x, text_y), ImGui::EditorUi::color(text_col), label);

        if (has_badge)
        {
            const ImVec2 b_min(IM_ROUND(start_x + label_sz.x + badge_gap), IM_ROUND(min_pos.y + (size.y - badge_h) * 0.5f));
            const ImVec2 b_max(IM_ROUND(b_min.x + badge_w), IM_ROUND(b_min.y + badge_h));
            draw->AddRectFilled(b_min, b_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(accent, 0.20f)), badge_h * 0.5f);
            draw->AddText(ImGui::GetFont(), badge_fs, ImVec2(IM_ROUND(b_min.x + badge_px), IM_ROUND(b_min.y + (badge_h - badge_ts.y) * 0.5f)), ImGui::EditorUi::color(ImGui::Style::color_accent_hi), badge);
        }

        return clicked;
    }

    void draw_radial_glow(ImDrawList* draw, const ImVec2& center, float radius_x, float radius_y, const ImVec4& color, float center_alpha, int segments = 48)
    {
        if (center_alpha <= 0.001f || radius_x <= 1.0f || radius_y <= 1.0f)
        {
            return;
        }

        const ImU32 col_center = ImGui::EditorUi::color(ImGui::EditorUi::alpha(color, center_alpha));
        const ImU32 col_edge   = ImGui::EditorUi::color(ImGui::EditorUi::alpha(color, 0.0f));
        const ImVec2 uv_white  = ImGui::GetFontTexUvWhitePixel();

        draw->PrimReserve(segments * 3, segments + 1);
        const ImDrawIdx center_idx = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
        draw->PrimWriteVtx(center, uv_white, col_center);
        for (int i = 0; i < segments; ++i)
        {
            const float angle = (static_cast<float>(i) / static_cast<float>(segments)) * 6.2831853f;
            draw->PrimWriteVtx(ImVec2(center.x + cosf(angle) * radius_x, center.y + sinf(angle) * radius_y), uv_white, col_edge);
        }
        for (int i = 0; i < segments; ++i)
        {
            const ImDrawIdx a = static_cast<ImDrawIdx>(center_idx + 1 + i);
            const ImDrawIdx b = static_cast<ImDrawIdx>(center_idx + 1 + ((i + 1) % segments));
            draw->PrimWriteIdx(center_idx);
            draw->PrimWriteIdx(a);
            draw->PrimWriteIdx(b);
        }
    }

    struct LightBeamSpec
    {
        float anchor_x;    // 0..1 normalized across viewport width
        float angle_rad;   // diagonal tilt
        float width_ratio; // fraction of viewport width
        float drift_amp;
        float drift_speed;
        float pulse_speed;
        float phase;
        float depth;
        ImVec4 core_color;
        ImVec4 fringe_color;
        float peak_alpha;
    };

    void draw_light_beam(ImDrawList* draw, const ImVec2& rect_min, float avail_w, float avail_h, float time, const ImVec2& parallax, const LightBeamSpec& b)
    {
        const float diag    = sqrtf(avail_w * avail_w + avail_h * avail_h) * 0.78f;
        const float dir_x   = cosf(b.angle_rad);
        const float dir_y   = sinf(b.angle_rad);
        const float perp_x  = -dir_y;
        const float perp_y  = dir_x;

        const float drift   = sinf(time * b.drift_speed + b.phase) * b.drift_amp * avail_w;
        const float cx      = rect_min.x + avail_w * b.anchor_x + drift + parallax.x * b.depth;
        const float cy      = rect_min.y + avail_h * 0.44f + parallax.y * b.depth;

        const float breathe = 0.72f + 0.28f * sinf(time * b.pulse_speed + b.phase * 1.9f);
        const float half_w  = b.width_ratio * avail_w * 0.5f * (0.92f + 0.08f * cosf(time * b.pulse_speed * 0.7f + b.phase));
        const float core_a  = b.peak_alpha * breathe;
        const float edge_a  = core_a * 0.35f;

        // 5-band feathered beam across width (outer-left -> fringe -> bright core -> fringe -> outer-right)
        // and 3 lengthwise stations (top fade -> mid bright -> bottom fade)
        const float offsets[5]   = { -half_w, -half_w * 0.38f, 0.0f, half_w * 0.38f, half_w };
        const float len_t[3]     = { -diag, -diag * 0.12f, diag * 0.95f };
        const float len_alpha[3] = { 0.0f, 1.0f, 0.0f };
        const ImVec2 uv_white    = ImGui::GetFontTexUvWhitePixel();

        draw->PrimReserve(2 * 4 * 6, 3 * 5);
        const ImDrawIdx base_idx = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);

        for (int row = 0; row < 3; ++row)
        {
            const float bx = cx + dir_x * len_t[row];
            const float by = cy + dir_y * len_t[row];
            const float la = len_alpha[row];

            for (int col = 0; col < 5; ++col)
            {
                const float x = bx + perp_x * offsets[col];
                const float y = by + perp_y * offsets[col];

                ImVec4 c = b.fringe_color;
                float a  = 0.0f;
                if (col == 2)
                {
                    c = b.core_color;
                    a = core_a * la;
                }
                else if (col == 1 || col == 3)
                {
                    c = b.fringe_color;
                    a = edge_a * la;
                }

                draw->PrimWriteVtx(ImVec2(x, y), uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(c, a)));
            }
        }

        for (int row = 0; row < 2; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                const ImDrawIdx i00 = static_cast<ImDrawIdx>(base_idx + row * 5 + col);
                const ImDrawIdx i01 = static_cast<ImDrawIdx>(i00 + 1);
                const ImDrawIdx i10 = static_cast<ImDrawIdx>(i00 + 5);
                const ImDrawIdx i11 = static_cast<ImDrawIdx>(i10 + 1);
                draw->PrimWriteIdx(i00); draw->PrimWriteIdx(i01); draw->PrimWriteIdx(i11);
                draw->PrimWriteIdx(i00); draw->PrimWriteIdx(i11); draw->PrimWriteIdx(i10);
            }
        }
    }

    struct PrismShardSpec
    {
        float x_ratio;
        float y_ratio;
        float size_ratio;  // relative to min(avail_w, avail_h)
        float aspect;      // elongation along local x
        float base_angle;
        float rot_speed;
        float drift_x;
        float drift_y;
        float drift_speed;
        float phase;
        float depth;
        float opacity;
        int   shape;       // 0 = beveled 5-sided slab, 1 = triangular prism, 2 = elongated crystal blade
        bool  warm_dispersion;
        bool  cast_caustic;
    };

    void draw_prism_shard(ImDrawList* draw, const ImVec2& rect_min, float avail_w, float avail_h, float time, const ImVec2& parallax, float scale, const PrismShardSpec& p)
    {
        const float min_dim = min(avail_w, avail_h);
        const float radius  = min_dim * p.size_ratio;
        if (radius < 4.0f)
        {
            return;
        }

        const float cx  = rect_min.x + avail_w * p.x_ratio + sinf(time * p.drift_speed + p.phase) * p.drift_x * avail_w + parallax.x * p.depth;
        const float cy  = rect_min.y + avail_h * p.y_ratio + cosf(time * p.drift_speed * 0.83f + p.phase * 1.4f) * p.drift_y * avail_h + parallax.y * p.depth;
        const float ang = p.base_angle + time * p.rot_speed;
        const float ca  = cosf(ang);
        const float sa  = sinf(ang);

        auto to_world = [&](float lx, float ly)
        {
            const float sx = lx * radius * p.aspect;
            const float sy = ly * radius;
            return ImVec2(cx + sx * ca - sy * sa, cy + sx * sa + sy * ca);
        };

        // build varied architectural crystal silhouettes (counter-clockwise in screen space)
        ImVec2 v[5];
        int v_count = 4;
        if (p.shape == 0)
        {
            // chamfered 5-sided optical glass monolith slab
            v_count = 5;
            v[0] = to_world(-0.18f, -1.02f);
            v[1] = to_world( 0.68f, -0.44f);
            v[2] = to_world( 0.56f,  0.66f);
            v[3] = to_world(-0.24f,  1.06f);
            v[4] = to_world(-0.72f,  0.12f);
        }
        else if (p.shape == 1)
        {
            // classic asymmetric 3-sided dispersion glass prism
            v_count = 3;
            v[0] = to_world( 0.04f, -1.04f);
            v[1] = to_world( 0.92f,  0.62f);
            v[2] = to_world(-0.84f,  0.48f);
        }
        else
        {
            // sleek 4-sided crystal blade sliver
            v_count = 4;
            v[0] = to_world( 0.00f, -1.16f);
            v[1] = to_world( 0.42f, -0.14f);
            v[2] = to_world(-0.04f,  1.12f);
            v[3] = to_world(-0.36f,  0.10f);
        }

        const ImVec2 apex = to_world(0.11f * sinf(time * 0.42f + p.phase) - 0.06f, -0.16f + 0.09f * cosf(time * 0.36f + p.phase));

        // virtual optical light direction from upper-left to lower-right
        const float lx = 0.72f;
        const float ly = 0.69f;

        const ImVec4 cyan_accent  = ImGui::Style::color_accent_1;
        const ImVec4 ice_white    = ImVec4(0.86f, 0.97f, 1.00f, 1.0f);
        const ImVec4 prism_violet = ImVec4(0.40f, 0.46f, 0.96f, 1.0f);
        const ImVec4 prism_warm   = ImVec4(0.98f, 0.72f, 0.38f, 1.0f);
        const ImVec4 glass_dark   = ImVec4(0.032f, 0.048f, 0.072f, 1.0f);

        const ImVec2 uv_white = ImGui::GetFontTexUvWhitePixel();

        // projected refracted caustic fan exiting downstream of hero shards
        if (p.cast_caustic)
        {
            const float px          = -ly;
            const float py          = lx;
            const float fan_len     = radius * 2.8f;
            const float start_half  = radius * 0.24f;
            const float end_half    = radius * 1.15f;
            const float caustic_a   = p.opacity * 0.44f * (0.84f + 0.16f * sinf(time * 0.55f + p.phase));

            // inner cyan-white caustic ray
            const ImVec2 s_l(apex.x - px * start_half, apex.y - py * start_half);
            const ImVec2 s_r(apex.x + px * start_half, apex.y + py * start_half);
            const ImVec2 e_l(apex.x + lx * fan_len - px * end_half, apex.y + ly * fan_len - py * end_half);
            const ImVec2 e_r(apex.x + lx * fan_len + px * end_half, apex.y + ly * fan_len + py * end_half);

            const ImVec4 ray_col = p.warm_dispersion ? ImGui::Style::lerp(cyan_accent, prism_warm, 0.24f) : cyan_accent;
            draw->PrimReserve(6, 4);
            const ImDrawIdx c_base = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
            draw->PrimWriteVtx(s_l, uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ice_white, caustic_a * 0.72f)));
            draw->PrimWriteVtx(s_r, uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ray_col,   caustic_a * 0.58f)));
            draw->PrimWriteVtx(e_r, uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ray_col,   0.0f)));
            draw->PrimWriteVtx(e_l, uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ice_white, 0.0f)));
            draw->PrimWriteIdx(c_base); draw->PrimWriteIdx(static_cast<ImDrawIdx>(c_base + 1)); draw->PrimWriteIdx(static_cast<ImDrawIdx>(c_base + 2));
            draw->PrimWriteIdx(c_base); draw->PrimWriteIdx(static_cast<ImDrawIdx>(c_base + 2)); draw->PrimWriteIdx(static_cast<ImDrawIdx>(c_base + 3));

            // delicate spectral dispersion side-rays (warm amber + prism violet split)
            const float split = radius * 0.34f;
            const ImVec2 e_warm(apex.x + lx * fan_len * 0.92f + px * (end_half + split), apex.y + ly * fan_len * 0.92f + py * (end_half + split));
            const ImVec2 e_viol(apex.x + lx * fan_len * 0.92f - px * (end_half + split), apex.y + ly * fan_len * 0.92f - py * (end_half + split));

            draw->PrimReserve(6, 6);
            const ImDrawIdx d_base = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
            draw->PrimWriteVtx(s_r,    uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(prism_warm,   caustic_a * 0.34f)));
            draw->PrimWriteVtx(e_r,    uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(prism_warm,   0.0f)));
            draw->PrimWriteVtx(e_warm, uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(prism_warm,   0.0f)));
            draw->PrimWriteVtx(s_l,    uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(prism_violet, caustic_a * 0.32f)));
            draw->PrimWriteVtx(e_viol, uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(prism_violet, 0.0f)));
            draw->PrimWriteVtx(e_l,    uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(prism_violet, 0.0f)));
            draw->PrimWriteIdx(d_base); draw->PrimWriteIdx(static_cast<ImDrawIdx>(d_base + 1)); draw->PrimWriteIdx(static_cast<ImDrawIdx>(d_base + 2));
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(d_base + 3)); draw->PrimWriteIdx(static_cast<ImDrawIdx>(d_base + 4)); draw->PrimWriteIdx(static_cast<ImDrawIdx>(d_base + 5));
        }

        // soft optical scatter halo around brighter shards
        if (p.opacity >= 0.085f)
        {
            const ImVec4 spill_col = p.warm_dispersion ? ImGui::Style::lerp(cyan_accent, prism_warm, 0.24f) : cyan_accent;
            draw_radial_glow(draw, apex, radius * 1.45f, radius * 1.25f, spill_col, p.opacity * 0.34f, 24);
        }

        // draw beveled translucent glass triangular facets meeting at the internal crystal ridge
        draw->PrimReserve(v_count * 3, v_count * 3);
        const ImDrawIdx base_idx = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);

        for (int f = 0; f < v_count; ++f)
        {
            const ImVec2& a = v[f];
            const ImVec2& b = v[(f + 1) % v_count];

            // outward edge normal to compute facet illumination against the upper-left key light
            const float ex    = b.x - a.x;
            const float ey    = b.y - a.y;
            const float el    = max(sqrtf(ex * ex + ey * ey), 1e-4f);
            const float nx    = ey / el;
            const float ny    = -ex / el;
            const float ndotl = clamp(nx * (-lx) + ny * (-ly), -1.0f, 1.0f);
            const float lit   = clamp(ndotl * 0.5f + 0.5f, 0.0f, 1.0f);
            const float lit_s = lit * lit;

            // crisp architectural glass contrast: shadowed facets drop to smoked obsidian, lit facets catch frosted cyan-white
            const ImVec4 lit_tint    = p.warm_dispersion ? ImGui::Style::lerp(cyan_accent, prism_warm, 0.16f) : cyan_accent;
            const ImVec4 facet_outer = ImGui::Style::lerp(glass_dark, ImGui::Style::lerp(lit_tint, ice_white, lit_s * 0.55f), 0.12f + 0.78f * lit_s);
            const ImVec4 facet_apex  = ImGui::Style::lerp(ImGui::Style::lerp(glass_dark, prism_violet, 0.45f), ice_white, 0.25f + 0.68f * lit);

            const float alpha_outer = p.opacity * (0.28f + 0.64f * lit_s);
            const float alpha_apex  = p.opacity * (0.42f + 0.85f * lit);

            draw->PrimWriteVtx(apex, uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(facet_apex,  alpha_apex)));
            draw->PrimWriteVtx(a,    uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(facet_outer, alpha_outer)));
            draw->PrimWriteVtx(b,    uv_white, ImGui::EditorUi::color(ImGui::EditorUi::alpha(facet_outer, alpha_outer * 0.70f)));

            const ImDrawIdx fi = static_cast<ImDrawIdx>(base_idx + f * 3);
            draw->PrimWriteIdx(fi);
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(fi + 1));
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(fi + 2));
        }

        // delicate internal crystal ridge hairlines
        const float ridge_w = max(1.0f, scale * 0.80f);
        for (int f = 0; f < v_count; ++f)
        {
            draw->AddLine(apex, v[f], ImGui::EditorUi::color(ImGui::EditorUi::alpha(ice_white, p.opacity * 0.88f)), ridge_w);
        }

        // dynamic Fresnel specular rim + crisp dual-band chromatic dispersion glint on light-facing bevels
        const float rim_w = max(1.0f, scale * 1.25f);
        for (int f = 0; f < v_count; ++f)
        {
            const ImVec2& a = v[f];
            const ImVec2& b = v[(f + 1) % v_count];
            const float ex  = b.x - a.x;
            const float ey  = b.y - a.y;
            const float el  = max(sqrtf(ex * ex + ey * ey), 1e-4f);
            const float nx  = ey / el;
            const float ny  = -ex / el;
            const float spec = powf(clamp(nx * (-lx) + ny * (-ly), 0.0f, 1.0f), 1.45f);

            const ImVec4 rim_col = ImGui::Style::lerp(cyan_accent, ice_white, spec * 0.85f);
            const float rim_a    = clamp(p.opacity * (0.65f + 3.20f * spec), 0.0f, 0.68f);
            draw->AddLine(a, b, ImGui::EditorUi::color(ImGui::EditorUi::alpha(rim_col, rim_a)), rim_w);

            // chromatic dispersion split offset outward on light-catching bevel edges
            if (spec > 0.25f)
            {
                const float disp_a    = 1.8f * scale;
                const float disp_b    = 3.4f * scale;
                const ImVec4 col_near = p.warm_dispersion ? prism_warm : cyan_accent;
                const ImVec4 col_far  = p.warm_dispersion ? prism_violet : prism_warm;
                draw->AddLine(
                    ImVec2(a.x + nx * disp_a, a.y + ny * disp_a),
                    ImVec2(b.x + nx * disp_a, b.y + ny * disp_a),
                    ImGui::EditorUi::color(ImGui::EditorUi::alpha(col_near, rim_a * 0.52f)),
                    max(1.0f, scale * 0.95f)
                );
                draw->AddLine(
                    ImVec2(a.x + nx * disp_b, a.y + ny * disp_b),
                    ImVec2(b.x + nx * disp_b, b.y + ny * disp_b),
                    ImGui::EditorUi::color(ImGui::EditorUi::alpha(col_far, rim_a * 0.26f)),
                    max(1.0f, scale * 0.80f)
                );
            }
        }
    }

    void draw_welcome_atmosphere(ImDrawList* draw, const ImVec2& rect_min, const ImVec2& rect_max, float scale)
    {
        const float avail_w = rect_max.x - rect_min.x;
        const float avail_h = rect_max.y - rect_min.y;
        const float time    = static_cast<float>(ImGui::GetTime());

        // gentle damped mouse parallax so shards and light beams separate cleanly in depth
        static ImVec2 smoothed_parallax(0.0f, 0.0f);
        ImVec2 target_parallax(0.0f, 0.0f);
        if (ImGui::IsMouseHoveringRect(rect_min, rect_max, false))
        {
            const ImVec2 mp = ImGui::GetIO().MousePos;
            const float nx  = clamp((mp.x - (rect_min.x + avail_w * 0.5f)) / max(avail_w * 0.5f, 1.0f), -1.0f, 1.0f);
            const float ny  = clamp((mp.y - (rect_min.y + avail_h * 0.5f)) / max(avail_h * 0.5f, 1.0f), -1.0f, 1.0f);
            target_parallax = ImVec2(-nx * ImGui::EditorUi::scaled(24.0f), -ny * ImGui::EditorUi::scaled(16.0f));
        }
        const float dt     = clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
        const float p_ease = 1.0f - expf(-4.5f * dt);
        smoothed_parallax.x += (target_parallax.x - smoothed_parallax.x) * p_ease;
        smoothed_parallax.y += (target_parallax.y - smoothed_parallax.y) * p_ease;

        // 1. Spartan obsidian-graphite architectural studio base (neutral dark graphite, not cobalt blue)
        const ImVec4 col_tl = ImVec4(0.036f, 0.044f, 0.058f, 1.0f);
        const ImVec4 col_tr = ImVec4(0.024f, 0.027f, 0.035f, 1.0f);
        const ImVec4 col_bl = ImVec4(0.020f, 0.022f, 0.028f, 1.0f);
        const ImVec4 col_br = ImVec4(0.028f, 0.036f, 0.046f, 1.0f);
        draw->AddRectFilledMultiColor(rect_min, rect_max, ImGui::EditorUi::color(col_tl), ImGui::EditorUi::color(col_tr), ImGui::EditorUi::color(col_br), ImGui::EditorUi::color(col_bl));

        const ImVec4 cyan_accent  = ImGui::Style::color_accent_1;
        const ImVec4 ice_white    = ImVec4(0.84f, 0.96f, 1.00f, 1.0f);
        const ImVec4 prism_indigo = ImVec4(0.32f, 0.42f, 0.88f, 1.0f);
        const ImVec4 prism_warm   = ImVec4(0.95f, 0.72f, 0.42f, 1.0f);

        // 2. subtle optical key light spilling from upper-left + faint warm counter-caustic lower-right
        draw_radial_glow(
            draw,
            ImVec2(rect_min.x + avail_w * (0.24f + 0.04f * sinf(time * 0.14f)) + smoothed_parallax.x * 0.20f, rect_min.y + avail_h * 0.16f + smoothed_parallax.y * 0.20f),
            avail_w * 0.50f,
            avail_h * 0.56f,
            cyan_accent,
            0.075f
        );
        draw_radial_glow(
            draw,
            ImVec2(rect_min.x + avail_w * (0.76f + 0.04f * cosf(time * 0.12f)) + smoothed_parallax.x * 0.25f, rect_min.y + avail_h * 0.74f + smoothed_parallax.y * 0.25f),
            avail_w * 0.42f,
            avail_h * 0.46f,
            prism_indigo,
            0.052f
        );
        draw_radial_glow(
            draw,
            ImVec2(rect_min.x + avail_w * 0.50f, rect_min.y + avail_h * 0.34f),
            avail_w * 0.32f,
            avail_h * 0.30f,
            ice_white,
            0.028f
        );

        // 3. diagonal volumetric refracted light shafts sweeping across the studio
        const LightBeamSpec beams[] =
        {
            { 0.22f, 0.92f, 0.24f, 0.030f, 0.14f, 0.22f, 0.3f, 0.25f, cyan_accent,  prism_indigo, 0.055f },
            { 0.39f, 0.96f, 0.15f, 0.024f, 0.19f, 0.28f, 1.6f, 0.45f, ice_white,    cyan_accent,  0.070f },
            { 0.52f, 0.94f, 0.08f, 0.018f, 0.23f, 0.34f, 2.9f, 0.65f, ice_white,    cyan_accent,  0.062f },
            { 0.65f, 0.91f, 0.18f, 0.028f, 0.16f, 0.20f, 4.1f, 0.38f, cyan_accent,  prism_warm,   0.042f },
            { 0.80f, 0.95f, 0.14f, 0.022f, 0.13f, 0.25f, 5.4f, 0.30f, prism_indigo, cyan_accent,  0.038f },
        };
        for (const LightBeamSpec& beam : beams)
        {
            draw_light_beam(draw, rect_min, avail_w, avail_h, time, smoothed_parallax, beam);
        }

        // 4. layered translucent 3d-beveled optical prism shards framing the viewport
        const PrismShardSpec shards[] =
        {
            // deep background architectural glass monoliths (large chamfered slabs cropping past edges)
            { 0.04f, 0.22f, 0.29f, 0.82f,  0.22f,  0.020f, 0.012f, 0.010f, 0.14f, 0.2f, 0.24f, 0.068f, 0, false, true  },
            { 0.94f, 0.24f, 0.30f, 0.86f, -0.34f, -0.018f, 0.014f, 0.010f, 0.13f, 1.4f, 0.26f, 0.062f, 0, true,  false },
            { 0.14f, 0.90f, 0.26f, 0.92f,  0.58f,  0.016f, 0.010f, 0.012f, 0.16f, 2.7f, 0.30f, 0.056f, 0, false, false },
            { 0.90f, 0.86f, 0.28f, 0.84f, -0.52f, -0.020f, 0.012f, 0.010f, 0.15f, 3.9f, 0.28f, 0.058f, 0, false, false },

            // mid-ground triangular optical dispersion prisms catching the studio beam
            { 0.22f, 0.22f, 0.115f, 0.88f, -0.28f, -0.042f, 0.015f, 0.014f, 0.21f, 2.1f, 0.65f, 0.115f, 1, true,  true  },
            { 0.78f, 0.24f, 0.110f, 0.85f,  0.42f,  0.038f, 0.014f, 0.013f, 0.19f, 3.3f, 0.62f, 0.108f, 1, false, true  },
            { 0.08f, 0.60f, 0.105f, 0.82f,  0.16f,  0.046f, 0.016f, 0.014f, 0.22f, 0.8f, 0.60f, 0.102f, 1, false, false },
            { 0.89f, 0.62f, 0.115f, 0.86f, -0.22f, -0.044f, 0.016f, 0.014f, 0.20f, 4.5f, 0.64f, 0.100f, 1, true,  false },

            // crisp foreground elongated crystal blades with spectral rim dispersion
            { 0.16f, 0.41f, 0.078f, 0.68f,  0.34f,  0.072f, 0.018f, 0.016f, 0.28f, 1.1f, 0.94f, 0.145f, 2, false, true  },
            { 0.84f, 0.43f, 0.082f, 0.66f, -0.32f, -0.068f, 0.018f, 0.016f, 0.26f, 2.9f, 0.96f, 0.140f, 2, true,  false },
        };
        for (const PrismShardSpec& shard : shards)
        {
            draw_prism_shard(draw, rect_min, avail_w, avail_h, time, smoothed_parallax, scale, shard);
        }

        // 5. fine optical dust specks catching the diagonal light shafts
        constexpr int mote_count = 24;
        for (int i = 0; i < mote_count; ++i)
        {
            const float fi     = static_cast<float>(i);
            const float h1     = fmodf(fi * 0.61803399f + 0.09f, 1.0f);
            const float h2     = fmodf(fi * 0.38196601f + 0.41f, 1.0f);
            const float h3     = fmodf(fi * 0.75487767f + 0.23f, 1.0f);
            const float depth  = 0.35f + 0.65f * h3;
            const float drift  = time * (0.008f + 0.012f * depth);
            const float u      = fmodf(h1 + drift * 0.45f, 1.0f);
            const float v      = fmodf(h2 + drift * 0.32f, 1.0f);

            const float px = rect_min.x + u * avail_w + smoothed_parallax.x * depth * 0.85f;
            const float py = rect_min.y + v * avail_h + smoothed_parallax.y * depth * 0.85f;

            // brighten dust specks that pass through the main diagonal light corridor
            const float beam_dist  = fabsf((u - 0.15f) - (v * 0.65f));
            const float beam_boost = clamp(1.0f - beam_dist * 2.2f, 0.18f, 1.0f);
            const float twinkle    = 0.45f + 0.55f * sinf(time * (0.9f + h3 * 1.2f) + fi * 2.1f);
            const float edge_mask  = clamp(min(min(u, 1.0f - u), min(v, 1.0f - v)) * 6.5f, 0.0f, 1.0f);
            const float alpha      = (0.08f + 0.30f * depth) * beam_boost * twinkle * edge_mask;
            if (alpha <= 0.01f)
            {
                continue;
            }

            const float core_r    = (0.85f + 1.35f * depth) * scale;
            const ImVec4 mote_col = (i % 5 == 0) ? prism_warm : ImGui::Style::lerp(cyan_accent, ice_white, 0.45f + 0.45f * h1);
            if (depth > 0.68f && beam_boost > 0.55f)
            {
                draw_radial_glow(draw, ImVec2(px, py), core_r * 4.2f, core_r * 4.2f, mote_col, alpha * 0.34f, 16);
            }
            draw->AddCircleFilled(ImVec2(px, py), core_r, ImGui::EditorUi::color(ImGui::EditorUi::alpha(mote_col, alpha)), 12);
        }

        // 6. clean obsidian perimeter vignette framing the hero & showcase cards
        const float vig_h  = avail_h * 0.16f;
        const ImU32 vig_hi = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_void, 0.70f));
        const ImU32 vig_lo = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_void, 0.0f));
        draw->AddRectFilledMultiColor(rect_min, ImVec2(rect_max.x, rect_min.y + vig_h), vig_hi, vig_hi, vig_lo, vig_lo);
        draw->AddRectFilledMultiColor(ImVec2(rect_min.x, rect_max.y - vig_h), rect_max, vig_lo, vig_lo, vig_hi, vig_hi);
    }

    void draw_empty_viewport_welcome(const ImVec2& rect_min, const ImVec2& rect_max)
    {
        const float avail_w = rect_max.x - rect_min.x;
        const float avail_h = rect_max.y - rect_min.y;
        if (avail_w <= 16.0f || avail_h <= 16.0f)
        {
            return;
        }

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(rect_min, rect_max, true);

        const float scale = ImGui::EditorUi::scaled(1.0f);

        // optical caustic light shafts, translucent beveled prism shards, and light-caught dust
        draw_welcome_atmosphere(draw, rect_min, rect_max, scale);

        const auto& worlds         = WorldSelector::GetWorlds();
        const float title_size     = ImGui::EditorUi::scaled(36.0f);
        const float tracking       = title_size * 0.55f;
        ImFont* font_bold          = Editor::font_bold ? Editor::font_bold : ImGui::GetFont();
        ImFont* font_regular       = ImGui::GetFont();
        const float title_w        = ImGui::EditorUi::calc_text_tracked("SPARTAN", tracking, font_bold, title_size);
        const char* hint           = "no world loaded  /  open a showcase or start from scratch";
        const float hint_w         = ImGui::EditorUi::micro_label_width(hint, Editor::font_mono_medium);

        // responsive card strip sizing (leaves room for flanking carousel arrows)
        const float margin_x       = ImGui::EditorUi::scaled(64.0f);
        const float strip_max_w    = ImGui::EditorUi::scaled(1140.0f);
        const float strip_w        = min(avail_w - margin_x * 2.0f, strip_max_w);
        const float card_gap       = ImGui::EditorUi::scaled(14.0f);

        int max_cards = 4;
        if (strip_w < ImGui::EditorUi::scaled(520.0f))
        {
            max_cards = 2;
        }
        else if (strip_w < ImGui::EditorUi::scaled(760.0f))
        {
            max_cards = 3;
        }
        const int card_count       = min(max_cards, static_cast<int>(worlds.size()));
        const float card_w         = card_count > 0 ? floor((strip_w - card_gap * (card_count - 1)) / static_cast<float>(card_count)) : 0.0f;
        const float thumb_h        = floor(card_w * 0.56f);
        const float footer_h       = ImGui::EditorUi::scaled(56.0f);
        const float card_h         = thumb_h + footer_h;

        const float rule_gap       = ImGui::EditorUi::scaled(12.0f);
        const float hint_gap       = ImGui::EditorUi::scaled(12.0f);
        const float actions_gap    = ImGui::EditorUi::scaled(24.0f);
        const float btn_h          = ImGui::EditorUi::scaled(34.0f);
        const float section_gap    = ImGui::EditorUi::scaled(34.0f);
        const float header_row_h   = ImGui::EditorUi::scaled(22.0f);
        const float header_gap     = ImGui::EditorUi::scaled(10.0f);

        const float hero_h         = title_size + rule_gap + 1.0f + hint_gap + ImGui::GetTextLineHeight();
        const float actions_h      = actions_gap + btn_h;
        const float cards_block_h  = (card_count > 0) ? (section_gap + header_row_h + header_gap + card_h) : 0.0f;

        const bool show_actions    = avail_w >= ImGui::EditorUi::scaled(360.0f) && avail_h >= hero_h + actions_h + ImGui::EditorUi::scaled(48.0f);
        const bool show_cards      = show_actions && card_count > 0 && strip_w >= ImGui::EditorUi::scaled(360.0f) && avail_h >= hero_h + actions_h + cards_block_h + ImGui::EditorUi::scaled(64.0f);

        const float total_h        = hero_h + (show_actions ? actions_h : 0.0f) + (show_cards ? cards_block_h : 0.0f);
        const float center_x       = (rect_min.x + rect_max.x) * 0.5f;
        float cursor_y             = IM_ROUND(rect_min.y + max(ImGui::EditorUi::scaled(28.0f), (avail_h - total_h) * 0.46f));

        // 1. brand hero
        if (avail_w > title_w + ImGui::EditorUi::scaled(32.0f))
        {
            ImGui::EditorUi::draw_text_tracked(
                draw,
                ImVec2(IM_ROUND(center_x - title_w * 0.5f), cursor_y),
                ImGui::EditorUi::color(ImGui::Style::color_text),
                "SPARTAN",
                tracking,
                font_bold,
                title_size
            );

            const float rule_y = IM_ROUND(cursor_y + title_size + rule_gap);
            const float rule_h = max(1.0f, floor(scale));
            const float rule_w = title_w * 0.58f;
            const ImU32 clear  = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_accent_1, 0.0f));
            const ImU32 lit    = ImGui::EditorUi::color(ImGui::Style::color_accent_1);
            draw->AddRectFilledMultiColor(ImVec2(center_x - rule_w, rule_y), ImVec2(center_x, rule_y + rule_h), clear, lit, lit, clear);
            draw->AddRectFilledMultiColor(ImVec2(center_x, rule_y), ImVec2(center_x + rule_w, rule_y + rule_h), lit, clear, clear, lit);

            if (avail_w > hint_w + ImGui::EditorUi::scaled(32.0f))
            {
                ImGui::EditorUi::draw_micro_label(
                    draw,
                    ImVec2(IM_ROUND(center_x - hint_w * 0.5f), rule_y + hint_gap),
                    ImGui::GetTextLineHeight(),
                    hint,
                    ImGui::Style::color_text_muted,
                    Editor::font_mono_medium
                );
            }
            cursor_y = rule_y + hint_gap + ImGui::GetTextLineHeight();
        }

        // 2. action buttons
        if (show_actions)
        {
            cursor_y += actions_gap;

            char count_badge[16] = {};
            if (!worlds.empty())
            {
                snprintf(count_badge, sizeof(count_badge), "%zu", worlds.size());
            }

            const float btn_gap       = ImGui::EditorUi::scaled(10.0f);
            const float browse_w      = ImGui::EditorUi::scaled(worlds.empty() ? 176.0f : 204.0f);
            const float new_w         = ImGui::EditorUi::scaled(162.0f);
            const float row_w         = browse_w + btn_gap + new_w;
            const float row_x         = IM_ROUND(center_x - row_w * 0.5f);

            if (draw_welcome_action_button("##welcome_browse_worlds", ImVec2(row_x, cursor_y), ImVec2(browse_w, btn_h), "Browse All Worlds", count_badge, true))
            {
                GeneralWindows::SetVisibilityWorlds(true);
            }
            if (draw_welcome_action_button("##welcome_new_world", ImVec2(row_x + browse_w + btn_gap, cursor_y), ImVec2(new_w, btn_h), "New Empty World", nullptr, false))
            {
                spartan::World::New();
            }

            cursor_y += btn_h;
        }

        // 3. quick-launch scrollable showcase carousel
        if (show_cards)
        {
            cursor_y += section_gap;
            const float strip_x0   = IM_ROUND(center_x - strip_w * 0.5f);
            const float strip_x1   = strip_x0 + strip_w;
            const int total_worlds = static_cast<int>(worlds.size());
            const float step_pitch = card_w + card_gap;
            const float content_w  = total_worlds > 0 ? (total_worlds * card_w + max(0, total_worlds - 1) * card_gap) : 0.0f;
            const float max_scroll = max(0.0f, content_w - strip_w);
            const bool scrollable  = max_scroll > 2.0f;

            bool any_recent = false;
            for (int i = 0; i < min(card_count, total_worlds); ++i)
            {
                if (WorldSelector::IsRecent(worlds[i].file_path))
                {
                    any_recent = true;
                    break;
                }
            }

            const char* section_label = any_recent ? "RECENT & FEATURED WORLDS" : "FEATURED EXAMPLE WORLDS";
            ImGui::EditorUi::draw_micro_label(draw, ImVec2(strip_x0, cursor_y), header_row_h, section_label, ImGui::Style::color_text_faint, Editor::font_mono_medium);

            if (total_worlds > card_count)
            {
                char view_all_buf[32];
                snprintf(view_all_buf, sizeof(view_all_buf), "View all %d ->", total_worlds);
                const ImVec2 link_sz = font_regular->CalcTextSizeA(ImGui::GetFontSize() * 0.92f, FLT_MAX, 0.0f, view_all_buf);
                const ImVec2 link_pos(IM_ROUND(strip_x1 - link_sz.x), IM_ROUND(cursor_y + (header_row_h - link_sz.y) * 0.5f));

                ImGui::SetCursorScreenPos(ImVec2(link_pos.x - ImGui::EditorUi::scaled(4.0f), cursor_y));
                const bool link_clicked = ImGui::InvisibleButton("##welcome_view_all", ImVec2(link_sz.x + ImGui::EditorUi::scaled(8.0f), header_row_h));
                const bool link_hovered = ImGui::IsItemHovered();
                if (link_hovered)
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }
                const ImVec4 link_col = link_hovered ? ImGui::Style::color_accent_hi : ImGui::Style::lerp(ImGui::Style::color_text_muted, ImGui::Style::color_accent_1, 0.45f);
                draw->AddText(font_regular, ImGui::GetFontSize() * 0.92f, link_pos, ImGui::EditorUi::color(link_col), view_all_buf);
                if (link_clicked)
                {
                    GeneralWindows::SetVisibilityWorlds(true);
                }
            }

            cursor_y += header_row_h + header_gap;

            // floating left/right chevron buttons placed cleanly beside the card strip
            const float arrow_radius    = ImGui::EditorUi::scaled(17.0f);
            const float arrow_gap       = ImGui::EditorUi::scaled(14.0f);
            const float arrow_y         = IM_ROUND(cursor_y + card_h * 0.5f);
            const bool arrows_outside   = (strip_x0 - rect_min.x) >= (arrow_radius * 2.0f + arrow_gap + ImGui::EditorUi::scaled(10.0f));
            const float left_arrow_x    = arrows_outside ? (strip_x0 - arrow_gap - arrow_radius) : (strip_x0 + arrow_radius + ImGui::EditorUi::scaled(8.0f));
            const float right_arrow_x   = arrows_outside ? (strip_x1 + arrow_gap + arrow_radius) : (strip_x1 - arrow_radius - ImGui::EditorUi::scaled(8.0f));

            const float can_scroll_left  = (scrollable && welcome_carousel.scroll_x > 2.0f) ? 1.0f : 0.0f;
            const float can_scroll_right = (scrollable && welcome_carousel.scroll_x < max_scroll - 2.0f) ? 1.0f : 0.0f;
            const float left_vis         = ImGui::EditorUi::animate(ImGui::GetID("##welcome_arrow_left_vis"), can_scroll_left, 10.0f);
            const float right_vis        = ImGui::EditorUi::animate(ImGui::GetID("##welcome_arrow_right_vis"), can_scroll_right, 10.0f);

            const float dt          = clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
            const ImVec2 mouse_pos  = ImGui::GetIO().MousePos;
            const ImVec2 strip_min(strip_x0, cursor_y);
            const ImVec2 strip_max(strip_x1, cursor_y + card_h);

            // test arrow buttons before drag capture so clicking an arrow never starts a drag
            const ImVec2 left_center(IM_ROUND(left_arrow_x), arrow_y);
            const ImVec2 right_center(IM_ROUND(right_arrow_x), arrow_y);
            const float arrow_step = max(1, card_count - 1) * step_pitch;
            bool arrow_hovered     = false;
            if (left_vis > 0.02f && ImGui::IsMouseHoveringRect(ImVec2(left_center.x - arrow_radius, left_center.y - arrow_radius), ImVec2(left_center.x + arrow_radius, left_center.y + arrow_radius)))
            {
                arrow_hovered = true;
            }
            if (right_vis > 0.02f && ImGui::IsMouseHoveringRect(ImVec2(right_center.x - arrow_radius, right_center.y - arrow_radius), ImVec2(right_center.x + arrow_radius, right_center.y + arrow_radius)))
            {
                arrow_hovered = true;
            }

            // update carousel drag, wheel, and inertia physics
            const bool allow_input = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
            const bool in_strip    = allow_input && !arrow_hovered && ImGui::IsMouseHoveringRect(strip_min, strip_max);

            if (scrollable && in_strip)
            {
                const float wheel = ImGui::GetIO().MouseWheelH != 0.0f ? -ImGui::GetIO().MouseWheelH : -ImGui::GetIO().MouseWheel;
                if (fabsf(wheel) > 0.001f)
                {
                    const float base = welcome_carousel.use_target ? welcome_carousel.scroll_target_x : welcome_carousel.scroll_x;
                    welcome_carousel.scroll_target_x = clamp(base + wheel * step_pitch * 0.85f, 0.0f, max_scroll);
                    welcome_carousel.use_target      = true;
                    welcome_carousel.scroll_velocity = 0.0f;
                }
            }

            if (in_strip && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                welcome_carousel.dragging            = true;
                welcome_carousel.drag_moved          = false;
                welcome_carousel.use_target          = false;
                welcome_carousel.scroll_velocity     = 0.0f;
                welcome_carousel.drag_smoothed_vel   = 0.0f;
                welcome_carousel.drag_start_mouse_x  = mouse_pos.x;
                welcome_carousel.drag_last_mouse_x   = mouse_pos.x;
                welcome_carousel.drag_start_scroll_x = welcome_carousel.scroll_x;

                const float local_x = (mouse_pos.x - strip_x0) + welcome_carousel.scroll_x;
                const int idx       = static_cast<int>(floor(local_x / step_pitch));
                const float in_card = local_x - idx * step_pitch;
                welcome_carousel.pressed_card_index = (idx >= 0 && idx < total_worlds && in_card >= 0.0f && in_card <= card_w) ? idx : -1;
            }

            if (welcome_carousel.dragging)
            {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    const float drag_dx = mouse_pos.x - welcome_carousel.drag_start_mouse_x;
                    if (scrollable && fabsf(drag_dx) > ImGui::EditorUi::scaled(6.0f))
                    {
                        welcome_carousel.drag_moved = true;
                    }

                    if (scrollable && welcome_carousel.drag_moved)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                        float desired = welcome_carousel.drag_start_scroll_x - drag_dx;
                        // soft elastic resistance when pulling past either edge
                        if (desired < 0.0f)
                        {
                            desired *= 0.28f;
                        }
                        else if (desired > max_scroll)
                        {
                            desired = max_scroll + (desired - max_scroll) * 0.28f;
                        }
                        welcome_carousel.scroll_x = desired;

                        if (dt > 0.0005f)
                        {
                            const float inst_vel = -(mouse_pos.x - welcome_carousel.drag_last_mouse_x) / dt;
                            const float blend    = min(1.0f, dt * 18.0f);
                            welcome_carousel.drag_smoothed_vel = welcome_carousel.drag_smoothed_vel + (inst_vel - welcome_carousel.drag_smoothed_vel) * blend;
                        }
                    }
                    welcome_carousel.drag_last_mouse_x = mouse_pos.x;
                }
                else
                {
                    if (welcome_carousel.drag_moved)
                    {
                        const float max_vel = ImGui::EditorUi::scaled(4200.0f);
                        welcome_carousel.scroll_velocity = clamp(welcome_carousel.drag_smoothed_vel, -max_vel, max_vel);
                        welcome_carousel.use_target      = false;
                    }
                    else if (welcome_carousel.pressed_card_index >= 0 && welcome_carousel.pressed_card_index < total_worlds && in_strip)
                    {
                        const float local_x = (mouse_pos.x - strip_x0) + welcome_carousel.scroll_x;
                        const int idx       = static_cast<int>(floor(local_x / step_pitch));
                        if (idx == welcome_carousel.pressed_card_index)
                        {
                            WorldSelector::OpenWorld(worlds[idx].file_path);
                        }
                    }
                    welcome_carousel.dragging           = false;
                    welcome_carousel.pressed_card_index = -1;
                }
            }
            else if (welcome_carousel.use_target)
            {
                welcome_carousel.scroll_target_x = clamp(welcome_carousel.scroll_target_x, 0.0f, max_scroll);
                const float ease = 1.0f - expf(-11.0f * dt);
                welcome_carousel.scroll_x += (welcome_carousel.scroll_target_x - welcome_carousel.scroll_x) * ease;
                if (fabsf(welcome_carousel.scroll_target_x - welcome_carousel.scroll_x) < 0.25f)
                {
                    welcome_carousel.scroll_x   = welcome_carousel.scroll_target_x;
                    welcome_carousel.use_target = false;
                }
            }
            else
            {
                // free inertia coasting + smooth elastic bounce-back at bounds
                if (fabsf(welcome_carousel.scroll_velocity) > 0.0f)
                {
                    welcome_carousel.scroll_x        += welcome_carousel.scroll_velocity * dt;
                    welcome_carousel.scroll_velocity *= expf(-5.2f * dt);
                    if (fabsf(welcome_carousel.scroll_velocity) < ImGui::EditorUi::scaled(4.0f))
                    {
                        welcome_carousel.scroll_velocity = 0.0f;
                    }
                }

                if (welcome_carousel.scroll_x < 0.0f || welcome_carousel.scroll_x > max_scroll)
                {
                    const float clamped = clamp(welcome_carousel.scroll_x, 0.0f, max_scroll);
                    welcome_carousel.scroll_x        += (clamped - welcome_carousel.scroll_x) * (1.0f - expf(-16.0f * dt));
                    welcome_carousel.scroll_velocity *= expf(-18.0f * dt);
                    if (fabsf(welcome_carousel.scroll_x - clamped) < 0.25f)
                    {
                        welcome_carousel.scroll_x        = clamped;
                        welcome_carousel.scroll_velocity = 0.0f;
                    }
                }
            }

            if (!scrollable)
            {
                welcome_carousel.scroll_x        = 0.0f;
                welcome_carousel.scroll_target_x = 0.0f;
                welcome_carousel.scroll_velocity = 0.0f;
                welcome_carousel.use_target      = false;
            }

            const float rounding   = ImGui::EditorUi::scaled(8.0f);
            const float border_w   = max(1.0f, IM_ROUND(scale));
            const float pad_x      = ImGui::EditorUi::scaled(12.0f);
            const float name_fs    = ImGui::GetFontSize() * 1.02f;
            const float desc_fs    = ImGui::GetFontSize() * 0.86f;
            const ImVec4 accent    = ImGui::Style::color_accent_1;

            // clip cards horizontally to the strip while giving generous vertical headroom for hover glow & lift
            const float glow_pad_y = ImGui::EditorUi::scaled(18.0f);
            draw->PushClipRect(ImVec2(strip_x0 - border_w, cursor_y - glow_pad_y), ImVec2(strip_x1 + border_w, cursor_y + card_h + glow_pad_y), true);

            int hovered_world_index = -1;
            bool left_clipped       = false;
            bool right_clipped      = false;
            for (int i = 0; i < total_worlds; ++i)
            {
                const float card_x = strip_x0 + i * step_pitch - welcome_carousel.scroll_x;
                if (card_x + card_w < strip_x0 - ImGui::EditorUi::scaled(24.0f) || card_x > strip_x1 + ImGui::EditorUi::scaled(24.0f))
                {
                    continue;
                }
                left_clipped  |= (card_x < strip_x0 - 1.0f && card_x + card_w > strip_x0 + 1.0f);
                right_clipped |= (card_x < strip_x1 - 1.0f && card_x + card_w > strip_x1 + 1.0f);

                const spartan::WorldMetadata& world = worlds[i];
                const ImVec2 c_min(IM_ROUND(card_x), IM_ROUND(cursor_y));
                const ImVec2 c_max(IM_ROUND(card_x + card_w), IM_ROUND(cursor_y + card_h));

                const bool hovered = in_strip && !welcome_carousel.drag_moved && ImGui::IsMouseHoveringRect(ImVec2(max(c_min.x, strip_x0), c_min.y), ImVec2(min(c_max.x, strip_x1), c_max.y));
                if (hovered)
                {
                    hovered_world_index = i;
                    if (!welcome_carousel.dragging)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    }
                }

                char card_id[32];
                snprintf(card_id, sizeof(card_id), "##welcome_world_%d", i);
                const float hover_t = ImGui::EditorUi::animate(ImGui::GetID(card_id), hovered ? 1.0f : 0.0f, 12.0f);

                // subtle lift on hover
                const float lift = IM_ROUND(hover_t * ImGui::EditorUi::scaled(2.0f));
                const ImVec2 draw_min(c_min.x, c_min.y - lift);
                const ImVec2 draw_max(c_max.x, c_max.y - lift);
                const ImVec2 img_min = draw_min;
                const ImVec2 img_max(draw_max.x, IM_ROUND(draw_min.y + thumb_h));

                // soft outer accent glow on hover
                if (hover_t > 0.01f)
                {
                    ImGui::EditorUi::draw_glow(draw, draw_min, draw_max, accent, rounding, ImGui::EditorUi::scaled(12.0f), 0.32f * hover_t);
                }

                // card base surface
                const ImVec4 card_bg = ImGui::Style::lerp(ImGui::Style::color_panel, ImGui::Style::color_surface, 0.35f + 0.50f * hover_t);
                draw->AddRectFilled(draw_min, draw_max, ImGui::EditorUi::color(card_bg), rounding);

                // thumbnail image (aspect-preserving center crop, top corners rounded)
                draw->AddRectFilled(img_min, img_max, ImGui::EditorUi::color(ImGui::Style::color_canvas_deep), rounding, ImDrawFlags_RoundCornersTop);
                if (RHI_Texture* preview_tex = WorldPreviews::GetTexture(world.file_path); preview_tex && preview_tex->GetRhiResource())
                {
                    const float image_aspect = static_cast<float>(preview_tex->GetWidth()) / static_cast<float>(max(preview_tex->GetHeight(), 1u));
                    const float frame_aspect = (img_max.x - img_min.x) / max(img_max.y - img_min.y, 1.0f);
                    ImVec2 uv_min(0.0f, 0.0f);
                    ImVec2 uv_max(1.0f, 1.0f);
                    if (image_aspect > frame_aspect)
                    {
                        uv_min.x = (1.0f - frame_aspect / image_aspect) * 0.5f;
                        uv_max.x = 1.0f - uv_min.x;
                    }
                    else
                    {
                        uv_min.y = (1.0f - image_aspect / frame_aspect) * 0.5f;
                        uv_max.y = 1.0f - uv_min.y;
                    }
                    const int tint_v = static_cast<int>(232.0f + 23.0f * hover_t);
                    draw->AddImageRounded(
                        reinterpret_cast<ImTextureID>(preview_tex),
                        img_min,
                        img_max,
                        uv_min,
                        uv_max,
                        IM_COL32(tint_v, tint_v, tint_v, 255),
                        rounding,
                        ImDrawFlags_RoundCornersTop
                    );
                }
                else
                {
                    const Icon& world_icon = ResourceCache::GetIcon(IconType::World);
                    if (world_icon.texture && world_icon.texture->GetRhiResource())
                    {
                        const float icon_s = min(img_max.x - img_min.x, img_max.y - img_min.y) * 0.34f;
                        const ImVec2 i_min(img_min.x + (img_max.x - img_min.x - icon_s) * 0.5f, img_min.y + (img_max.y - img_min.y - icon_s) * 0.5f);
                        draw->AddImage(reinterpret_cast<ImTextureID>(world_icon.texture), i_min, ImVec2(i_min.x + icon_s, i_min.y + icon_s), ImVec2(world_icon.uv_min.x, world_icon.uv_min.y), ImVec2(world_icon.uv_max.x, world_icon.uv_max.y), ImGui::EditorUi::color(ImGui::Style::color_text_faint));
                    }
                }

                // subtle bottom gradient scrim blending thumbnail cleanly into card footer
                const float scrim_h   = thumb_h * 0.36f;
                const ImU32 scrim_top = ImGui::EditorUi::color(ImGui::EditorUi::alpha(card_bg, 0.0f));
                const ImU32 scrim_bot = ImGui::EditorUi::color(ImGui::EditorUi::alpha(card_bg, 0.85f));
                draw->AddRectFilledMultiColor(ImVec2(img_min.x, img_max.y - scrim_h), img_max, scrim_top, scrim_top, scrim_bot, scrim_bot);

                // highlight only the most recently opened world so the rest of the thumbnails stay unobstructed
                if (i == 0 && WorldSelector::IsRecent(world.file_path))
                {
                    const char* recent_txt = "LAST OPENED";
                    const float chip_fs    = ImGui::GetFontSize() * 0.72f;
                    const ImVec2 chip_ts   = font_bold->CalcTextSizeA(chip_fs, FLT_MAX, 0.0f, recent_txt);
                    const float chip_px    = ImGui::EditorUi::scaled(8.0f);
                    const float chip_py    = ImGui::EditorUi::scaled(3.0f);
                    const ImVec2 ch_min(img_min.x + ImGui::EditorUi::scaled(8.0f), img_min.y + ImGui::EditorUi::scaled(8.0f));
                    const ImVec2 ch_max(ch_min.x + chip_ts.x + chip_px * 2.0f, ch_min.y + chip_ts.y + chip_py * 2.0f);
                    draw->AddRectFilled(ch_min, ch_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_void, 0.78f)), (ch_max.y - ch_min.y) * 0.5f);
                    draw->AddRect(ch_min, ch_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(accent, 0.55f)), (ch_max.y - ch_min.y) * 0.5f, border_w);
                    draw->AddText(font_bold, chip_fs, ImVec2(ch_min.x + chip_px, ch_min.y + chip_py), ImGui::EditorUi::color(ImGui::Style::color_accent_hi), recent_txt);
                }

                // subtle "OPEN" pill fading in on hover (top-right)
                if (hover_t > 0.02f)
                {
                    const char* open_txt = "OPEN";
                    const float chip_fs  = ImGui::GetFontSize() * 0.72f;
                    const ImVec2 chip_ts = font_bold->CalcTextSizeA(chip_fs, FLT_MAX, 0.0f, open_txt);
                    const float chip_px  = ImGui::EditorUi::scaled(8.0f);
                    const float chip_py  = ImGui::EditorUi::scaled(3.0f);
                    const float chip_w   = chip_ts.x + chip_px * 2.0f;
                    const float chip_h   = chip_ts.y + chip_py * 2.0f;
                    const ImVec2 ch_min(img_max.x - chip_w - ImGui::EditorUi::scaled(8.0f), img_min.y + ImGui::EditorUi::scaled(8.0f));
                    const ImVec2 ch_max(ch_min.x + chip_w, ch_min.y + chip_h);
                    draw->AddRectFilled(ch_min, ch_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(accent, 0.88f * hover_t)), chip_h * 0.5f);
                    draw->AddText(font_bold, chip_fs, ImVec2(ch_min.x + chip_px, ch_min.y + chip_py), ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_void, hover_t)), open_txt);
                }

                // card caption (formatted title + 1-line description)
                const float text_max_w    = card_w - pad_x * 2.0f;
                const string display_name = truncate_single_line(font_bold, name_fs, format_world_display_name(world.name), text_max_w);
                const string desc_source  = world.description.empty() ? world.name : world.description;
                const string desc_line    = truncate_single_line(font_regular, desc_fs, desc_source, text_max_w);

                const float title_y = IM_ROUND(img_max.y + ImGui::EditorUi::scaled(9.0f));
                const float desc_y  = IM_ROUND(title_y + name_fs + ImGui::EditorUi::scaled(4.0f));
                const ImVec4 name_c = ImGui::Style::lerp(ImGui::Style::color_text, ImGui::Style::color_accent_hi, hover_t * 0.35f);
                draw->AddText(font_bold, name_fs, ImVec2(draw_min.x + pad_x, title_y), ImGui::EditorUi::color(name_c), display_name.c_str());
                draw->AddText(font_regular, desc_fs, ImVec2(draw_min.x + pad_x, desc_y), ImGui::EditorUi::color(ImGui::Style::color_text_muted), desc_line.c_str());

                // crisp border
                const ImVec4 rim_col = ImGui::Style::lerp(ImGui::Style::color_border_strong, ImGui::EditorUi::alpha(accent, 0.85f), hover_t);
                draw->AddRect(draw_min, draw_max, ImGui::EditorUi::color(rim_col), rounding, border_w);
            }

            // soft obsidian edge fades appear only while a card is actively crossing an edge
            const float left_fade_t  = ImGui::EditorUi::animate(ImGui::GetID("##welcome_fade_left"), left_clipped ? 1.0f : 0.0f, 12.0f);
            const float right_fade_t = ImGui::EditorUi::animate(ImGui::GetID("##welcome_fade_right"), right_clipped ? 1.0f : 0.0f, 12.0f);
            const float edge_fade_w  = ImGui::EditorUi::scaled(42.0f);
            if (left_fade_t > 0.02f)
            {
                const ImU32 opaque = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_void, 0.84f * left_fade_t));
                const ImU32 clear  = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_void, 0.0f));
                draw->AddRectFilledMultiColor(ImVec2(strip_x0 - border_w, cursor_y - glow_pad_y), ImVec2(strip_x0 + edge_fade_w, cursor_y + card_h + glow_pad_y), opaque, clear, clear, opaque);
            }
            if (right_fade_t > 0.02f)
            {
                const ImU32 opaque = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_void, 0.84f * right_fade_t));
                const ImU32 clear  = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_void, 0.0f));
                draw->AddRectFilledMultiColor(ImVec2(strip_x1 - edge_fade_w, cursor_y - glow_pad_y), ImVec2(strip_x1 + border_w, cursor_y + card_h + glow_pad_y), clear, opaque, opaque, clear);
            }

            draw->PopClipRect();

            // floating navigation chevrons (drawn above the clip rect so they sit crisply beside the strip)
            if (draw_carousel_arrow_button("##welcome_carousel_prev", left_center, arrow_radius, false, left_vis))
            {
                const float base = welcome_carousel.use_target ? welcome_carousel.scroll_target_x : welcome_carousel.scroll_x;
                welcome_carousel.scroll_target_x = max(0.0f, base - arrow_step);
                welcome_carousel.use_target      = true;
                welcome_carousel.scroll_velocity = 0.0f;
            }
            if (draw_carousel_arrow_button("##welcome_carousel_next", right_center, arrow_radius, true, right_vis))
            {
                const float base = welcome_carousel.use_target ? welcome_carousel.scroll_target_x : welcome_carousel.scroll_x;
                welcome_carousel.scroll_target_x = min(max_scroll, base + arrow_step);
                welcome_carousel.use_target      = true;
                welcome_carousel.scroll_velocity = 0.0f;
            }

            // hover tooltip for the active card
            if (hovered_world_index >= 0 && !welcome_carousel.dragging)
            {
                const spartan::WorldMetadata& world = worlds[hovered_world_index];
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(format_world_display_name(world.name).c_str());
                if (!world.description.empty())
                {
                    ImGui::PushTextWrapPos(ImGui::EditorUi::scaled(320.0f));
                    ImGui::TextDisabled("%s", world.description.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::Spacing();
                ImGui::TextColored(accent, scrollable ? "Click to open  /  Drag or scroll for more" : "Click to open");
                ImGui::EndTooltip();
            }

            cursor_y += card_h;

            // minimalist scroll progress track under the carousel
            if (scrollable)
            {
                const float track_gap = ImGui::EditorUi::scaled(14.0f);
                const float track_h   = max(2.0f, ImGui::EditorUi::scaled(2.5f));
                const float track_w   = min(strip_w * 0.24f, ImGui::EditorUi::scaled(200.0f));
                const float track_x0  = IM_ROUND(center_x - track_w * 0.5f);
                const float track_y0  = IM_ROUND(cursor_y + track_gap);
                const float frac_vis  = clamp(strip_w / max(content_w, 1.0f), 0.12f, 1.0f);
                const float frac_pos  = clamp(welcome_carousel.scroll_x / max(max_scroll, 1.0f), 0.0f, 1.0f);
                const float thumb_w   = max(ImGui::EditorUi::scaled(24.0f), track_w * frac_vis);
                const float thumb_x0  = track_x0 + (track_w - thumb_w) * frac_pos;

                draw->AddRectFilled(ImVec2(track_x0, track_y0), ImVec2(track_x0 + track_w, track_y0 + track_h), ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.10f)), track_h * 0.5f);
                draw->AddRectFilled(ImVec2(thumb_x0, track_y0), ImVec2(thumb_x0 + thumb_w, track_y0 + track_h), ImGui::EditorUi::color(ImGui::EditorUi::alpha(accent, 0.72f)), track_h * 0.5f);

                cursor_y += track_gap + track_h;
            }
        }

        // 4. quiet footer hint at the bottom of the empty viewport
        const char* footer_hint = "F1 documentation   /   Ctrl+P controls   /   drop a model or prefab here";
        const float footer_w    = ImGui::EditorUi::micro_label_width(footer_hint, Editor::font_mono_medium);
        const float footer_y    = IM_ROUND(rect_max.y - ImGui::GetTextLineHeight() - ImGui::EditorUi::scaled(18.0f));
        if (avail_w > footer_w + ImGui::EditorUi::scaled(48.0f) && footer_y > cursor_y + ImGui::EditorUi::scaled(22.0f))
        {
            ImGui::EditorUi::draw_micro_label(
                draw,
                ImVec2(IM_ROUND(center_x - footer_w * 0.5f), footer_y),
                ImGui::GetTextLineHeight(),
                footer_hint,
                ImGui::Style::color_text_faint,
                Editor::font_mono_medium
            );
        }

        draw->PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2(rect_min.x, rect_max.y));
    }

    void clear_preview_state()
    {
        preview_entity_id            = 0;
        preview_original_material.reset();
        preview_original_was_default = false;
    }

    void revert_material_preview()
    {
        if (preview_entity_id == 0)
        {
            return;
        }

        if (Entity* entity = World::GetEntityById(preview_entity_id))
        {
            if (Render* render = entity->GetComponent<Render>())
            {
                if (preview_original_was_default)
                {
                    render->SetDefaultMaterial();
                }
                else if (preview_original_material)
                {
                    render->SetMaterial(preview_original_material);
                }
            }
        }

        clear_preview_state();
    }

    void apply_material_preview(Entity* entity, const char* material_path)
    {
        if (!entity || !material_path || !*material_path)
        {
            return;
        }

        Render* render = entity->GetComponent<Render>();
        if (!render)
        {
            return;
        }

        // remember what to restore, the default flag short-circuits the path lookup for engine-owned defaults
        bool was_default = render->IsUsingDefaultMaterial();
        shared_ptr<Material> original;
        if (Material* current = render->GetMaterial(); current && !was_default)
        {
            original = ResourceCache::GetByPath<Material>(current->GetResourceFilePath());
        }

        // cache-aware load, no-op if the material is already in the resource cache
        shared_ptr<Material> dragged = ResourceCache::Load<Material>(material_path);
        if (!dragged)
        {
            return;
        }

        render->SetMaterial(dragged);

        preview_entity_id            = entity->GetObjectId();
        preview_original_material    = original;
        preview_original_was_default = was_default;
    }

    // peek at the active drag-drop payload without accepting, returns the path if it is a material drag
    // returns nullptr when there is no active drag or the active drag is not a material payload
    const char* peek_material_drag_path()
    {
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        if (!payload || !payload->IsDataType(ImGuiSp::GDragDropTypes[(int)ImGuiSp::DragPayloadType::Material].data()))
        {
            return nullptr;
        }

        if (payload->DataSize < static_cast<int>(sizeof(ImGuiSp::DragDropPayload)))
        {
            return nullptr;
        }

        const ImGuiSp::DragDropPayload* sp_payload = static_cast<const ImGuiSp::DragDropPayload*>(payload->Data);
        if (sp_payload->path[0] == '\0')
        {
            return nullptr;
        }

        return sp_payload->path;
    }

    // the overlay layout to restore when it is shown again, 1 full or 2 compact
    float performance_overlay_mode = 1.0f;

    void set_performance_overlay(const float mode)
    {
        ConsoleRegistry::Get().SetValueFromString("r.performance_metrics", to_string(mode));
    }

    // the performance overlay is drawn by the renderer into the frame, its controls live here because only imgui gets the mouse
    struct PerformanceOverlayControls
    {
        bool hovered = false;
        ImVec2 close_min;
        ImVec2 close_max;
        ImVec2 layout_min;
        ImVec2 layout_max;
    };

    PerformanceOverlayControls performance_overlay_controls(const ImVec2& image_min, const ImVec2& image_max)
    {
        PerformanceOverlayControls controls;

        float x0, y0, x1, y1;
        const RHI_Viewport& viewport = Renderer::GetViewport();
        if (!Profiler::GetPerformanceMetricsRect(x0, y0, x1, y1) || viewport.width <= 0.0f || viewport.height <= 0.0f)
        {
            return controls;
        }

        Camera* camera = World::GetCamera();
        if ((camera && camera->GetFlag(CameraFlags::IsControlled)) || !ImGui::IsWindowHovered())
        {
            return controls;
        }

        const float scale_x     = (image_max.x - image_min.x) / viewport.width;
        const float scale_y     = (image_max.y - image_min.y) / viewport.height;
        const ImVec2 panel_min  = ImVec2(image_min.x + x0 * scale_x, image_min.y + y0 * scale_y);
        const ImVec2 panel_max  = ImVec2(image_min.x + x1 * scale_x, image_min.y + y1 * scale_y);

        // a column hanging off the left edge, inside the panel every pixel is a reading
        const float size    = ImGui::EditorUi::scaled(22.0f);
        const float gap     = ImGui::EditorUi::scaled(4.0f);
        const float right   = panel_min.x - gap;
        controls.close_min  = ImVec2(right - size, panel_min.y);
        controls.close_max  = ImVec2(right, panel_min.y + size);
        controls.layout_min = ImVec2(controls.close_min.x, controls.close_max.y + gap);
        controls.layout_max = ImVec2(right, controls.layout_min.y + size);

        // the gap between the column and the panel stays live so the buttons do not vanish on the way over
        const bool in_panel  = ImGui::IsMouseHoveringRect(panel_min, panel_max, false);
        const bool in_column = ImGui::IsMouseHoveringRect(ImVec2(controls.close_min.x, panel_min.y), ImVec2(panel_min.x, controls.layout_max.y), false);
        controls.hovered     = in_panel || in_column;

        return controls;
    }

    void performance_overlay_controls_draw(const PerformanceOverlayControls& controls)
    {
        if (!controls.hovered)
        {
            return;
        }

        namespace ui    = ImGui::EditorUi;
        namespace style = ImGui::Style;

        ImDrawList* draw     = ImGui::GetWindowDrawList();
        const float rounding = ui::scaled(3.0f);
        const float inset    = ui::scaled(7.0f);
        const float stroke   = ui::scaled(1.5f);
        const bool compact   = cvar_performance_metrics.GetValueAs<float>() >= 2.0f;

        auto button = [&](const ImVec2& min, const ImVec2& max, const char* tooltip)
        {
            const bool hovered = ImGui::IsMouseHoveringRect(min, max, false);
            draw->AddRectFilled(min, max, ui::color(hovered ? style::color_surface_hover : ui::alpha(style::color_panel, 0.9f)), rounding);
            draw->AddRect(min, max, ui::color(style::color_border), rounding);
            if (hovered)
            {
                ImGui::SetTooltip("%s", tooltip);
            }

            return hovered;
        };

        const bool close_hovered = button(controls.close_min, controls.close_max, "Hide performance overlay (F7)");
        {
            const ImU32 glyph = ui::color(close_hovered ? style::color_text : style::color_text_muted);
            const ImVec2 a    = ImVec2(controls.close_min.x + inset, controls.close_min.y + inset);
            const ImVec2 b    = ImVec2(controls.close_max.x - inset, controls.close_max.y - inset);
            draw->AddLine(a, b, glyph, stroke);
            draw->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), glyph, stroke);
        }

        const bool layout_hovered = button(controls.layout_min, controls.layout_max, compact ? "Show full overlay" : "Show compact overlay");
        {
            // a bar collapses the panel, an outlined box expands it back
            const ImU32 glyph = ui::color(layout_hovered ? style::color_text : style::color_text_muted);
            const ImVec2 a    = ImVec2(controls.layout_min.x + inset, controls.layout_min.y + inset);
            const ImVec2 b    = ImVec2(controls.layout_max.x - inset, controls.layout_max.y - inset);
            if (compact)
            {
                draw->AddRect(a, b, glyph, 0.0f, stroke);
            }
            else
            {
                const float mid = IM_ROUND((a.y + b.y) * 0.5f);
                draw->AddLine(ImVec2(a.x, mid), ImVec2(b.x, mid), glyph, stroke);
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (close_hovered)
            {
                Viewport::TogglePerformanceOverlay();
            }
            else if (layout_hovered)
            {
                set_performance_overlay(compact ? 1.0f : 2.0f);
            }
        }
    }
}

void Viewport::TogglePerformanceOverlay()
{
    const float mode = cvar_performance_metrics.GetValue();
    if (mode > 0.0f)
    {
        performance_overlay_mode = mode;
        set_performance_overlay(0.0f);
    }
    else
    {
        set_performance_overlay(performance_overlay_mode);
    }
}

Viewport::Viewport(Editor* editor) : Widget(editor)
{
    m_title         = "Viewport";
    m_dock          = WidgetDock::Center;
    m_size_initial  = Vector2(400, 250);
    m_flags        |= ImGuiWindowFlags_NoScrollbar;
    m_padding       = Vector2(0.0f);
}

void Viewport::OnTickVisible()
{
    // get viewport size
    uint32_t width  = static_cast<uint32_t>(ImGui::GetContentRegionAvail().x);
    uint32_t height = static_cast<uint32_t>(ImGui::GetContentRegionAvail().y);

    // update engine's viewport
    if (!first_frame && RHI_Device::IsValidResolution(width, height)) // during the first frame the viewport is not yet initialized (it's size will be something weird)
    {
        const bool size_changed = width_previous != width || height_previous != height;
        if (size_changed)
        {
            Renderer::SetViewport(static_cast<float>(width), static_cast<float>(height));
            width_previous      = width;
            height_previous     = height;
            resize_settle_timer = 0.0;
        }

        if (cvar_resolution_output_match_viewport.GetValueAs<bool>() && !Renderer::IsStereoView())
        {
            const Vector2& output       = Renderer::GetResolutionOutput();
            const bool output_out_of_sync = static_cast<uint32_t>(output.x) != width || static_cast<uint32_t>(output.y) != height;
            if (!output_out_of_sync)
            {
                initial_output_synced = true;
                resize_settle_timer   = 0.0;
            }
            else if (!initial_output_synced)
            {
                Renderer::SetResolutionOutput(width, height);
                initial_output_synced = true;
                resize_settle_timer   = 0.0;
            }
            else if (!size_changed && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                // wait briefly after a resize stops so dragging window or panel borders does not stall reallocating render targets every frame
                resize_settle_timer += Timer::GetDeltaTimeSec();
                if (resize_settle_timer >= 0.15)
                {
                    Renderer::SetResolutionOutput(width, height);
                    resize_settle_timer = 0.0;
                }
            }
        }
        else
        {
            initial_output_synced = true;
            resize_settle_timer   = 0.0;
        }
    }
    first_frame = false;

    // let the input system know about the position of this viewport within the editor
    // this will allow the system to properly calculate a relative mouse position
    const ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 main_viewport_pos = ImGui::GetMainViewport()->Pos;
    const Vector2 offset = Vector2(screen_pos.x - main_viewport_pos.x, screen_pos.y - main_viewport_pos.y);
    Input::SetEditorViewportOffset(offset);

    // publish the viewport's screen-space rect so other systems can snap overlays to it
    m_screen_position = Vector2(screen_pos.x, screen_pos.y);
    m_screen_size     = Vector2(static_cast<float>(width), static_cast<float>(height));

    // draw the image after a potential resolution change call has been made
    if (!World::GetCamera())
    {
        ImGui::SetNextItemAllowOverlap();
    }
    ImGuiSp::image(Renderer::GetRenderTarget(Renderer_RenderTarget::frame_output), ImVec2(static_cast<float>(width), static_cast<float>(height)));

    // cache the image rect for hover tests, isitemhovered can return false during drag-drop
    ImVec2 image_rect_min = ImGui::GetItemRectMin();
    ImVec2 image_rect_max = ImGui::GetItemRectMax();

    // the overlay covers the scene, so clicks on it belong to its controls and never pick through it
    const PerformanceOverlayControls overlay_controls = performance_overlay_controls(image_rect_min, image_rect_max);

    // let the input system know if the mouse is within the viewport
    Input::SetMouseIsInViewport(ImGui::IsItemHovered());

    const bool is_playing = Engine::IsFlagSet(EngineMode::Playing);

    // material drag-preview, runs before the drop handlers so the drop just clears state without reverting
    if (!is_playing)
    {
        const char* drag_path  = peek_material_drag_path();
        bool        drag_active = drag_path != nullptr;
        bool        in_image   = ImGui::IsMouseHoveringRect(image_rect_min, image_rect_max);
        bool        previewing = drag_active && in_image;

        if (previewing)
        {
            Entity*  hovered    = pick_entity_under_cursor();
            uint64_t hovered_id = hovered ? hovered->GetObjectId() : 0;
            if (hovered_id != preview_entity_id)
            {
                revert_material_preview();
                if (hovered)
                {
                    apply_material_preview(hovered, drag_path);
                }
            }
        }
        else if (drag_active)
        {
            // drag still active but cursor moved off the image, restore the original
            revert_material_preview();
        }
        else if (preview_drag_was_active)
        {
            // the imgui drop handler can miss the release frame, so commit here instead of reverting
            clear_preview_state();
        }

        preview_drag_was_active = drag_active;
    }
    else if (preview_drag_was_active)
    {
        revert_material_preview();
        preview_drag_was_active = false;
    }

    // handle model drop
    if (!is_playing)
    {
        if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Model))
        {
            if (payload->path[0] != '\0')
            {
                m_editor->GetWidget<AssetBrowser>()->ShowMeshImportDialog(payload->path);
            }
        }

        // handle prefab drop
        if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Prefab))
        {
            if (payload->path[0] != '\0')
            {
                const char* file_path = payload->path;
                Entity* entity        = World::CreateEntity();
                string name           = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
                entity->SetObjectName(name);
                if (Prefab::LoadFromFile(file_path, entity))
                {
                    entity->SetPrefabFilePath(file_path);

                    // snapshot the loaded hierarchy as the prefab base so later edits persist as overrides
                    entity->MarkPrefabBaseline();
                    editor_history::Created(entity);
                }
                else
                {
                    World::RemoveEntity(entity);
                }
            }
        }

        // handle material drop, the preview already applied the material to the hovered mesh,
        // so commit just means clearing the preview state without restoring the original
        if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Material))
        {
            if (preview_entity_id != 0)
            {
                if (auto entity = World::GetEntityById(preview_entity_id))
                {
                    auto render = entity->GetComponent<Render>();
                    if (render && render->GetMaterial())
                    {
                        auto material = std::static_pointer_cast<Material>(render->GetMaterial()->shared_from_this());
                        revert_material_preview();
                        editor_history::EntityScope history(entity, true);
                        render->SetMaterial(material);
                    }
                }
                clear_preview_state();
            }
            else if (payload->path[0] != '\0')
            {
                // fallback for the unlikely case the drop fires without a prior hover frame
                if (Entity* hovered = pick_entity_under_cursor())
                {
                    if (Render* render = hovered->GetComponent<Render>())
                    {
                        render->SetMaterial(payload->path);
                    }
                }
            }
        }
    }

    Camera* camera = World::GetCamera();

    // double-click to focus on entity
    if (!is_playing && camera && ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered() && !overlay_controls.hovered && ImGui::TransformGizmo::allow_picking() && !TerrainEditor::IsSculptActive())
    {
        spartan::Selection::Pick(*camera);
        m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(spartan::Selection::GetSelectedEntity());
        if (spartan::Selection::GetSelectedEntity())
        {
            spartan::CameraController::Get(*camera).Focus(spartan::Selection::GetSelectedEntity(), spartan::Selection::GetSelectedInstance());
        }
    }
    // mouse picking (with multi-select via Ctrl handled in Pick())
    else if (!is_playing && camera && ImGui::IsMouseClicked(0) && ImGui::IsItemHovered() && !overlay_controls.hovered && ImGui::TransformGizmo::allow_picking() && !TerrainEditor::IsSculptActive())
    {
        spartan::Selection::Pick(*camera);

        // when ctrl is held, Pick() already handled multi-selection via ToggleSelection(),
        // so we only update the properties panel without overwriting the camera's selection
        if (Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Ctrl_Right))
        {
            Properties::ClearMaterialInspection();
        }
        else
        {
            m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(spartan::Selection::GetSelectedEntity());
        }
    }

    // Ctrl+D to duplicate selected entities
    if (camera && ImGui::IsWindowFocused() && Input::GetKey(KeyCode::Ctrl_Left) && Input::GetKeyDown(KeyCode::D) && !ImGuiSp::editor_shortcuts_blocked())
    {
        const std::vector<Entity*>& selected_entities = spartan::Selection::GetSelectedEntities();
        if (!selected_entities.empty())
        {
            // clone all selected entities
            std::vector<Entity*> cloned_entities;
            for (Entity* entity : selected_entities)
            {
                if (entity)
                {
                    bool selected_ancestor = false;
                    for (auto parent = entity->GetParent(); parent; parent = parent->GetParent())
                        if (std::find(selected_entities.begin(), selected_entities.end(), parent) != selected_entities.end()) { selected_ancestor = true; break; }
                    if (selected_ancestor) continue;
                    Entity* cloned = entity->Clone();
                    if (cloned)
                    {
                        cloned_entities.push_back(cloned);
                    }
                }
            }

            editor_history::Created(cloned_entities);
            // select the cloned entities instead
            if (!cloned_entities.empty())
            {
                spartan::Selection::ClearSelection();
                for (Entity* cloned : cloned_entities)
                {
                    spartan::Selection::AddToSelection(cloned);
                }
                m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(cloned_entities[0]);
            }
        }
    }

    // when no world is loaded, render the interactive welcome hub directly on the viewport canvas
    if (!camera)
    {
        draw_empty_viewport_welcome(image_rect_min, image_rect_max);
    }

    // The gizmo projects its own pivot, which can be the geometry center of a hierarchy.
    // Culling by the selected entity's origin hides visible gizmos on offset geometry.
    ImGui::TransformGizmo::tick();

    // drawn last, the tooltip would otherwise become the item the hover tests above read
    performance_overlay_controls_draw(overlay_controls);

    // check if the engine wants cursor control
    if (camera && camera->GetFlag(spartan::CameraFlags::IsControlled))
    {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    else
    {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    }
}
