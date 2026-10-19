/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================================
#include "pch.h"
#include "TerrainDeformation.h"
#include "../commands/console/ConsoleCommands.h"
#include "../rhi/RHI_Vertex.h"
#include <cmath>
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        TConsoleVar<int32_t> cvar_deformation("terrain.deformation", 1, "tire ruts in soft terrain, 0 off, 1 on");
        TConsoleVar<float> cvar_softness("terrain.deformation_softness", 1.0f, "scales how deep tires sink, 1 is a soft loam");
        TConsoleVar<float> cvar_resistance("terrain.deformation_resistance", 1.0f, "scales the drag of pushing a fresh rut");
        TConsoleVar<int32_t> cvar_debug("terrain.deformation_debug", 0, "log rut stamping stats about once a second");

        atomic<uint32_t> stat_calls   = 0;
        atomic<uint32_t> stat_touched = 0;
        atomic<uint32_t> stat_rejected = 0;
        atomic<float> stat_depth      = 0.0f;
        atomic<float> stat_load       = 0.0f;

        // bekker pressure sinkage, p = (kc / b + kphi) * z^n, a soft loam between wong's sandy loam and his clay
        constexpr float soil_n           = 0.8f;
        constexpr float soil_kc          = 4000.0f;   // N / m^(n + 1)
        constexpr float soil_kphi        = 900000.0f; // N / m^(n + 2)
        constexpr float carcass_pressure = 30000.0f;  // Pa the sidewalls carry on top of the air
        constexpr float depth_max        = 0.25f;
        constexpr float load_min         = 150.0f;    // N, below this the tire is skimming

        constexpr int32_t res  = static_cast<int32_t>(TerrainDeformation::resolution);
        constexpr int32_t mask = res - 1;
        static_assert((TerrainDeformation::resolution & (TerrainDeformation::resolution - 1)) == 0, "the toroidal wrap masks, the resolution must be a power of two");

        struct Rect
        {
            int32_t x0 = 1;
            int32_t z0 = 1;
            int32_t x1 = 0;
            int32_t z1 = 0;

            bool IsEmpty() const { return x1 < x0 || z1 < z0; }

            void Merge(int32_t a_x0, int32_t a_z0, int32_t a_x1, int32_t a_z1)
            {
                if (IsEmpty())
                {
                    x0 = a_x0; z0 = a_z0; x1 = a_x1; z1 = a_z1;
                    return;
                }
                x0 = min(x0, a_x0);
                z0 = min(z0, a_z0);
                x1 = max(x1, a_x1);
                z1 = max(z1, a_z1);
            }
        };

        struct State
        {
            mutex lock;
            vector<float> cells;
            // cell coordinates of the window's min corner
            int32_t origin_x  = 0;
            int32_t origin_z  = 0;
            bool origin_valid = false;
            Rect content;     // world cells that hold anything, inclusive
            Rect dirty;       // storage texels to upload, inclusive
            bool full_upload = true;
        };

        State& state()
        {
            static State instance;
            return instance;
        }

        atomic<bool> content_flag = false;

        size_t cell_index(int32_t cx, int32_t cz)
        {
            return static_cast<size_t>(cz & mask) * res + static_cast<size_t>(cx & mask);
        }

        void ensure_cells(State& s)
        {
            if (s.cells.empty())
            {
                s.cells.assign(static_cast<size_t>(res) * res, 0.0f);
                s.full_upload = true;
            }
        }

        void reset_content(State& s)
        {
            s.content = Rect();
            content_flag.store(false, memory_order_relaxed);
        }

        // a world cell rect lands in storage as up to four rects once the wrap splits it
        void mark_dirty(State& s, int32_t cx0, int32_t cz0, int32_t cx1, int32_t cz1)
        {
            if (cx1 - cx0 + 1 >= res || cz1 - cz0 + 1 >= res)
            {
                s.full_upload = true;
                return;
            }

            const int32_t sx0 = cx0 & mask;
            const int32_t sx1 = cx1 & mask;
            const int32_t sz0 = cz0 & mask;
            const int32_t sz1 = cz1 & mask;
            const int32_t xs[2][2] = { { sx0, sx0 <= sx1 ? sx1 : mask }, { 0, sx1 } };
            const int32_t zs[2][2] = { { sz0, sz0 <= sz1 ? sz1 : mask }, { 0, sz1 } };
            const int32_t x_count  = sx0 <= sx1 ? 1 : 2;
            const int32_t z_count  = sz0 <= sz1 ? 1 : 2;
            for (int32_t i = 0; i < x_count; i++)
            {
                for (int32_t j = 0; j < z_count; j++)
                {
                    s.dirty.Merge(xs[i][0], zs[j][0], xs[i][1], zs[j][1]);
                }
            }
        }

        bool in_window(const State& s, int32_t cx, int32_t cz)
        {
            return cx >= s.origin_x && cx < s.origin_x + res && cz >= s.origin_z && cz < s.origin_z + res;
        }

        float cell_value(const State& s, int32_t cx, int32_t cz)
        {
            return in_window(s, cx, cz) ? s.cells[cell_index(cx, cz)] : 0.0f;
        }

        // cell centres sit at (c + 0.5) * cell_size, the gpu texel centres sit at the same places
        float sample_locked(const State& s, float world_x, float world_z)
        {
            const float gx  = world_x / TerrainDeformation::cell_size - 0.5f;
            const float gz  = world_z / TerrainDeformation::cell_size - 0.5f;
            const float fx0 = floorf(gx);
            const float fz0 = floorf(gz);
            const int32_t x0 = static_cast<int32_t>(fx0);
            const int32_t z0 = static_cast<int32_t>(fz0);
            const float tx   = gx - fx0;
            const float tz   = gz - fz0;
            const float a    = cell_value(s, x0, z0)         * (1.0f - tx) + cell_value(s, x0 + 1, z0)     * tx;
            const float b    = cell_value(s, x0, z0 + 1)     * (1.0f - tx) + cell_value(s, x0 + 1, z0 + 1) * tx;
            return a * (1.0f - tz) + b * tz;
        }

        float smoothstep(float edge0, float edge1, float x)
        {
            const float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
    }

    bool TerrainDeformation::IsEnabled()
    {
        return cvar_deformation.GetValue() != 0;
    }

    void TerrainDeformation::Clear()
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (!s.cells.empty() && !s.content.IsEmpty())
        {
            fill(s.cells.begin(), s.cells.end(), 0.0f);
            s.full_upload = true;
        }
        reset_content(s);
    }

    void TerrainDeformation::SetFocus(const Vector3& position)
    {
        if (!isfinite(position.x) || !isfinite(position.z))
        {
            return;
        }

        State& s = state();
        lock_guard<mutex> guard(s.lock);
        ensure_cells(s);

        const int32_t want_x = static_cast<int32_t>(floorf(position.x / cell_size)) - res / 2;
        const int32_t want_z = static_cast<int32_t>(floorf(position.z / cell_size)) - res / 2;
        if (!s.origin_valid)
        {
            s.origin_x     = want_x;
            s.origin_z     = want_z;
            s.origin_valid = true;
            return;
        }

        // the window only moves in eighths, a car crawling along does not rewrite strips every frame
        const int32_t dx = want_x - s.origin_x;
        const int32_t dz = want_z - s.origin_z;
        if (abs(dx) < res / 8 && abs(dz) < res / 8)
        {
            return;
        }

        if (abs(dx) >= res || abs(dz) >= res)
        {
            fill(s.cells.begin(), s.cells.end(), 0.0f);
            s.full_upload = true;
            reset_content(s);
        }
        else
        {
            // the columns and rows that enter reuse the storage of the ones that leave, erase them
            const int32_t enter_x0 = dx > 0 ? s.origin_x + res : want_x;
            const int32_t enter_x1 = dx > 0 ? want_x + res - 1 : s.origin_x - 1;
            for (int32_t cx = enter_x0; cx <= enter_x1; cx++)
            {
                const int32_t column = cx & mask;
                for (int32_t row = 0; row < res; row++)
                {
                    s.cells[static_cast<size_t>(row) * res + column] = 0.0f;
                }
            }
            if (dx != 0)
            {
                mark_dirty(s, enter_x0, 0, enter_x1, res - 1);
            }

            const int32_t enter_z0 = dz > 0 ? s.origin_z + res : want_z;
            const int32_t enter_z1 = dz > 0 ? want_z + res - 1 : s.origin_z - 1;
            for (int32_t cz = enter_z0; cz <= enter_z1; cz++)
            {
                float* row = s.cells.data() + static_cast<size_t>(cz & mask) * res;
                fill(row, row + res, 0.0f);
            }
            if (dz != 0)
            {
                mark_dirty(s, 0, enter_z0, res - 1, enter_z1);
            }
        }

        s.origin_x = want_x;
        s.origin_z = want_z;

        // whatever stayed is still where it was, the bounds only shrink to the new window
        if (!s.content.IsEmpty())
        {
            s.content.x0 = max(s.content.x0, s.origin_x);
            s.content.z0 = max(s.content.z0, s.origin_z);
            s.content.x1 = min(s.content.x1, s.origin_x + res - 1);
            s.content.z1 = min(s.content.z1, s.origin_z + res - 1);
            if (s.content.IsEmpty())
            {
                reset_content(s);
            }
        }
    }

    float TerrainDeformation::Stamp(const TerrainDeformationStamp& stamp)
    {
        stat_calls.fetch_add(1, memory_order_relaxed);
        stat_load.store(stamp.load, memory_order_relaxed);
        if (!IsEnabled() || !(stamp.load > load_min) || !(stamp.dt > 0.0f))
        {
            stat_rejected.fetch_add(1, memory_order_relaxed);
            return 0.0f;
        }

        // rock and scree do not give, the soil softens in over the gentle slopes
        const float slope_softness = smoothstep(0.707f, 0.906f, stamp.normal.y);
        const float softness       = max(cvar_softness.GetValue(), 0.0f) * slope_softness;
        if (softness <= 0.0f)
        {
            return 0.0f;
        }

        const float width    = clamp(stamp.width, 0.08f, 0.6f);
        const float radius   = clamp(stamp.radius, 0.15f, 1.0f);
        const float diameter = radius * 2.0f;

        // rigid wheel sinkage, z = (3 W / ((3 - n) (kc + b kphi) sqrt(D)))^(2 / (2n + 1)), heavier, narrower
        // and smaller wheels all sink further
        const float k_wheel = soil_kc + width * soil_kphi;
        const float z_rigid = powf(3.0f * stamp.load / ((3.0f - soil_n) * k_wheel * sqrtf(diameter)), 2.0f / (2.0f * soil_n + 1.0f));
        // a pneumatic tire flattens once the soil pushes back harder than its air does, from there on the
        // patch presses like a plate at inflation pressure, which is why aired down tires float
        const float k_plate = soil_kc / width + soil_kphi;
        const float p_tire  = max(stamp.pressure, 0.3f) * 100000.0f + carcass_pressure;
        const float z_plate = powf(p_tire / k_plate, 1.0f / soil_n);
        float depth         = min(z_rigid, z_plate) * softness;
        depth               = min(depth, min(depth_max, radius * 0.5f));
        if (!isfinite(depth) || depth < 0.002f)
        {
            return 0.0f;
        }

        // the patch is the chord the sunk wheel cuts, the flattened tire never leaves less than that
        const float patch_length = clamp(2.0f * sqrtf(max(depth * (diameter - depth), 0.0f)), 0.12f, 0.6f);

        Vector3 delta(stamp.position.x - stamp.position_previous.x, 0.0f, stamp.position.z - stamp.position_previous.z);
        const float travel = delta.Length();
        if (!isfinite(travel) || travel > 5.0f)
        {
            delta = Vector3::Zero;
        }
        Vector3 forward(stamp.forward.x, 0.0f, stamp.forward.z);
        if (forward.LengthSquared() < 1e-4f)
        {
            forward = travel > 1e-3f ? delta / travel : Vector3::Forward;
        }
        forward.Normalize();
        const Vector3 right(forward.z, 0.0f, -forward.x);
        const Vector3 motion  = travel > 1e-3f && travel <= 5.0f ? delta / travel : forward;

        const Vector3 center  = (stamp.position + stamp.position_previous) * 0.5f;
        const float along     = fabsf(delta.Dot(forward));
        const float across    = fabsf(delta.Dot(right));
        const float half_len  = patch_length * 0.5f + along * 0.5f;
        const float half_wid  = width * 0.5f + across * 0.5f;
        const float berm_wid  = width * 0.5f;
        const float berm_high = depth * 0.3f;
        const float reach     = half_wid + berm_wid;

        // wheelspin digs below the static sinkage, the tread throws soil out at its slip speed, a slip ratio
        // would read a creeping tire as spinning
        const float ground_speed = fabsf(delta.Dot(forward)) / stamp.dt;
        const float slip_speed   = fabsf(stamp.tread_speed) - ground_speed;
        const float dig          = slip_speed > 1.0f ? min(slip_speed - 1.0f, 10.0f) * 0.004f * stamp.dt : 0.0f;

        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (!s.origin_valid)
        {
            return 0.0f;
        }
        ensure_cells(s);

        // compaction work per metre is the integral of the bekker pressure over the fresh sinkage, only
        // the leading edge cuts fresh soil, so the rear wheels following the fronts barely pay it
        const Vector3 lead   = stamp.position + motion * (patch_length * 0.5f);
        const float existing = max(-sample_locked(s, lead.x, lead.z), 0.0f);
        const float fresh    = max(depth - existing, 0.0f);
        const float speed    = travel / stamp.dt;
        const float resistance = k_wheel * powf(fresh, soil_n + 1.0f) / (soil_n + 1.0f)
            * max(cvar_resistance.GetValue(), 0.0f) * smoothstep(0.0f, 0.5f, speed);

        const float extent_x = fabsf(forward.x) * half_len + fabsf(right.x) * reach;
        const float extent_z = fabsf(forward.z) * half_len + fabsf(right.z) * reach;
        const int32_t cx0 = max(static_cast<int32_t>(floorf((center.x - extent_x) / cell_size)), s.origin_x);
        const int32_t cz0 = max(static_cast<int32_t>(floorf((center.z - extent_z) / cell_size)), s.origin_z);
        const int32_t cx1 = min(static_cast<int32_t>(floorf((center.x + extent_x) / cell_size)), s.origin_x + res - 1);
        const int32_t cz1 = min(static_cast<int32_t>(floorf((center.z + extent_z) / cell_size)), s.origin_z + res - 1);
        if (cx1 < cx0 || cz1 < cz0)
        {
            return resistance;
        }

        bool touched = false;
        for (int32_t cz = cz0; cz <= cz1; cz++)
        {
            for (int32_t cx = cx0; cx <= cx1; cx++)
            {
                const float wx = (static_cast<float>(cx) + 0.5f) * cell_size - center.x;
                const float wz = (static_cast<float>(cz) + 0.5f) * cell_size - center.z;
                const float u  = wx * forward.x + wz * forward.z;
                const float v  = fabsf(wx * right.x + wz * right.z);
                if (fabsf(u) > half_len || v > reach)
                {
                    continue;
                }

                float& h = s.cells[cell_index(cx, cz)];
                if (v <= half_wid)
                {
                    // a flat floor, the walls round off over the outer part of the tread like the shoulders do
                    const float t = v / half_wid;
                    const float t2 = t * t;
                    const float profile = 1.0f - t2 * t2 * t2;
                    float target = -depth * profile;
                    if (dig > 0.0f)
                    {
                        target = min(target, h - dig * profile);
                    }
                    target = max(target, -depth_max);
                    if (target < h)
                    {
                        h = target;
                        touched = true;
                    }
                }
                else if (h > -0.005f)
                {
                    // the displaced soil heaps up beside the tread
                    const float t    = (v - half_wid) / berm_wid;
                    const float berm = berm_high * 4.0f * t * (1.0f - t);
                    if (berm > h)
                    {
                        h = berm;
                        touched = true;
                    }
                }
            }
        }

        stat_depth.store(depth, memory_order_relaxed);
        if (touched)
        {
            stat_touched.fetch_add(1, memory_order_relaxed);
            mark_dirty(s, cx0, cz0, cx1, cz1);
            s.content.Merge(cx0, cz0, cx1, cz1);
            content_flag.store(true, memory_order_relaxed);
        }

        return resistance;
    }

    float TerrainDeformation::SampleOffset(float world_x, float world_z)
    {
        if (!content_flag.load(memory_order_relaxed) || !IsEnabled())
        {
            return 0.0f;
        }

        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (s.cells.empty() || !s.origin_valid)
        {
            return 0.0f;
        }
        return sample_locked(s, world_x, world_z);
    }

    bool TerrainDeformation::CollectUploads(vector<TerrainDeformationUpload>& uploads)
    {
        uploads.clear();

        State& s = state();
        lock_guard<mutex> guard(s.lock);
        ensure_cells(s);

        Rect rect = s.dirty;
        if (s.full_upload)
        {
            rect = { 0, 0, mask, mask };
        }
        if (rect.IsEmpty())
        {
            return false;
        }

        TerrainDeformationUpload& upload = uploads.emplace_back();
        upload.x      = static_cast<uint32_t>(rect.x0);
        upload.y      = static_cast<uint32_t>(rect.z0);
        upload.width  = static_cast<uint32_t>(rect.x1 - rect.x0 + 1);
        upload.height = static_cast<uint32_t>(rect.z1 - rect.z0 + 1);
        upload.texels.resize(static_cast<size_t>(upload.width) * upload.height);
        for (uint32_t z = 0; z < upload.height; z++)
        {
            const float* source = s.cells.data() + static_cast<size_t>(upload.y + z) * res + upload.x;
            uint16_t* target    = upload.texels.data() + static_cast<size_t>(z) * upload.width;
            for (uint32_t x = 0; x < upload.width; x++)
            {
                target[x] = vertex_pack::float_to_half(source[x]);
            }
        }

        s.dirty       = Rect();
        s.full_upload = false;
        return true;
    }

    Vector4 TerrainDeformation::GetMapping()
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        const bool active = IsEnabled() && s.origin_valid && !s.content.IsEmpty();
        return Vector4(
            static_cast<float>(s.origin_x) * cell_size,
            static_cast<float>(s.origin_z) * cell_size,
            1.0f / window_size,
            active ? 1.0f : 0.0f
        );
    }

    Vector4 TerrainDeformation::GetBounds()
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (s.content.IsEmpty())
        {
            return Vector4::Zero;
        }
        // one cell of margin, a bilinear tap half a cell outside still reads the edge
        return Vector4(
            static_cast<float>(s.content.x0 - 1) * cell_size,
            static_cast<float>(s.content.z0 - 1) * cell_size,
            static_cast<float>(s.content.x1 + 2) * cell_size,
            static_cast<float>(s.content.z1 + 2) * cell_size
        );
    }

    bool TerrainDeformation::HasContent()
    {
        return content_flag.load(memory_order_relaxed) && IsEnabled();
    }

    void TerrainDeformation::LogStats(bool gpu_ready)
    {
        static uint32_t frame = 0;
        if (cvar_debug.GetValue() == 0 || (++frame % 60) != 0)
        {
            return;
        }

        State& s = state();
        float deepest = 0.0f;
        Rect content;
        int32_t origin_x = 0;
        int32_t origin_z = 0;
        {
            lock_guard<mutex> guard(s.lock);
            content  = s.content;
            origin_x = s.origin_x;
            origin_z = s.origin_z;
            for (float h : s.cells)
            {
                deepest = min(deepest, h);
            }
        }

        SP_LOG_INFO("terrain deformation: calls %u, rejected %u, touched %u, last load %.0f N, last depth %.3f m, deepest %.3f m, origin (%.1f, %.1f), content x %d..%d z %d..%d, gpu %d",
            stat_calls.exchange(0), stat_rejected.exchange(0), stat_touched.exchange(0), stat_load.load(), stat_depth.load(), deepest,
            origin_x * cell_size, origin_z * cell_size, content.x0, content.x1, content.z0, content.z1, gpu_ready ? 1 : 0);
    }
}
