/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "pch.h"
#include "TransformGizmo.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace TransformGizmo
{
    namespace
    {
        constexpr float pi         = 3.14159265358979323846f;
        constexpr float deg_to_rad = pi / 180.0f;
        constexpr float rad_to_deg = 180.0f / pi;
        constexpr float epsilon    = 1e-6f;

        struct Vec3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;

            Vec3() = default;
            Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

            Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
            Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
            Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
            Vec3 operator/(float s) const { return { x / s, y / s, z / s }; }
            Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
        };

        float dot(const Vec3& a, const Vec3& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        Vec3 cross(const Vec3& a, const Vec3& b)
        {
            return {
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            };
        }

        float length(const Vec3& v)
        {
            return sqrtf(dot(v, v));
        }

        Vec3 normalize(const Vec3& v)
        {
            const float len = length(v);
            if (len < epsilon)
            {
                return { 0.0f, 0.0f, 0.0f };
            }
            return v / len;
        }

        float clamp01(float v)
        {
            return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        }

        float snap_value(float v, float step)
        {
            if (fabsf(step) < epsilon)
            {
                return v;
            }
            return floorf(v / step + 0.5f) * step;
        }

        // row-vector multiply, float[16] matches spartan::math::Matrix column-major memory
        // layout: m00,m10,m20,m30, m01,m11,m21,m31, m02,m12,m22,m32, m03,m13,m23,m33
        void transform_point(const float* m, const Vec3& p, Vec3& out, float& w_out)
        {
            out.x = m[0] * p.x + m[1] * p.y + m[2]  * p.z + m[3];
            out.y = m[4] * p.x + m[5] * p.y + m[6]  * p.z + m[7];
            out.z = m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11];
            w_out = m[12] * p.x + m[13] * p.y + m[14] * p.z + m[15];
        }

        void transform_vector(const float* m, const Vec3& v, Vec3& out)
        {
            out.x = m[0] * v.x + m[1] * v.y + m[2]  * v.z;
            out.y = m[4] * v.x + m[5] * v.y + m[6]  * v.z;
            out.z = m[8] * v.x + m[9] * v.y + m[10] * v.z;
        }

        // model/local axes are matrix rows (matches Matrix::GetScale)
        Vec3 get_axis(const float* m, int axis)
        {
            return normalize(Vec3(m[axis], m[4 + axis], m[8 + axis]));
        }

        Vec3 get_origin(const float* m)
        {
            return { m[3], m[7], m[11] };
        }

        void set_origin(float* m, const Vec3& o)
        {
            m[3]  = o.x;
            m[7]  = o.y;
            m[11] = o.z;
        }

        float get_axis_scale(const float* m, int axis)
        {
            return length(Vec3(m[axis], m[4 + axis], m[8 + axis]));
        }

        void set_axis_scaled(float* m, int axis, const Vec3& dir, float scale)
        {
            m[axis]     = dir.x * scale;
            m[4 + axis] = dir.y * scale;
            m[8 + axis] = dir.z * scale;
        }

        // view matrix columns are camera right/up/forward
        Vec3 view_right(const float* view)
        {
            return normalize(Vec3(view[0], view[1], view[2]));
        }

        Vec3 view_up(const float* view)
        {
            return normalize(Vec3(view[4], view[5], view[6]));
        }

        Vec3 view_forward(const float* view)
        {
            return normalize(Vec3(view[8], view[9], view[10]));
        }

        Vec3 view_eye(const float* view)
        {
            const float tx = view[3];
            const float ty = view[7];
            const float tz = view[11];
            return {
                -(tx * view[0] + ty * view[4] + tz * view[8]),
                -(tx * view[1] + ty * view[5] + tz * view[9]),
                -(tx * view[2] + ty * view[6] + tz * view[10])
            };
        }

        void build_rotation_matrix(const Vec3& axis, float angle_rad, float* out)
        {
            const Vec3 n = normalize(axis);
            const float c = cosf(angle_rad);
            const float s = sinf(angle_rad);
            const float t = 1.0f - c;

            const float r00 = t * n.x * n.x + c;
            const float r01 = t * n.x * n.y - s * n.z;
            const float r02 = t * n.x * n.z + s * n.y;
            const float r10 = t * n.x * n.y + s * n.z;
            const float r11 = t * n.y * n.y + c;
            const float r12 = t * n.y * n.z - s * n.x;
            const float r20 = t * n.x * n.z - s * n.y;
            const float r21 = t * n.y * n.z + s * n.x;
            const float r22 = t * n.z * n.z + c;

            // column-major like spartan::math::Matrix
            out[0]  = r00; out[1]  = r10; out[2]  = r20; out[3]  = 0.0f;
            out[4]  = r01; out[5]  = r11; out[6]  = r21; out[7]  = 0.0f;
            out[8]  = r02; out[9]  = r12; out[10] = r22; out[11] = 0.0f;
            out[12] = 0.0f; out[13] = 0.0f; out[14] = 0.0f; out[15] = 1.0f;
        }

        void identity_matrix(float* m)
        {
            memset(m, 0, sizeof(float) * 16);
            m[0] = m[5] = m[10] = m[15] = 1.0f;
        }

        enum class MoveType
        {
            None,
            AxisX,
            AxisY,
            AxisZ,
            PlaneYZ,
            PlaneZX,
            PlaneXY,
            Screen,
            RotateX,
            RotateY,
            RotateZ,
            RotateScreen,
            ScaleX,
            ScaleY,
            ScaleZ,
            ScaleXYZ,
            Count
        };

        struct Context
        {
            float rect_x = 0.0f;
            float rect_y = 0.0f;
            float rect_w = 1.0f;
            float rect_h = 1.0f;
            bool orthographic = false;
            ImDrawList* draw_list = nullptr;

            bool using_gizmo = false;
            bool over_gizmo  = false;
            MoveType active  = MoveType::None;
            MoveType hover   = MoveType::None;

            // per handle hover fade, indexed by MoveType
            float anim[static_cast<int>(MoveType::Count)] = {};

            float matrix_start[16] = {};
            Vec3 origin_start;
            Vec3 click_world;
            Vec3 axis_local[3];
            float scale_start[3] = { 1.0f, 1.0f, 1.0f };
            float angle_start    = 0.0f;
            float drag_amount    = 0.0f;
            Vec3 rotation_u;
            Vec3 rotation_v;
            float rotation_ring_scale = 1.0f;

            Operation operation = Operation::Translate;
            Space space         = Space::World;
            DeltaInfo delta;
        };

        Context g_ctx;
        Style g_style;

        bool style_initialized = false;

        void ensure_style()
        {
            if (style_initialized)
            {
                return;
            }

            g_style.colors[static_cast<int>(Color::DirectionX)] = ImVec4(0.87f, 0.25f, 0.28f, 1.0f);
            g_style.colors[static_cast<int>(Color::DirectionY)] = ImVec4(0.42f, 0.80f, 0.24f, 1.0f);
            g_style.colors[static_cast<int>(Color::DirectionZ)] = ImVec4(0.24f, 0.52f, 0.95f, 1.0f);
            g_style.colors[static_cast<int>(Color::PlaneX)]     = ImVec4(0.87f, 0.25f, 0.28f, 1.0f);
            g_style.colors[static_cast<int>(Color::PlaneY)]     = ImVec4(0.42f, 0.80f, 0.24f, 1.0f);
            g_style.colors[static_cast<int>(Color::PlaneZ)]     = ImVec4(0.24f, 0.52f, 0.95f, 1.0f);
            g_style.colors[static_cast<int>(Color::Selection)]  = ImVec4(1.00f, 0.80f, 0.20f, 1.0f);
            g_style.colors[static_cast<int>(Color::Inactive)]   = ImVec4(0.55f, 0.55f, 0.58f, 0.55f);
            g_style.colors[static_cast<int>(Color::Text)]       = ImVec4(0.94f, 0.95f, 0.97f, 1.0f);
            style_initialized = true;
        }

        ImU32 to_u32(const ImVec4& c, float alpha_mul = 1.0f)
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha_mul));
        }

        ImVec4 lerp_color(const ImVec4& a, const ImVec4& b, float t)
        {
            return ImVec4(
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t
            );
        }

        ImVec4 base_color(MoveType type)
        {
            ensure_style();
            switch (type)
            {
            case MoveType::AxisX:
            case MoveType::RotateX:
            case MoveType::ScaleX:
                return g_style.colors[static_cast<int>(Color::DirectionX)];
            case MoveType::AxisY:
            case MoveType::RotateY:
            case MoveType::ScaleY:
                return g_style.colors[static_cast<int>(Color::DirectionY)];
            case MoveType::AxisZ:
            case MoveType::RotateZ:
            case MoveType::ScaleZ:
                return g_style.colors[static_cast<int>(Color::DirectionZ)];
            case MoveType::PlaneYZ:
                return g_style.colors[static_cast<int>(Color::PlaneX)];
            case MoveType::PlaneZX:
                return g_style.colors[static_cast<int>(Color::PlaneY)];
            case MoveType::PlaneXY:
                return g_style.colors[static_cast<int>(Color::PlaneZ)];
            default:
                return g_style.colors[static_cast<int>(Color::Text)];
            }
        }

        // 0 when idle, 1 when hovered or dragged, smoothed over time
        float hot_amount(MoveType type)
        {
            if (g_ctx.active == type)
            {
                return 1.0f;
            }
            if (g_ctx.using_gizmo)
            {
                return 0.0f;
            }
            return clamp01(g_ctx.anim[static_cast<int>(type)]);
        }

        // handles that are not being dragged fade back so the active one reads clearly
        float dim_amount(MoveType type)
        {
            if (!g_ctx.using_gizmo)
            {
                return 1.0f;
            }
            return (g_ctx.active == type) ? 1.0f : g_style.inactive_dim;
        }

        ImU32 handle_color(MoveType type, float alpha_mul = 1.0f)
        {
            ensure_style();
            const ImVec4 sel = g_style.colors[static_cast<int>(Color::Selection)];
            const ImVec4 c   = lerp_color(base_color(type), sel, hot_amount(type));
            return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, alpha_mul * dim_amount(type)));
        }

        void update_animation(MoveType highlight)
        {
            const float dt = ImGui::GetIO().DeltaTime;
            const float k  = 1.0f - expf(-g_style.hover_animation_speed * (dt > 0.0f ? dt : 0.016f));
            for (int i = 0; i < static_cast<int>(MoveType::Count); i++)
            {
                const float target = (i == static_cast<int>(highlight)) ? 1.0f : 0.0f;
                g_ctx.anim[i] += (target - g_ctx.anim[i]) * k;
            }
        }

        bool world_to_screen(const float* view, const float* proj, const Vec3& world, ImVec2& screen, float* clip_w = nullptr)
        {
            Vec3 view_pos;
            float view_w = 1.0f;
            transform_point(view, world, view_pos, view_w);

            const float clip_x = proj[0] * view_pos.x + proj[1] * view_pos.y + proj[2]  * view_pos.z + proj[3]  * view_w;
            const float clip_y = proj[4] * view_pos.x + proj[5] * view_pos.y + proj[6]  * view_pos.z + proj[7]  * view_w;
            const float clip_w_local = proj[12] * view_pos.x + proj[13] * view_pos.y + proj[14] * view_pos.z + proj[15] * view_w;

            if (clip_w)
            {
                *clip_w = clip_w_local;
            }

            if (fabsf(clip_w_local) < epsilon)
            {
                return false;
            }

            const float inv_w = 1.0f / clip_w_local;
            const float ndc_x = clip_x * inv_w;
            const float ndc_y = clip_y * inv_w;

            screen.x = g_ctx.rect_x + (ndc_x * 0.5f + 0.5f) * g_ctx.rect_w;
            screen.y = g_ctx.rect_y + (1.0f - (ndc_y * 0.5f + 0.5f)) * g_ctx.rect_h;
            return clip_w_local > 0.0f;
        }

        void screen_ray(const float* view, const float* proj, const ImVec2& mouse, Vec3& origin, Vec3& direction)
        {
            const float nx = ((mouse.x - g_ctx.rect_x) / g_ctx.rect_w) * 2.0f - 1.0f;
            const float ny = 1.0f - ((mouse.y - g_ctx.rect_y) / g_ctx.rect_h) * 2.0f;

            const Vec3 right   = view_right(view);
            const Vec3 up      = view_up(view);
            const Vec3 forward = view_forward(view);
            origin = view_eye(view);

            if (g_ctx.orthographic)
            {
                direction = forward;
                return;
            }

            // proj m11 = 1/tan(fov/2) at index 5 in column-major layout
            const float tan_half_fov = (fabsf(proj[5]) > epsilon) ? (1.0f / proj[5]) : 1.0f;
            const float aspect = (fabsf(proj[0]) > epsilon) ? (proj[5] / proj[0]) : (g_ctx.rect_w / g_ctx.rect_h);
            direction = normalize(
                right * (nx * tan_half_fov * aspect) +
                up * (ny * tan_half_fov) +
                forward
            );
        }

        bool intersect_plane(const Vec3& ray_o, const Vec3& ray_d, const Vec3& plane_o, const Vec3& plane_n, Vec3& hit)
        {
            const float denom = dot(plane_n, ray_d);
            if (fabsf(denom) < epsilon)
            {
                return false;
            }
            const float t = dot(plane_o - ray_o, plane_n) / denom;
            if (t < 0.0f)
            {
                return false;
            }
            hit = ray_o + ray_d * t;
            return true;
        }

        float distance_point_segment_sq(const ImVec2& p, const ImVec2& a, const ImVec2& b, float* t_out = nullptr)
        {
            const float abx = b.x - a.x;
            const float aby = b.y - a.y;
            const float apx = p.x - a.x;
            const float apy = p.y - a.y;
            const float ab_len_sq = abx * abx + aby * aby;
            float t = 0.0f;
            if (ab_len_sq > epsilon)
            {
                t = clamp01((apx * abx + apy * aby) / ab_len_sq);
            }
            if (t_out)
            {
                *t_out = t;
            }
            const float cx = a.x + abx * t - p.x;
            const float cy = a.y + aby * t - p.y;
            return cx * cx + cy * cy;
        }

        float compute_scale_factor(const float* view, const float* proj, const Vec3& origin)
        {
            ImVec2 s0, s1;
            if (!world_to_screen(view, proj, origin, s0))
            {
                return 1.0f;
            }

            // use camera right in world from inverse view axes
            const Vec3 cam_right = view_right(view);
            const float probe = 1.0f;
            if (!world_to_screen(view, proj, origin + cam_right * probe, s1))
            {
                return 1.0f;
            }

            const float px = sqrtf((s1.x - s0.x) * (s1.x - s0.x) + (s1.y - s0.y) * (s1.y - s0.y));
            if (px < epsilon)
            {
                return 1.0f;
            }

            const float desired_px = g_style.gizmo_size_clip_space * g_ctx.rect_h;
            return desired_px / px;
        }

        void resolve_axes(const float* matrix, Space space, Vec3 axes[3])
        {
            if (space == Space::Local)
            {
                axes[0] = get_axis(matrix, 0);
                axes[1] = get_axis(matrix, 1);
                axes[2] = get_axis(matrix, 2);
            }
            else
            {
                axes[0] = { 1.0f, 0.0f, 0.0f };
                axes[1] = { 0.0f, 1.0f, 0.0f };
                axes[2] = { 0.0f, 0.0f, 1.0f };
            }
        }

        float smoothstep(float edge0, float edge1, float x)
        {
            if (fabsf(edge1 - edge0) < epsilon)
            {
                return x < edge0 ? 0.0f : 1.0f;
            }
            const float t = clamp01((x - edge0) / (edge1 - edge0));
            return t * t * (3.0f - 2.0f * t);
        }

        bool axis_visible(const float* view, const Vec3& axis)
        {
            const Vec3 cam_forward = view_forward(view);
            // hide when nearly parallel to view
            return fabsf(dot(cam_forward, axis)) < 0.975f;
        }

        // axes fade out instead of popping when they align with the view
        float axis_fade(const float* view, const Vec3& axis)
        {
            const float d = fabsf(dot(view_forward(view), axis));
            return 1.0f - smoothstep(0.96f, 0.99f, d);
        }

        // plane handles fade out when seen edge on
        float plane_fade(const float* view, const Vec3& normal)
        {
            const float d = fabsf(dot(view_forward(view), normal));
            return smoothstep(0.10f, 0.35f, d);
        }

        bool plane_visible(const float* view, const Vec3& normal)
        {
            return plane_fade(view, normal) > 0.35f;
        }

        // point inside a screen space quad, used for plane handle picking
        bool point_in_quad(const ImVec2& p, const ImVec2 quad[4])
        {
            int sign = 0;
            for (int i = 0; i < 4; i++)
            {
                const ImVec2& a = quad[i];
                const ImVec2& b = quad[(i + 1) % 4];
                const float cross_z = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
                const int s = (cross_z > 0.0f) ? 1 : -1;
                if (sign == 0)
                {
                    sign = s;
                }
                else if (s != sign)
                {
                    return false;
                }
            }
            return true;
        }

        MoveType hit_test_translate(
            const float* view,
            const float* proj,
            const Vec3& origin,
            const Vec3 axes[3],
            float scale,
            const ImVec2& mouse
        )
        {
            ImVec2 origin_s;
            if (!world_to_screen(view, proj, origin, origin_s))
            {
                return MoveType::None;
            }

            const float hit_r = g_style.hit_radius_pixels;
            const float hit_r_sq = hit_r * hit_r;
            float best = hit_r_sq;
            MoveType best_type = MoveType::None;

            // screen handle
            {
                const float dx = mouse.x - origin_s.x;
                const float dy = mouse.y - origin_s.y;
                const float d = dx * dx + dy * dy;
                if (d < (g_style.center_circle_size + 6.0f) * (g_style.center_circle_size + 6.0f))
                {
                    return MoveType::Screen;
                }
            }

            // planes take priority, the whole quad is clickable
            const MoveType plane_types[3] = { MoveType::PlaneYZ, MoveType::PlaneZX, MoveType::PlaneXY };
            for (int i = 0; i < 3; i++)
            {
                const int a = (i + 1) % 3;
                const int b = (i + 2) % 3;
                if (!plane_visible(view, axes[i]))
                {
                    continue;
                }

                const float ps    = scale * g_style.plane_size;
                const float inset = ps * 0.18f;
                ImVec2 quad[4];
                if (!world_to_screen(view, proj, origin + (axes[a] + axes[b]) * inset, quad[0]) ||
                    !world_to_screen(view, proj, origin + axes[a] * ps + axes[b] * inset, quad[1]) ||
                    !world_to_screen(view, proj, origin + (axes[a] + axes[b]) * ps, quad[2]) ||
                    !world_to_screen(view, proj, origin + axes[b] * ps + axes[a] * inset, quad[3]))
                {
                    continue;
                }

                if (point_in_quad(mouse, quad))
                {
                    return plane_types[i];
                }
            }

            const MoveType axis_types[3] = { MoveType::AxisX, MoveType::AxisY, MoveType::AxisZ };
            for (int i = 0; i < 3; i++)
            {
                if (!axis_visible(view, axes[i]))
                {
                    continue;
                }
                ImVec2 tip;
                if (!world_to_screen(view, proj, origin + axes[i] * scale, tip))
                {
                    continue;
                }
                float t = 0.0f;
                const float d = distance_point_segment_sq(mouse, origin_s, tip, &t);
                if (t > 0.05f && d < best)
                {
                    best = d;
                    best_type = axis_types[i];
                }
            }

            return best < hit_r_sq ? best_type : MoveType::None;
        }

        MoveType hit_test_rotate(
            const float* view,
            const float* proj,
            const Vec3& origin,
            const Vec3 axes[3],
            float scale,
            const ImVec2& mouse
        )
        {
            ImVec2 origin_s;
            if (!world_to_screen(view, proj, origin, origin_s))
            {
                return MoveType::None;
            }

            const float hit_r = g_style.hit_radius_pixels;
            float best = hit_r * hit_r;
            MoveType best_type = MoveType::None;

            const int segments = 64;
            const MoveType ring_types[3] = { MoveType::RotateX, MoveType::RotateY, MoveType::RotateZ };

            for (int axis = 0; axis < 3; axis++)
            {
                if (!axis_visible(view, axes[axis]))
                {
                    continue;
                }

                const Vec3 u = axes[(axis + 1) % 3];
                const Vec3 v = axes[(axis + 2) % 3];

                for (int i = 0; i < segments; i++)
                {
                    const float a0 = (static_cast<float>(i) / segments) * 2.0f * pi;
                    const float a1 = (static_cast<float>(i + 1) / segments) * 2.0f * pi;
                    const Vec3 p0 = origin + (u * cosf(a0) + v * sinf(a0)) * scale;
                    const Vec3 p1 = origin + (u * cosf(a1) + v * sinf(a1)) * scale;
                    ImVec2 s0, s1;
                    if (!world_to_screen(view, proj, p0, s0) || !world_to_screen(view, proj, p1, s1))
                    {
                        continue;
                    }
                    const float d = distance_point_segment_sq(mouse, s0, s1);
                    if (d < best)
                    {
                        best = d;
                        best_type = ring_types[axis];
                    }
                }
            }

            // screen rotate: outer circle
            {
                const float radius_px = 0.0f;
                ImVec2 tip;
                const Vec3 cam_right = view_right(view);
                if (world_to_screen(view, proj, origin + cam_right * (scale * 1.15f), tip))
                {
                    const float dx = tip.x - origin_s.x;
                    const float dy = tip.y - origin_s.y;
                    const float r = sqrtf(dx * dx + dy * dy);
                    const float md = sqrtf((mouse.x - origin_s.x) * (mouse.x - origin_s.x) + (mouse.y - origin_s.y) * (mouse.y - origin_s.y));
                    const float dist = fabsf(md - r);
                    if (dist < hit_r && dist * dist < best)
                    {
                        best = dist * dist;
                        best_type = MoveType::RotateScreen;
                        (void)radius_px;
                    }
                }
            }

            return best < hit_r * hit_r ? best_type : MoveType::None;
        }

        MoveType hit_test_scale(
            const float* view,
            const float* proj,
            const Vec3& origin,
            const Vec3 axes[3],
            float scale,
            const ImVec2& mouse
        )
        {
            ImVec2 origin_s;
            if (!world_to_screen(view, proj, origin, origin_s))
            {
                return MoveType::None;
            }

            const float hit_r_sq = g_style.hit_radius_pixels * g_style.hit_radius_pixels;
            float best = hit_r_sq;
            MoveType best_type = MoveType::None;

            {
                const float dx = mouse.x - origin_s.x;
                const float dy = mouse.y - origin_s.y;
                if (dx * dx + dy * dy < (g_style.center_circle_size + 6.0f) * (g_style.center_circle_size + 6.0f))
                {
                    return MoveType::ScaleXYZ;
                }
            }

            const MoveType types[3] = { MoveType::ScaleX, MoveType::ScaleY, MoveType::ScaleZ };
            for (int i = 0; i < 3; i++)
            {
                if (!axis_visible(view, axes[i]))
                {
                    continue;
                }
                ImVec2 tip;
                if (!world_to_screen(view, proj, origin + axes[i] * scale, tip))
                {
                    continue;
                }
                float t = 0.0f;
                const float d = distance_point_segment_sq(mouse, origin_s, tip, &t);
                if (t > 0.05f && d < best)
                {
                    best = d;
                    best_type = types[i];
                }
            }

            return best < hit_r_sq ? best_type : MoveType::None;
        }

        constexpr int max_polyline_points = 200;

        ImU32 with_alpha(ImU32 col, int alpha)
        {
            return (col & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24);
        }

        float alpha_of(ImU32 col)
        {
            return static_cast<float>((col >> 24) & 0xFF) / 255.0f;
        }

        ImU32 mul_alpha(ImU32 col, float m)
        {
            int a = static_cast<int>(static_cast<float>((col >> 24) & 0xFF) * m + 0.5f);
            a = a < 0 ? 0 : (a > 255 ? 255 : a);
            return with_alpha(col, a);
        }

        ImU32 mul_rgb(ImU32 col, float m)
        {
            const int a = static_cast<int>((col >> 24) & 0xFF);
            int r = static_cast<int>(static_cast<float>(col & 0xFF) * m + 0.5f);
            int g = static_cast<int>(static_cast<float>((col >> 8) & 0xFF) * m + 0.5f);
            int b = static_cast<int>(static_cast<float>((col >> 16) & 0xFF) * m + 0.5f);
            r = r > 255 ? 255 : r;
            g = g > 255 ? 255 : g;
            b = b > 255 ? 255 : b;
            return IM_COL32(r, g, b, a);
        }

        // soft falloff around a handle, three layers from wide and faint to narrow and stronger
        constexpr int halo_layers = 3;
        constexpr float halo_width[halo_layers]    = { 1.0f, 0.62f, 0.30f };
        constexpr float halo_strength[halo_layers] = { 0.30f, 0.50f, 0.85f };

        // very dark version of the handle color, keeps a hint of hue so it does not read as ink
        ImU32 halo_of(ImU32 col, float strength)
        {
            const int a = static_cast<int>(clamp01(alpha_of(col) * g_style.halo_alpha * strength) * 255.0f);
            return with_alpha(mul_rgb(col, 0.10f), a);
        }

        // pushes a point away from a center by a pixel amount, builds seamless inflated outlines
        ImVec2 grow_point(const ImVec2& p, const ImVec2& center, float pixels)
        {
            const float dx  = p.x - center.x;
            const float dy  = p.y - center.y;
            const float len = sqrtf(dx * dx + dy * dy);
            if (len < epsilon)
            {
                return p;
            }
            return ImVec2(p.x + dx / len * pixels, p.y + dy / len * pixels);
        }

        // soft key light from the upper left of the camera, used for fake shading
        Vec3 light_direction(const float* view)
        {
            return normalize(view_right(view) * -0.45f + view_up(view) * 0.8f + view_forward(view) * -0.4f);
        }

        // converts a pixel size into world units at the gizmo distance
        float world_from_pixels(float gizmo_scale, float pixels)
        {
            const float desired_px = g_style.gizmo_size_clip_space * g_ctx.rect_h;
            if (desired_px < epsilon)
            {
                return 0.0f;
            }
            return gizmo_scale * (pixels / desired_px);
        }

        void draw_line_soft(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float thickness)
        {
            if (g_style.show_halo)
            {
                if (g_style.ambient_glow > 0.0f)
                {
                    dl->AddLine(a, b, mul_alpha(col, g_style.ambient_glow), thickness + g_style.halo_thickness * 1.7f);
                }

                for (int i = 0; i < halo_layers; i++)
                {
                    dl->AddLine(a, b, halo_of(col, halo_strength[i]), thickness + g_style.halo_thickness * halo_width[i]);
                }
            }

            dl->AddLine(a, b, col, thickness);

            // fake round caps so shafts do not look chopped
            dl->AddCircleFilled(a, thickness * 0.5f, col, 12);
            dl->AddCircleFilled(b, thickness * 0.5f, col, 12);
        }

        // polyline with a per point alpha ramp, drawn in short chunks so the gradient stays smooth
        void draw_polyline_faded(ImDrawList* dl, const ImVec2* pts, const float* fade, int count, ImU32 col, float thickness)
        {
            if (count < 2)
            {
                return;
            }

            const int chunk  = 6;
            const int passes = g_style.show_halo ? halo_layers + 2 : 1;

            for (int pass = 0; pass < passes; pass++)
            {
                // pass 0 is a faint colored bloom, then the dark halo layers, then the line itself
                const bool is_glow = g_style.show_halo && pass == 0;
                const bool is_halo = !is_glow && pass < passes - 1;
                float width = thickness;
                if (is_glow)
                {
                    width = thickness + g_style.halo_thickness * 1.7f;
                }
                else if (is_halo)
                {
                    width = thickness + g_style.halo_thickness * halo_width[pass - 1];
                }

                for (int i = 0; i + 1 < count; i += chunk)
                {
                    int end = i + chunk;
                    if (end > count - 1)
                    {
                        end = count - 1;
                    }

                    float f = 0.0f;
                    for (int j = i; j <= end; j++)
                    {
                        f += fade[j];
                    }
                    f /= static_cast<float>(end - i + 1);

                    if (f <= 0.02f)
                    {
                        continue;
                    }

                    ImU32 c = mul_alpha(col, f);
                    if (is_glow)
                    {
                        c = mul_alpha(col, g_style.ambient_glow * f);
                    }
                    else if (is_halo)
                    {
                        c = halo_of(col, halo_strength[pass - 1] * f);
                    }

                    dl->AddPolyline(&pts[i], end - i + 1, c, width, 0);
                }
            }
        }

        // shaded cone used for translation arrow heads
        void draw_cone(
            ImDrawList* dl,
            const float* view,
            const float* proj,
            const Vec3& tip,
            const Vec3& axis,
            float cone_length,
            float cone_radius,
            ImU32 col
        )
        {
            constexpr int segments = 20;

            const Vec3 base_center = tip - axis * cone_length;
            ImVec2 tip_s;
            if (!world_to_screen(view, proj, tip, tip_s))
            {
                return;
            }

            Vec3 u = cross(axis, Vec3(0.0f, 1.0f, 0.0f));
            if (length(u) < 0.05f)
            {
                u = cross(axis, Vec3(1.0f, 0.0f, 0.0f));
            }
            u = normalize(u);
            const Vec3 v = normalize(cross(axis, u));

            Vec3 rim_dir[segments];
            ImVec2 rim[segments];
            for (int i = 0; i < segments; i++)
            {
                const float a = (static_cast<float>(i) / segments) * 2.0f * pi;
                rim_dir[i] = u * cosf(a) + v * sinf(a);
                if (!world_to_screen(view, proj, base_center + rim_dir[i] * cone_radius, rim[i]))
                {
                    return;
                }
            }

            // soft halo hugging the silhouette, no hard outline
            ImVec2 base_s;
            if (g_style.show_halo && world_to_screen(view, proj, base_center, base_s))
            {
                const float dx  = tip_s.x - base_s.x;
                const float dy  = tip_s.y - base_s.y;
                const float len = sqrtf(dx * dx + dy * dy);
                if (len > epsilon)
                {
                    // widest rim points across the axis define the cone silhouette
                    const ImVec2 perp = ImVec2(-dy / len, dx / len);
                    int i_min = 0;
                    int i_max = 0;
                    float v_min = 1e30f;
                    float v_max = -1e30f;
                    for (int i = 0; i < segments; i++)
                    {
                        const float pv = (rim[i].x - base_s.x) * perp.x + (rim[i].y - base_s.y) * perp.y;
                        if (pv < v_min)
                        {
                            v_min = pv;
                            i_min = i;
                        }
                        if (pv > v_max)
                        {
                            v_max = pv;
                            i_max = i;
                        }
                    }

                    // layer -1 is the faint colored bloom
                    for (int layer = -1; layer < halo_layers; layer++)
                    {
                        const bool glow  = layer < 0;
                        const float grow = glow
                            ? g_style.halo_thickness * 0.85f
                            : g_style.halo_thickness * halo_width[layer] * 0.5f;
                        const ImU32 c = glow
                            ? mul_alpha(col, g_style.ambient_glow)
                            : halo_of(col, halo_strength[layer]);

                        if (glow && g_style.ambient_glow <= 0.0f)
                        {
                            continue;
                        }

                        ImVec2 ring[segments];
                        for (int i = 0; i < segments; i++)
                        {
                            ring[i] = grow_point(rim[i], base_s, grow);
                        }

                        const ImVec2 tip_g = ImVec2(tip_s.x + dx / len * grow, tip_s.y + dy / len * grow);
                        dl->AddConvexPolyFilled(ring, segments, c);
                        dl->AddTriangleFilled(tip_g, ring[i_min], ring[i_max], c);
                    }
                }
            }

            const Vec3 light = light_direction(view);

            // base cap first, it also hides seams between the fan triangles
            dl->AddConvexPolyFilled(rim, segments, mul_rgb(col, 0.62f));

            for (int i = 0; i < segments; i++)
            {
                const int next    = (i + 1) % segments;
                const Vec3 n      = normalize(rim_dir[i] + rim_dir[next]);
                const float shade = 0.70f + 0.45f * clamp01(dot(n, light));
                dl->AddTriangleFilled(tip_s, rim[i], rim[next], mul_rgb(col, shade));
            }
        }

        // shaded box used for scale handles
        void draw_cube(
            ImDrawList* dl,
            const float* view,
            const float* proj,
            const Vec3& center,
            const Vec3 axes[3],
            float half_size,
            ImU32 col
        )
        {
            ImVec2 corner[8];
            for (int i = 0; i < 8; i++)
            {
                const Vec3 p = center +
                    axes[0] * ((i & 1) ? half_size : -half_size) +
                    axes[1] * ((i & 2) ? half_size : -half_size) +
                    axes[2] * ((i & 4) ? half_size : -half_size);
                if (!world_to_screen(view, proj, p, corner[i]))
                {
                    return;
                }
            }

            static const int faces[6][4] =
            {
                { 1, 3, 7, 5 },
                { 0, 4, 6, 2 },
                { 2, 6, 7, 3 },
                { 0, 1, 5, 4 },
                { 4, 5, 7, 6 },
                { 0, 2, 3, 1 }
            };
            static const int face_axis[6]    = { 0, 0, 1, 1, 2, 2 };
            static const float face_sign[6]  = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f };

            const Vec3 cam_forward = view_forward(view);
            const Vec3 light       = light_direction(view);

            // halo grows every corner away from the projected center, so shared edges stay seamless
            ImVec2 center_s;
            if (g_style.show_halo && world_to_screen(view, proj, center, center_s))
            {
                for (int layer = -1; layer < halo_layers; layer++)
                {
                    const bool glow = layer < 0;
                    if (glow && g_style.ambient_glow <= 0.0f)
                    {
                        continue;
                    }

                    const float grow = glow
                        ? g_style.halo_thickness * 0.85f
                        : g_style.halo_thickness * halo_width[layer] * 0.5f;
                    const ImU32 c = glow
                        ? mul_alpha(col, g_style.ambient_glow)
                        : halo_of(col, halo_strength[layer]);

                    for (int f = 0; f < 6; f++)
                    {
                        if (dot(axes[face_axis[f]] * face_sign[f], cam_forward) >= 0.0f)
                        {
                            continue;
                        }

                        ImVec2 quad[4];
                        for (int i = 0; i < 4; i++)
                        {
                            quad[i] = grow_point(corner[faces[f][i]], center_s, grow);
                        }
                        dl->AddConvexPolyFilled(quad, 4, c);
                    }
                }
            }

            // face shading alone separates the sides, no edge lines needed
            for (int f = 0; f < 6; f++)
            {
                const Vec3 n = axes[face_axis[f]] * face_sign[f];
                if (dot(n, cam_forward) >= 0.0f)
                {
                    continue;
                }

                ImVec2 quad[4];
                for (int i = 0; i < 4; i++)
                {
                    quad[i] = corner[faces[f][i]];
                }

                const float shade = 0.62f + 0.52f * clamp01(dot(n, light));
                dl->AddConvexPolyFilled(quad, 4, mul_rgb(col, shade));
            }
        }

        void draw_center_handle(ImDrawList* dl, const ImVec2& center, ImU32 col, float radius, float hot)
        {
            if (hot > 0.01f)
            {
                dl->AddCircleFilled(center, radius + g_style.glow_thickness * hot, mul_alpha(col, 0.20f * hot), 32);
            }

            if (g_style.show_halo)
            {
                // halo the rim only, the interior keeps its translucent fill
                for (int i = 0; i < halo_layers; i++)
                {
                    dl->AddCircle(
                        center,
                        radius,
                        halo_of(col, halo_strength[i]),
                        32,
                        2.0f + g_style.halo_thickness * halo_width[i]
                    );
                }
            }

            dl->AddCircleFilled(center, radius, mul_alpha(col, 0.22f), 24);
            dl->AddCircle(center, radius, col, 24, 2.0f + hot);
            dl->AddCircleFilled(center, radius * 0.34f, col, 12);
        }

        void draw_axis_label(ImDrawList* dl, const ImVec2& anchor, const ImVec2& dir, const char* text, ImU32 col)
        {
            const ImVec2 size = ImGui::CalcTextSize(text);
            const ImVec2 pos  = ImVec2(
                anchor.x + dir.x * 15.0f - size.x * 0.5f,
                anchor.y + dir.y * 15.0f - size.y * 0.5f
            );

            // soft backing plate, two rounded layers instead of a hard chip
            for (int i = 0; i < 2; i++)
            {
                const float pad = 4.0f - static_cast<float>(i) * 1.5f;
                dl->AddRectFilled(
                    ImVec2(pos.x - pad, pos.y - pad * 0.5f),
                    ImVec2(pos.x + size.x + pad, pos.y + size.y + pad * 0.5f),
                    IM_COL32(0, 0, 0, static_cast<int>(alpha_of(col) * (i == 0 ? 45.0f : 70.0f))),
                    4.0f
                );
            }
            dl->AddText(pos, col, text);
        }

        // circle in the u,v plane, optionally hiding the half that faces away from the camera
        void draw_ring_faded(
            ImDrawList* dl,
            const float* view,
            const float* proj,
            const Vec3& origin,
            const Vec3& u,
            const Vec3& v,
            float radius,
            ImU32 col,
            float thickness,
            bool camera_facing
        )
        {
            constexpr int segments = 96;
            static ImVec2 pts[segments + 1];
            static float fade[segments + 1];

            const Vec3 cam_forward = view_forward(view);
            int count = 0;

            for (int i = 0; i <= segments; i++)
            {
                const float a  = (static_cast<float>(i) / segments) * 2.0f * pi;
                const Vec3 dir = u * cosf(a) + v * sinf(a);

                ImVec2 s;
                float f = 1.0f;
                bool keep = world_to_screen(view, proj, origin + dir * radius, s);
                if (keep && camera_facing)
                {
                    f = 1.0f - smoothstep(-0.20f, 0.12f, dot(dir, cam_forward));
                    keep = f > 0.02f;
                }

                if (!keep)
                {
                    draw_polyline_faded(dl, pts, fade, count, col, thickness);
                    count = 0;
                    continue;
                }

                pts[count]  = s;
                fade[count] = f;
                count++;
            }

            draw_polyline_faded(dl, pts, fade, count, col, thickness);
        }

        void draw_rotation_delta_wedge(
            ImDrawList* dl,
            const float* view,
            const float* proj,
            const Vec3& origin,
            const Vec3& u,
            const Vec3& v,
            float radius,
            const Vec3& start_dir,
            float angle_rad,
            ImU32 col
        )
        {
            if (fabsf(angle_rad) < 0.001f)
            {
                return;
            }

            const float start  = atan2f(dot(start_dir, v), dot(start_dir, u));
            const int segments = std::max(8, std::min(128, static_cast<int>(fabsf(angle_rad) / (pi / 48.0f))));

            ImVec2 origin_s;
            if (!world_to_screen(view, proj, origin, origin_s))
            {
                return;
            }

            // filled pie as a fan so angles beyond 180 degrees stay correct, two layers for depth
            for (int layer = 0; layer < 2; layer++)
            {
                const float r     = radius * (layer == 0 ? 1.0f : 0.55f);
                const ImU32 fill  = with_alpha(col, layer == 0 ? 50 : 45);
                ImVec2 prev;
                bool has_prev = false;
                for (int i = 0; i <= segments; i++)
                {
                    const float t = static_cast<float>(i) / segments;
                    const float a = start + angle_rad * t;
                    ImVec2 s;
                    if (!world_to_screen(view, proj, origin + (u * cosf(a) + v * sinf(a)) * r, s))
                    {
                        has_prev = false;
                        continue;
                    }
                    if (has_prev)
                    {
                        dl->AddTriangleFilled(origin_s, prev, s, fill);
                    }
                    prev     = s;
                    has_prev = true;
                }
            }

            // bold arc on the rim
            {
                static ImVec2 pts[max_polyline_points];
                static float fade[max_polyline_points];
                int count = 0;
                for (int i = 0; i <= segments && count < max_polyline_points; i++)
                {
                    const float t = static_cast<float>(i) / segments;
                    const float a = start + angle_rad * t;
                    ImVec2 s;
                    if (world_to_screen(view, proj, origin + (u * cosf(a) + v * sinf(a)) * radius, s))
                    {
                        pts[count]  = s;
                        fade[count] = 1.0f;
                        count++;
                    }
                }
                draw_polyline_faded(dl, pts, fade, count, col, g_style.rotation_line_thickness + 2.0f);
            }

            // start and end spokes
            ImVec2 start_s;
            ImVec2 end_s;
            const Vec3 start_p = origin + (u * cosf(start) + v * sinf(start)) * radius;
            const Vec3 end_p   = origin + (u * cosf(start + angle_rad) + v * sinf(start + angle_rad)) * radius;
            if (world_to_screen(view, proj, start_p, start_s))
            {
                dl->AddLine(origin_s, start_s, with_alpha(col, 150), 1.6f);
            }
            if (world_to_screen(view, proj, end_p, end_s))
            {
                dl->AddLine(origin_s, end_s, with_alpha(col, 220), 2.0f);
                dl->AddCircleFilled(end_s, 7.0f, halo_of(col, 0.9f), 16);
                dl->AddCircleFilled(end_s, 4.5f, col, 16);
            }
        }

        void draw_translate(
            const float* view,
            const float* proj,
            const Vec3& origin,
            const Vec3 axes[3],
            float scale
        )
        {
            ImDrawList* dl = g_ctx.draw_list;
            ImVec2 origin_s;
            if (!world_to_screen(view, proj, origin, origin_s))
            {
                return;
            }

            const MoveType axis_types[3]  = { MoveType::AxisX, MoveType::AxisY, MoveType::AxisZ };
            const MoveType plane_types[3] = { MoveType::PlaneYZ, MoveType::PlaneZX, MoveType::PlaneXY };
            const char* axis_names[3]     = { "X", "Y", "Z" };

            // planes first so axes draw on top
            for (int i = 0; i < 3; i++)
            {
                const int a = (i + 1) % 3;
                const int b = (i + 2) % 3;
                const float fade = plane_fade(view, axes[i]);
                if (fade <= 0.02f)
                {
                    continue;
                }

                const float ps    = scale * g_style.plane_size;
                const float inset = ps * 0.18f;
                const Vec3 p0 = origin + (axes[a] + axes[b]) * inset;
                const Vec3 p1 = origin + axes[a] * ps + axes[b] * inset;
                const Vec3 p2 = origin + (axes[a] + axes[b]) * ps;
                const Vec3 p3 = origin + axes[b] * ps + axes[a] * inset;
                ImVec2 quad[4];
                if (!world_to_screen(view, proj, p0, quad[0]) ||
                    !world_to_screen(view, proj, p1, quad[1]) ||
                    !world_to_screen(view, proj, p2, quad[2]) ||
                    !world_to_screen(view, proj, p3, quad[3]))
                {
                    continue;
                }

                const float hot = hot_amount(plane_types[i]);
                const ImU32 col = handle_color(plane_types[i], fade);
                const ImU32 fill = mul_alpha(col, 0.22f + 0.40f * hot);
                const ImU32 line = mul_alpha(col, 0.75f + 0.25f * hot);

                if (g_style.show_halo)
                {
                    const ImVec2 quad_center = ImVec2(
                        (quad[0].x + quad[1].x + quad[2].x + quad[3].x) * 0.25f,
                        (quad[0].y + quad[1].y + quad[2].y + quad[3].y) * 0.25f
                    );

                    for (int layer = 0; layer < halo_layers; layer++)
                    {
                        const float grow = g_style.halo_thickness * halo_width[layer] * 0.4f;
                        ImVec2 grown[4];
                        for (int c = 0; c < 4; c++)
                        {
                            grown[c] = grow_point(quad[c], quad_center, grow);
                        }
                        dl->AddConvexPolyFilled(grown, 4, halo_of(col, halo_strength[layer] * 0.55f));
                    }
                }

                dl->AddConvexPolyFilled(quad, 4, fill);
                dl->AddPolyline(quad, 4, mul_alpha(col, 0.35f), 1.2f, ImDrawFlags_Closed);

                // outer edges bolder, reads like a corner bracket
                const float edge_thickness = 2.2f + hot * 1.6f;
                dl->AddLine(quad[1], quad[2], line, edge_thickness);
                dl->AddLine(quad[2], quad[3], line, edge_thickness);
            }

            // far axes first so the near ones overlap them
            const Vec3 eye         = view_eye(view);
            const Vec3 cam_forward = view_forward(view);
            int order[3]           = { 0, 1, 2 };
            float depth[3];
            for (int i = 0; i < 3; i++)
            {
                depth[i] = dot(origin + axes[i] * scale - eye, cam_forward);
            }
            for (int i = 1; i < 3; i++)
            {
                for (int j = i; j > 0 && depth[order[j]] > depth[order[j - 1]]; j--)
                {
                    const int tmp = order[j];
                    order[j]      = order[j - 1];
                    order[j - 1]  = tmp;
                }
            }

            float cone_length = world_from_pixels(scale, g_style.translation_line_arrow_size);
            cone_length = std::min(cone_length, scale * 0.45f);

            for (int k = 0; k < 3; k++)
            {
                const int i      = order[k];
                const float fade = axis_fade(view, axes[i]);
                if (fade <= 0.02f)
                {
                    continue;
                }

                const float hot = hot_amount(axis_types[i]);
                const ImU32 col = handle_color(axis_types[i], fade);

                // leave a gap at the center so the arrows do not collide
                const Vec3 base      = origin + axes[i] * (scale * 0.14f);
                const Vec3 tip       = origin + axes[i] * scale;
                const Vec3 shaft_end = tip - axes[i] * (cone_length * 0.9f);
                ImVec2 base_s;
                ImVec2 shaft_s;
                ImVec2 tip_s;
                if (!world_to_screen(view, proj, base, base_s) ||
                    !world_to_screen(view, proj, shaft_end, shaft_s) ||
                    !world_to_screen(view, proj, tip, tip_s))
                {
                    continue;
                }

                const float thickness = g_style.translation_line_thickness + hot * 2.0f;
                if (hot > 0.01f)
                {
                    dl->AddLine(base_s, shaft_s, mul_alpha(col, 0.22f * hot), thickness + g_style.glow_thickness);
                }

                draw_line_soft(dl, base_s, shaft_s, col, thickness);
                draw_cone(dl, view, proj, tip, axes[i], cone_length, cone_length * (0.34f + hot * 0.05f), col);

                if (g_style.show_axis_labels)
                {
                    const float dx  = tip_s.x - origin_s.x;
                    const float dy  = tip_s.y - origin_s.y;
                    const float len = sqrtf(dx * dx + dy * dy);
                    if (len > 1.0f)
                    {
                        const ImVec2 dir = ImVec2(dx / len, dy / len);
                        draw_axis_label(dl, tip_s, dir, axis_names[i], mul_alpha(col, 0.55f + 0.45f * hot));
                    }
                }
            }

            const float screen_hot = hot_amount(MoveType::Screen);
            draw_center_handle(
                dl,
                origin_s,
                handle_color(MoveType::Screen),
                g_style.center_circle_size + screen_hot * 1.5f,
                screen_hot
            );
        }

        void draw_rotate(
            const float* view,
            const float* proj,
            const Vec3& origin,
            const Vec3 axes[3],
            float scale
        )
        {
            ImDrawList* dl = g_ctx.draw_list;
            ImVec2 origin_s;
            if (!world_to_screen(view, proj, origin, origin_s))
            {
                return;
            }

            const MoveType ring_types[3] = { MoveType::RotateX, MoveType::RotateY, MoveType::RotateZ };

            // screen space ring first, it sits behind the axis rings
            {
                const float hot = hot_amount(MoveType::RotateScreen);
                const ImU32 col = handle_color(MoveType::RotateScreen, 0.55f + 0.45f * hot);
                draw_ring_faded(
                    dl, view, proj, origin,
                    view_right(view), view_up(view),
                    scale * 1.15f,
                    col,
                    2.0f + hot * 1.5f,
                    false
                );
            }

            // idle rings first, the hot one last so it is never overdrawn
            for (int pass = 0; pass < 2; pass++)
            {
                for (int axis = 0; axis < 3; axis++)
                {
                    const float fade = axis_fade(view, axes[axis]);
                    if (fade <= 0.02f)
                    {
                        continue;
                    }

                    const float hot = hot_amount(ring_types[axis]);
                    if ((pass == 0) == (hot > 0.01f))
                    {
                        continue;
                    }

                    const Vec3 u          = axes[(axis + 1) % 3];
                    const Vec3 v          = axes[(axis + 2) % 3];
                    const ImU32 col       = handle_color(ring_types[axis], fade);
                    const float thickness = g_style.rotation_line_thickness + hot * 2.0f;

                    if (hot > 0.01f)
                    {
                        // faint full circle so the rotation plane is readable while dragging
                        draw_ring_faded(dl, view, proj, origin, u, v, scale, mul_alpha(col, 0.20f * hot), 1.5f, false);
                        draw_ring_faded(dl, view, proj, origin, u, v, scale, mul_alpha(col, 0.18f * hot), thickness + g_style.glow_thickness, true);
                    }

                    draw_ring_faded(dl, view, proj, origin, u, v, scale, col, thickness, true);
                }
            }

            // filled delta sector for whatever ring is being dragged
            if (g_ctx.using_gizmo)
            {
                const bool is_ring =
                    g_ctx.active == MoveType::RotateX ||
                    g_ctx.active == MoveType::RotateY ||
                    g_ctx.active == MoveType::RotateZ ||
                    g_ctx.active == MoveType::RotateScreen;

                if (is_ring)
                {
                    draw_rotation_delta_wedge(
                        dl, view, proj, origin,
                        g_ctx.rotation_u, g_ctx.rotation_v,
                        g_ctx.rotation_ring_scale,
                        g_ctx.click_world,
                        g_ctx.drag_amount,
                        handle_color(g_ctx.active)
                    );
                }
            }

            if (g_ctx.operation != Operation::Universal)
            {
                draw_center_handle(
                    dl,
                    origin_s,
                    handle_color(MoveType::RotateScreen, 0.7f),
                    g_style.center_circle_size * 0.55f,
                    0.0f
                );
            }
        }

        void draw_scale(
            const float* view,
            const float* proj,
            const Vec3& origin,
            const Vec3 axes[3],
            float scale
        )
        {
            ImDrawList* dl = g_ctx.draw_list;
            ImVec2 origin_s;
            if (!world_to_screen(view, proj, origin, origin_s))
            {
                return;
            }

            const MoveType types[3]   = { MoveType::ScaleX, MoveType::ScaleY, MoveType::ScaleZ };
            const char* axis_names[3] = { "X", "Y", "Z" };
            const float uniform_hot   = hot_amount(MoveType::ScaleXYZ);
            const float cube_half     = world_from_pixels(scale, g_style.scale_line_circle_size);

            // translucent triangle between the tips hints at uniform scaling
            if (uniform_hot > 0.01f)
            {
                ImVec2 tri[3];
                bool ok = true;
                for (int i = 0; i < 3; i++)
                {
                    ok = ok && world_to_screen(view, proj, origin + axes[i] * scale, tri[i]);
                }

                if (ok)
                {
                    const ImU32 col = handle_color(MoveType::ScaleXYZ);
                    dl->AddConvexPolyFilled(tri, 3, mul_alpha(col, 0.14f * uniform_hot));
                    dl->AddPolyline(tri, 3, mul_alpha(col, 0.45f * uniform_hot), 1.5f, ImDrawFlags_Closed);
                }
            }

            // far axes first so the near ones overlap them
            const Vec3 eye         = view_eye(view);
            const Vec3 cam_forward = view_forward(view);
            int order[3]           = { 0, 1, 2 };
            float depth[3];
            for (int i = 0; i < 3; i++)
            {
                depth[i] = dot(origin + axes[i] * scale - eye, cam_forward);
            }
            for (int i = 1; i < 3; i++)
            {
                for (int j = i; j > 0 && depth[order[j]] > depth[order[j - 1]]; j--)
                {
                    const int tmp = order[j];
                    order[j]      = order[j - 1];
                    order[j - 1]  = tmp;
                }
            }

            for (int k = 0; k < 3; k++)
            {
                const int i      = order[k];
                const float fade = axis_fade(view, axes[i]);
                if (fade <= 0.02f)
                {
                    continue;
                }

                const float hot = hot_amount(types[i]);
                const ImU32 col = handle_color(types[i], fade);

                const Vec3 base     = origin + axes[i] * (scale * 0.14f);
                const Vec3 cube_pos = origin + axes[i] * scale;
                const Vec3 tip      = cube_pos - axes[i] * (cube_half * 0.9f);
                ImVec2 base_s;
                ImVec2 tip_s;
                ImVec2 cube_s;
                if (!world_to_screen(view, proj, base, base_s) ||
                    !world_to_screen(view, proj, tip, tip_s) ||
                    !world_to_screen(view, proj, cube_pos, cube_s))
                {
                    continue;
                }

                const float thickness = g_style.scale_line_thickness + hot * 1.5f;
                if (hot > 0.01f)
                {
                    dl->AddLine(base_s, tip_s, mul_alpha(col, 0.22f * hot), thickness + g_style.glow_thickness);
                }

                draw_line_soft(dl, base_s, tip_s, col, thickness);
                draw_cube(dl, view, proj, cube_pos, axes, cube_half * (1.0f + hot * 0.2f), col);

                // in universal mode the translate arrows already carry the labels
                if (g_style.show_axis_labels && g_ctx.operation != Operation::Universal)
                {
                    const float dx  = cube_s.x - origin_s.x;
                    const float dy  = cube_s.y - origin_s.y;
                    const float len = sqrtf(dx * dx + dy * dy);
                    if (len > 1.0f)
                    {
                        const ImVec2 dir = ImVec2(dx / len, dy / len);
                        draw_axis_label(dl, cube_s, dir, axis_names[i], mul_alpha(col, 0.55f + 0.45f * hot));
                    }
                }
            }

            // universal mode uses the translate circle as the center handle
            if (g_ctx.operation != Operation::Universal)
            {
                draw_cube(
                    dl, view, proj, origin, axes,
                    cube_half * (0.85f + uniform_hot * 0.25f),
                    handle_color(MoveType::ScaleXYZ)
                );
            }
        }

        void draw_delta_hud(const float* view, const float* proj, const Vec3& origin)
        {
            if (!g_style.show_delta_hud || !g_ctx.delta.active)
            {
                return;
            }

            ImVec2 origin_s;
            if (!world_to_screen(view, proj, origin, origin_s))
            {
                return;
            }

            ImDrawList* dl = g_ctx.draw_list;

            const char* title = "move";
            char labels[3][8]  = {};
            char values[3][32] = {};
            int row_count = 0;

            if (g_ctx.delta.operation == Operation::Rotate)
            {
                title = "rotate";
                const char* ring = "v";
                if (g_ctx.active == MoveType::RotateX)
                {
                    ring = "x";
                }
                else if (g_ctx.active == MoveType::RotateY)
                {
                    ring = "y";
                }
                else if (g_ctx.active == MoveType::RotateZ)
                {
                    ring = "z";
                }
                snprintf(labels[0], sizeof(labels[0]), "%s", ring);
                snprintf(values[0], sizeof(values[0]), "%.2f deg", g_ctx.delta.values[0]);
                row_count = 1;
            }
            else
            {
                const bool is_scale = g_ctx.delta.operation == Operation::Scale;
                title = is_scale ? "scale" : "move";
                const char* axis_names[3] = { "x", "y", "z" };
                for (int i = 0; i < 3; i++)
                {
                    snprintf(labels[i], sizeof(labels[i]), "%s", axis_names[i]);
                    snprintf(values[i], sizeof(values[i]), is_scale ? "%.3f" : "%.3f m", g_ctx.delta.values[i]);
                }
                row_count = 3;
            }

            const float pad     = 7.0f;
            const float line_h  = ImGui::GetTextLineHeight();
            const float label_w = ImGui::CalcTextSize("x").x + 8.0f;
            float value_w = 0.0f;
            for (int i = 0; i < row_count; i++)
            {
                value_w = std::max(value_w, ImGui::CalcTextSize(values[i]).x);
            }

            const ImVec2 title_size = ImGui::CalcTextSize(title);
            const float panel_w = std::max(title_size.x, label_w + value_w) + pad * 2.0f;
            const float panel_h = line_h * static_cast<float>(row_count + 1) + pad * 2.0f + 3.0f;

            ImVec2 p = ImVec2(origin_s.x + 26.0f, origin_s.y - panel_h - 16.0f);
            p.x = std::min(p.x, g_ctx.rect_x + g_ctx.rect_w - panel_w - 6.0f);
            p.x = std::max(p.x, g_ctx.rect_x + 6.0f);
            p.y = std::min(p.y, g_ctx.rect_y + g_ctx.rect_h - panel_h - 6.0f);
            p.y = std::max(p.y, g_ctx.rect_y + 6.0f);

            const ImVec2 pmax  = ImVec2(p.x + panel_w, p.y + panel_h);
            const ImU32 accent = to_u32(g_style.colors[static_cast<int>(Color::Selection)]);

            // leader line so the readout stays tied to the gizmo
            dl->AddLine(origin_s, ImVec2(p.x + 2.0f, pmax.y - 2.0f), IM_COL32(255, 255, 255, 45), 1.0f);

            dl->AddRectFilled(ImVec2(p.x + 3.0f, p.y + 4.0f), ImVec2(pmax.x + 3.0f, pmax.y + 4.0f), IM_COL32(0, 0, 0, 70), 6.0f);
            dl->AddRectFilled(p, pmax, IM_COL32(18, 19, 22, 232), 6.0f);
            dl->AddRect(p, pmax, mul_alpha(accent, 0.45f), 6.0f, 1.2f);

            dl->AddText(ImVec2(p.x + pad, p.y + pad), IM_COL32(170, 172, 180, 220), title);

            const ImU32 label_colors[3] =
            {
                to_u32(base_color(MoveType::AxisX)),
                to_u32(base_color(MoveType::AxisY)),
                to_u32(base_color(MoveType::AxisZ))
            };

            for (int i = 0; i < row_count; i++)
            {
                const float y = p.y + pad + line_h * static_cast<float>(i + 1) + 3.0f;
                const ImU32 label_col = (row_count == 1) ? accent : label_colors[i];
                dl->AddText(ImVec2(p.x + pad, y), label_col, labels[i]);
                dl->AddText(ImVec2(p.x + pad + label_w, y), to_u32(g_style.colors[static_cast<int>(Color::Text)]), values[i]);
            }
        }

        bool begin_drag(MoveType type, const float* view, const float* proj, const float* matrix, const Vec3 axes[3], float gizmo_scale, const ImVec2& mouse)
        {
            Vec3 ray_o, ray_d;
            screen_ray(view, proj, mouse, ray_o, ray_d);

            const Vec3 origin = get_origin(matrix);
            Vec3 plane_n;

            switch (type)
            {
            case MoveType::AxisX:
            case MoveType::ScaleX:
                plane_n = normalize(cross(axes[0], cross(axes[0], ray_d)));
                break;
            case MoveType::AxisY:
            case MoveType::ScaleY:
                plane_n = normalize(cross(axes[1], cross(axes[1], ray_d)));
                break;
            case MoveType::AxisZ:
            case MoveType::ScaleZ:
                plane_n = normalize(cross(axes[2], cross(axes[2], ray_d)));
                break;
            case MoveType::PlaneYZ:
                plane_n = axes[0];
                break;
            case MoveType::PlaneZX:
                plane_n = axes[1];
                break;
            case MoveType::PlaneXY:
                plane_n = axes[2];
                break;
            case MoveType::Screen:
            case MoveType::ScaleXYZ:
            case MoveType::RotateScreen:
                plane_n = view_forward(view) * -1.0f;
                break;
            case MoveType::RotateX:
                plane_n = axes[0];
                break;
            case MoveType::RotateY:
                plane_n = axes[1];
                break;
            case MoveType::RotateZ:
                plane_n = axes[2];
                break;
            default:
                return false;
            }

            Vec3 hit;
            if (!intersect_plane(ray_o, ray_d, origin, plane_n, hit))
            {
                plane_n = view_forward(view) * -1.0f;
                if (!intersect_plane(ray_o, ray_d, origin, plane_n, hit))
                {
                    return false;
                }
            }

            memcpy(g_ctx.matrix_start, matrix, sizeof(float) * 16);
            g_ctx.origin_start = origin;
            g_ctx.click_world = hit;
            g_ctx.axis_local[0] = axes[0];
            g_ctx.axis_local[1] = axes[1];
            g_ctx.axis_local[2] = axes[2];
            g_ctx.scale_start[0] = get_axis_scale(matrix, 0);
            g_ctx.scale_start[1] = get_axis_scale(matrix, 1);
            g_ctx.scale_start[2] = get_axis_scale(matrix, 2);
            g_ctx.drag_amount = 0.0f;
            g_ctx.rotation_ring_scale = gizmo_scale;

            if (type == MoveType::RotateX || type == MoveType::RotateY || type == MoveType::RotateZ || type == MoveType::RotateScreen)
            {
                g_ctx.click_world = normalize(hit - origin);
                g_ctx.angle_start = 0.0f;

                if (type == MoveType::RotateX)
                {
                    g_ctx.rotation_u = axes[1];
                    g_ctx.rotation_v = axes[2];
                }
                else if (type == MoveType::RotateY)
                {
                    g_ctx.rotation_u = axes[2];
                    g_ctx.rotation_v = axes[0];
                }
                else if (type == MoveType::RotateZ)
                {
                    g_ctx.rotation_u = axes[0];
                    g_ctx.rotation_v = axes[1];
                }
                else
                {
                    g_ctx.rotation_u = view_right(view);
                    g_ctx.rotation_v = view_up(view);
                    g_ctx.rotation_ring_scale = gizmo_scale * 1.15f;
                }
            }

            g_ctx.active = type;
            g_ctx.using_gizmo = true;
            g_ctx.delta = {};
            g_ctx.delta.active = true;
            g_ctx.delta.operation = g_ctx.operation;
            return true;
        }

        void apply_drag(const float* view, const float* proj, float* matrix, const float* snap, const ImVec2& mouse)
        {
            Vec3 ray_o, ray_d;
            screen_ray(view, proj, mouse, ray_o, ray_d);

            const Vec3 origin = g_ctx.origin_start;
            const MoveType type = g_ctx.active;
            Vec3 plane_n;

            switch (type)
            {
            case MoveType::AxisX:
            case MoveType::ScaleX:
                plane_n = normalize(cross(g_ctx.axis_local[0], cross(g_ctx.axis_local[0], ray_d)));
                break;
            case MoveType::AxisY:
            case MoveType::ScaleY:
                plane_n = normalize(cross(g_ctx.axis_local[1], cross(g_ctx.axis_local[1], ray_d)));
                break;
            case MoveType::AxisZ:
            case MoveType::ScaleZ:
                plane_n = normalize(cross(g_ctx.axis_local[2], cross(g_ctx.axis_local[2], ray_d)));
                break;
            case MoveType::PlaneYZ:
                plane_n = g_ctx.axis_local[0];
                break;
            case MoveType::PlaneZX:
                plane_n = g_ctx.axis_local[1];
                break;
            case MoveType::PlaneXY:
                plane_n = g_ctx.axis_local[2];
                break;
            case MoveType::Screen:
            case MoveType::ScaleXYZ:
            case MoveType::RotateScreen:
                plane_n = view_forward(view) * -1.0f;
                break;
            case MoveType::RotateX:
                plane_n = g_ctx.axis_local[0];
                break;
            case MoveType::RotateY:
                plane_n = g_ctx.axis_local[1];
                break;
            case MoveType::RotateZ:
                plane_n = g_ctx.axis_local[2];
                break;
            default:
                return;
            }

            Vec3 hit;
            if (!intersect_plane(ray_o, ray_d, origin, plane_n, hit))
            {
                return;
            }

            memcpy(matrix, g_ctx.matrix_start, sizeof(float) * 16);

            // translate
            if (type == MoveType::AxisX || type == MoveType::AxisY || type == MoveType::AxisZ ||
                type == MoveType::PlaneYZ || type == MoveType::PlaneZX || type == MoveType::PlaneXY ||
                type == MoveType::Screen)
            {
                Vec3 delta = hit - g_ctx.click_world;

                if (type == MoveType::AxisX)
                {
                    delta = g_ctx.axis_local[0] * dot(delta, g_ctx.axis_local[0]);
                }
                else if (type == MoveType::AxisY)
                {
                    delta = g_ctx.axis_local[1] * dot(delta, g_ctx.axis_local[1]);
                }
                else if (type == MoveType::AxisZ)
                {
                    delta = g_ctx.axis_local[2] * dot(delta, g_ctx.axis_local[2]);
                }
                else if (type == MoveType::PlaneYZ)
                {
                    delta = delta - g_ctx.axis_local[0] * dot(delta, g_ctx.axis_local[0]);
                }
                else if (type == MoveType::PlaneZX)
                {
                    delta = delta - g_ctx.axis_local[1] * dot(delta, g_ctx.axis_local[1]);
                }
                else if (type == MoveType::PlaneXY)
                {
                    delta = delta - g_ctx.axis_local[2] * dot(delta, g_ctx.axis_local[2]);
                }

                if (snap)
                {
                    // snap in local axis basis when possible
                    if (type == MoveType::AxisX)
                    {
                        const float d = snap_value(dot(delta, g_ctx.axis_local[0]), snap[0]);
                        delta = g_ctx.axis_local[0] * d;
                    }
                    else if (type == MoveType::AxisY)
                    {
                        const float d = snap_value(dot(delta, g_ctx.axis_local[1]), snap[1]);
                        delta = g_ctx.axis_local[1] * d;
                    }
                    else if (type == MoveType::AxisZ)
                    {
                        const float d = snap_value(dot(delta, g_ctx.axis_local[2]), snap[2]);
                        delta = g_ctx.axis_local[2] * d;
                    }
                    else
                    {
                        delta.x = snap_value(delta.x, snap[0]);
                        delta.y = snap_value(delta.y, snap[1]);
                        delta.z = snap_value(delta.z, snap[2]);
                    }
                }

                set_origin(matrix, origin + delta);
                g_ctx.delta.operation = Operation::Translate;
                g_ctx.delta.values[0] = delta.x;
                g_ctx.delta.values[1] = delta.y;
                g_ctx.delta.values[2] = delta.z;
                g_ctx.delta.active = true;
                return;
            }

            // rotate
            if (type == MoveType::RotateX || type == MoveType::RotateY || type == MoveType::RotateZ || type == MoveType::RotateScreen)
            {
                Vec3 axis;
                if (type == MoveType::RotateX) axis = g_ctx.axis_local[0];
                else if (type == MoveType::RotateY) axis = g_ctx.axis_local[1];
                else if (type == MoveType::RotateZ) axis = g_ctx.axis_local[2];
                else axis = plane_n;

                const Vec3 start_dir = normalize(g_ctx.click_world);
                const Vec3 cur_dir = normalize(hit - origin);
                const float sin_a = dot(cross(start_dir, cur_dir), axis);
                const float cos_a = dot(start_dir, cur_dir);
                float angle = atan2f(sin_a, cos_a);

                if (snap)
                {
                    const float step = snap[0] * deg_to_rad;
                    angle = snap_value(angle, step);
                }

                // wedge follows the mouse; matrix uses opposite sign for row-vector convention
                g_ctx.drag_amount = angle;

                float rot[16];
                build_rotation_matrix(axis, -angle, rot);

                // rotate axes around origin
                Vec3 ax[3];
                for (int i = 0; i < 3; i++)
                {
                    const float sx = g_ctx.scale_start[i];
                    Vec3 dir = g_ctx.axis_local[i];
                    transform_vector(rot, dir, ax[i]);
                    ax[i] = normalize(ax[i]);
                    set_axis_scaled(matrix, i, ax[i], sx);
                }
                set_origin(matrix, origin);

                g_ctx.delta.operation = Operation::Rotate;
                g_ctx.delta.values[0] = angle * rad_to_deg;
                g_ctx.delta.values[1] = 0.0f;
                g_ctx.delta.values[2] = 0.0f;
                g_ctx.delta.active = true;
                return;
            }

            // scale
            if (type == MoveType::ScaleX || type == MoveType::ScaleY || type == MoveType::ScaleZ || type == MoveType::ScaleXYZ)
            {
                int axis_index = 0;
                if (type == MoveType::ScaleY) axis_index = 1;
                if (type == MoveType::ScaleZ) axis_index = 2;

                float ratio = 1.0f;
                if (type == MoveType::ScaleXYZ)
                {
                    ImVec2 origin_s, click_s, mouse_s = mouse;
                    world_to_screen(view, proj, origin, origin_s);
                    world_to_screen(view, proj, g_ctx.click_world, click_s);
                    const float start_len = sqrtf((click_s.x - origin_s.x) * (click_s.x - origin_s.x) + (click_s.y - origin_s.y) * (click_s.y - origin_s.y));
                    const float cur_len = sqrtf((mouse_s.x - origin_s.x) * (mouse_s.x - origin_s.x) + (mouse_s.y - origin_s.y) * (mouse_s.y - origin_s.y));
                    ratio = (start_len > epsilon) ? (cur_len / start_len) : 1.0f;
                }
                else
                {
                    const float start = dot(g_ctx.click_world - origin, g_ctx.axis_local[axis_index]);
                    const float cur = dot(hit - origin, g_ctx.axis_local[axis_index]);
                    ratio = (fabsf(start) > epsilon) ? (cur / start) : 1.0f;
                }

                if (snap)
                {
                    // snap ratio to increments around 1
                    const float step = snap[axis_index];
                    if (step > epsilon)
                    {
                        const float delta_scale = (ratio - 1.0f) * g_ctx.scale_start[type == MoveType::ScaleXYZ ? 0 : axis_index];
                        const float snapped = snap_value(delta_scale, step);
                        ratio = 1.0f + snapped / (g_ctx.scale_start[type == MoveType::ScaleXYZ ? 0 : axis_index] > epsilon ? g_ctx.scale_start[type == MoveType::ScaleXYZ ? 0 : axis_index] : 1.0f);
                    }
                }

                ratio = std::max(0.001f, ratio);

                for (int i = 0; i < 3; i++)
                {
                    float s = g_ctx.scale_start[i];
                    if (type == MoveType::ScaleXYZ || i == axis_index)
                    {
                        s *= ratio;
                    }
                    set_axis_scaled(matrix, i, g_ctx.axis_local[i], s);
                }
                set_origin(matrix, origin);

                g_ctx.delta.operation = Operation::Scale;
                g_ctx.delta.values[0] = get_axis_scale(matrix, 0) / (g_ctx.scale_start[0] > epsilon ? g_ctx.scale_start[0] : 1.0f);
                g_ctx.delta.values[1] = get_axis_scale(matrix, 1) / (g_ctx.scale_start[1] > epsilon ? g_ctx.scale_start[1] : 1.0f);
                g_ctx.delta.values[2] = get_axis_scale(matrix, 2) / (g_ctx.scale_start[2] > epsilon ? g_ctx.scale_start[2] : 1.0f);
                g_ctx.delta.active = true;
            }
        }
    }

    void begin_frame()
    {
        ensure_style();
        if (!g_ctx.using_gizmo)
        {
            g_ctx.hover = MoveType::None;
            g_ctx.over_gizmo = false;
            g_ctx.delta.active = false;
        }
    }

    void set_rect(float x, float y, float w, float h)
    {
        g_ctx.rect_x = x;
        g_ctx.rect_y = y;
        g_ctx.rect_w = w;
        g_ctx.rect_h = h;
    }

    void set_draw_list(ImDrawList* list)
    {
        g_ctx.draw_list = list;
    }

    void set_orthographic(bool ortho)
    {
        g_ctx.orthographic = ortho;
    }

    bool manipulate(
        const float* view,
        const float* proj,
        Operation operation,
        Space space,
        float* matrix_16,
        float* delta_matrix_16,
        const float* snap
    )
    {
        ensure_style();
        if (!view || !proj || !matrix_16)
        {
            return false;
        }

        if (!g_ctx.draw_list)
        {
            g_ctx.draw_list = ImGui::GetWindowDrawList();
        }

        g_ctx.operation = operation;
        g_ctx.space = space;

        const Vec3 origin = get_origin(matrix_16);
        Vec3 axes[3];
        resolve_axes(matrix_16, space, axes);

        // draw axes may flip so handles stay in front of the object
        Vec3 draw_axes[3] = { axes[0], axes[1], axes[2] };
        const Vec3 cam_forward = view_forward(view);
        for (int i = 0; i < 3; i++)
        {
            if (dot(draw_axes[i], cam_forward) > 0.0f)
            {
                draw_axes[i] = draw_axes[i] * -1.0f;
            }
        }

        const float scale = compute_scale_factor(view, proj, origin);
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 mouse = io.MousePos;
        const bool mouse_in_rect =
            mouse.x >= g_ctx.rect_x && mouse.x <= g_ctx.rect_x + g_ctx.rect_w &&
            mouse.y >= g_ctx.rect_y && mouse.y <= g_ctx.rect_y + g_ctx.rect_h;

        MoveType hover = MoveType::None;
        if (!g_ctx.using_gizmo && mouse_in_rect)
        {
            if (operation == Operation::Translate || operation == Operation::Universal)
            {
                hover = hit_test_translate(view, proj, origin, draw_axes, scale, mouse);
            }
            if (hover == MoveType::None && (operation == Operation::Rotate || operation == Operation::Universal))
            {
                hover = hit_test_rotate(view, proj, origin, draw_axes, scale * (operation == Operation::Universal ? 0.85f : 1.0f), mouse);
            }
            if (hover == MoveType::None && (operation == Operation::Scale || operation == Operation::Universal))
            {
                hover = hit_test_scale(view, proj, origin, draw_axes, scale * (operation == Operation::Universal ? 0.7f : 1.0f), mouse);
            }
        }

        g_ctx.hover = hover;
        g_ctx.over_gizmo = (hover != MoveType::None) || g_ctx.using_gizmo;

        if (!g_ctx.using_gizmo && hover != MoveType::None && io.MouseClicked[0])
        {
            float interact_scale = scale;
            if (operation == Operation::Universal)
            {
                if (hover == MoveType::RotateX || hover == MoveType::RotateY || hover == MoveType::RotateZ || hover == MoveType::RotateScreen)
                {
                    interact_scale *= 0.85f;
                }
                else if (hover == MoveType::ScaleX || hover == MoveType::ScaleY || hover == MoveType::ScaleZ || hover == MoveType::ScaleXYZ)
                {
                    interact_scale *= 0.7f;
                }
            }
            begin_drag(hover, view, proj, matrix_16, axes, interact_scale, mouse);
        }

        if (g_ctx.using_gizmo)
        {
            if (io.MouseDown[0])
            {
                apply_drag(view, proj, matrix_16, snap, mouse);
            }
            else
            {
                g_ctx.using_gizmo = false;
                g_ctx.active = MoveType::None;
                g_ctx.delta.active = false;
            }
        }

        // draw
        const MoveType hl = g_ctx.using_gizmo ? g_ctx.active : hover;
        update_animation(hl);

        // rotate rings sit behind the arrows and boxes
        if (operation == Operation::Rotate || operation == Operation::Universal)
        {
            draw_rotate(view, proj, get_origin(matrix_16), draw_axes, scale * (operation == Operation::Universal ? 0.85f : 1.0f));
        }
        if (operation == Operation::Scale || operation == Operation::Universal)
        {
            draw_scale(view, proj, get_origin(matrix_16), draw_axes, scale * (operation == Operation::Universal ? 0.7f : 1.0f));
        }
        if (operation == Operation::Translate || operation == Operation::Universal)
        {
            draw_translate(view, proj, get_origin(matrix_16), draw_axes, scale);
        }

        draw_delta_hud(view, proj, get_origin(matrix_16));

        if (delta_matrix_16)
        {
            // delta = current * inverse(start) approximate via origin/axes diff
            identity_matrix(delta_matrix_16);
            if (g_ctx.using_gizmo)
            {
                const Vec3 cur = get_origin(matrix_16);
                const Vec3 d = cur - g_ctx.origin_start;
                delta_matrix_16[3]  = d.x;
                delta_matrix_16[7]  = d.y;
                delta_matrix_16[11] = d.z;
            }
        }

        return g_ctx.using_gizmo;
    }

    bool is_using()
    {
        return g_ctx.using_gizmo;
    }

    bool is_over()
    {
        return g_ctx.over_gizmo;
    }

    Style& get_style()
    {
        ensure_style();
        return g_style;
    }

    const DeltaInfo& get_delta_info()
    {
        return g_ctx.delta;
    }
}
