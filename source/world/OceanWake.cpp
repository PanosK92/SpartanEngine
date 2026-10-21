/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================================
#include "pch.h"
#include "OceanWake.h"
#include "components/Terrain.h"
#include "../commands/console/ConsoleCommands.h"
#include "../rhi/RHI_Vertex.h"
#include <cmath>
#include <mutex>
#include <unordered_map>
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        TConsoleVar<int32_t> cvar_wake("ocean.wake", 1, "interactive water, bodies in the sea push waves around, 0 off, 1 on");
        TConsoleVar<float> cvar_strength("ocean.wake_strength", 1.0f, "scales the water a moving body pushes aside, 1 conserves the displaced volume");
        TConsoleVar<float> cvar_damping("ocean.wake_damping", 0.2f, "how fast the wake calms down, per second");
        TConsoleVar<float> cvar_depth("ocean.wake_depth", 1.5f, "the depth the waves feel at most, m, sets how fast they run (sqrt(g * depth)) and so the angle of the v behind a hull");

        constexpr int32_t res          = static_cast<int32_t>(OceanWake::resolution);
        constexpr int32_t mask         = res - 1;
        constexpr float step_dt        = 1.0f / 120.0f;
        constexpr uint32_t steps_max   = 4;
        constexpr float gravity        = 9.81f;
        constexpr float depth_dry      = 0.03f;  // m, shallower cells are beach the waves reflect from
        constexpr int32_t sponge_cells = 24;     // the window edge absorbs instead of wrapping waves around
        constexpr float sponge_rate    = 6.0f;   // per second at the very edge
        constexpr float level_rate     = 0.05f;  // per second, a heaped sea settles back to the ocean level
        constexpr float foam_life      = 3.5f;   // seconds for the wake foam to fade to a third
        constexpr float height_max     = 3.0f;   // m, a body teleported into the sea must not blow the field up
        constexpr uint32_t stale_ticks = 30;     // ticks without a report before a body counts as gone
        static_assert((OceanWake::resolution & (OceanWake::resolution - 1)) == 0, "the toroidal wrap masks, the resolution must be a power of two");

        struct Body
        {
            vector<OceanWakeColumn> current;
            vector<OceanWakeColumn> previous;
            bool has_previous = false;
            uint32_t idle     = 0; // ticks since the last report
            bool reported     = false;
        };

        struct State
        {
            mutex lock;
            vector<float> height;
            vector<float> velocity;
            vector<float> foam;
            vector<float> speed2;  // squared wave speed, 0 on dry cells
            vector<float> source;
            vector<int32_t> touched;
            vector<uint32_t> hull_stamp; // cells under the body being splatted carry its stamp
            vector<uint32_t> hull_distance;
            uint32_t stamp      = 0;
            float sponge_x[res] = {};
            float sponge_z[res] = {};
            unordered_map<uint64_t, Body> bodies;
            int32_t origin_x  = 0; // cell coordinates of the window's min corner
            int32_t origin_z  = 0;
            bool origin_valid = false;
            float sea_level   = numeric_limits<float>::quiet_NaN();
            float depth_key   = 0.0f;
            float accumulator = 0.0f;
            float quiet_time  = 0.0f;
            bool active       = false;
            bool upload_zero  = false; // one upload of a flat field after the waves died out
        };

        State& state()
        {
            static State instance;
            return instance;
        }

        size_t cell_index(int32_t cx, int32_t cz)
        {
            return static_cast<size_t>(cz & mask) * res + static_cast<size_t>(cx & mask);
        }

        void ensure_cells(State& s)
        {
            if (s.height.empty())
            {
                const size_t count = static_cast<size_t>(res) * res;
                s.height.assign(count, 0.0f);
                s.velocity.assign(count, 0.0f);
                s.foam.assign(count, 0.0f);
                s.speed2.assign(count, 0.0f);
                s.source.assign(count, 0.0f);
                s.hull_stamp.assign(count, 0);
                s.hull_distance.assign(count, 0);
            }
        }

        bool in_window(const State& s, int32_t cx, int32_t cz)
        {
            return s.origin_valid && cx >= s.origin_x && cx < s.origin_x + res && cz >= s.origin_z && cz < s.origin_z + res;
        }

        float cell_height(const State& s, int32_t cx, int32_t cz)
        {
            return in_window(s, cx, cz) ? s.height[cell_index(cx, cz)] : 0.0f;
        }

        // cell centres sit at (c + 0.5) * cell_size, the gpu texel centres sit at the same places
        float sample_locked(const State& s, float world_x, float world_z)
        {
            if (!s.active || s.height.empty())
            {
                return 0.0f;
            }

            const float gx   = world_x / OceanWake::cell_size - 0.5f;
            const float gz   = world_z / OceanWake::cell_size - 0.5f;
            const float fx0  = floorf(gx);
            const float fz0  = floorf(gz);
            const int32_t x0 = static_cast<int32_t>(fx0);
            const int32_t z0 = static_cast<int32_t>(fz0);
            const float tx   = gx - fx0;
            const float tz   = gz - fz0;
            const float a    = cell_height(s, x0, z0)     * (1.0f - tx) + cell_height(s, x0 + 1, z0)     * tx;
            const float b    = cell_height(s, x0, z0 + 1) * (1.0f - tx) + cell_height(s, x0 + 1, z0 + 1) * tx;
            return a * (1.0f - tz) + b * tz;
        }

        // the bed decides where the sea is and how fast its waves run, shallow water slows them down
        void reset_cell(State& s, Terrain* terrain, int32_t cx, int32_t cz)
        {
            const size_t i = cell_index(cx, cz);
            s.height[i]    = 0.0f;
            s.velocity[i]  = 0.0f;
            s.foam[i]      = 0.0f;
            s.source[i]    = 0.0f;

            float depth = 1000.0f;
            float bed   = 0.0f;
            if (terrain && terrain->SampleHeight((cx + 0.5f) * OceanWake::cell_size, (cz + 0.5f) * OceanWake::cell_size, bed))
            {
                depth = s.sea_level - bed;
            }
            s.speed2[i] = depth > depth_dry ? gravity * min(depth, s.depth_key) : 0.0f;
        }

        void reset_columns(State& s, Terrain* terrain, int32_t cx0, int32_t cx1)
        {
            for (int32_t cx = cx0; cx < cx1; cx++)
            {
                for (int32_t cz = s.origin_z; cz < s.origin_z + res; cz++)
                {
                    reset_cell(s, terrain, cx, cz);
                }
            }
        }

        void reset_rows(State& s, Terrain* terrain, int32_t cz0, int32_t cz1)
        {
            for (int32_t cz = cz0; cz < cz1; cz++)
            {
                for (int32_t cx = s.origin_x; cx < s.origin_x + res; cx++)
                {
                    reset_cell(s, terrain, cx, cz);
                }
            }
        }

        // cells scrolling into the window start calm, a big jump or a new sea level rebuilds all of it
        void move_window(State& s, const Vector3& focus, float sea_level)
        {
            const int32_t target_x = static_cast<int32_t>(floorf(focus.x / OceanWake::cell_size)) - res / 2;
            const int32_t target_z = static_cast<int32_t>(floorf(focus.z / OceanWake::cell_size)) - res / 2;
            const float depth      = max(cvar_depth.GetValue(), 0.1f);
            Terrain* terrain       = Terrain::FindActive();

            const bool rebuild = !s.origin_valid || sea_level != s.sea_level || depth != s.depth_key || abs(target_x - s.origin_x) >= res || abs(target_z - s.origin_z) >= res;
            if (rebuild)
            {
                s.sea_level    = sea_level;
                s.depth_key    = depth;
                s.origin_x     = target_x;
                s.origin_z     = target_z;
                s.origin_valid = true;
                reset_rows(s, terrain, s.origin_z, s.origin_z + res);
                return;
            }

            // keep the camera off the sponge, the window only moves once the focus drifts a few metres off centre
            const int32_t slack = 8;
            int32_t dx = target_x - s.origin_x;
            int32_t dz = target_z - s.origin_z;
            dx = abs(dx) > slack ? dx : 0;
            dz = abs(dz) > slack ? dz : 0;
            if (dx != 0)
            {
                const int32_t old_x = s.origin_x;
                s.origin_x          = target_x;
                if (dx > 0)
                {
                    reset_columns(s, terrain, old_x + res, s.origin_x + res);
                }
                else
                {
                    reset_columns(s, terrain, s.origin_x, old_x);
                }
            }
            if (dz != 0)
            {
                const int32_t old_z = s.origin_z;
                s.origin_z          = target_z;
                if (dz > 0)
                {
                    reset_rows(s, terrain, old_z + res, s.origin_z + res);
                }
                else
                {
                    reset_rows(s, terrain, s.origin_z, old_z);
                }
            }
        }

        // water held out of its place lands around the slice, a bilinear tap spread by a 3x3 binomial so a hull
        // sliding less than a cell per step still sheds a smooth wave instead of a staircase
        void splat(State& s, const vector<OceanWakeColumn>& columns, float sign)
        {
            static const float binomial[3] = { 0.25f, 0.5f, 0.25f };
            const float to_height          = sign * max(cvar_strength.GetValue(), 0.0f) / (OceanWake::cell_size * OceanWake::cell_size);
            for (const OceanWakeColumn& column : columns)
            {
                if (!(column.volume > 0.0f) || !isfinite(column.x) || !isfinite(column.z))
                {
                    continue;
                }

                const float gx   = column.x / OceanWake::cell_size - 0.5f;
                const float gz   = column.z / OceanWake::cell_size - 0.5f;
                const float fx0  = floorf(gx);
                const float fz0  = floorf(gz);
                const int32_t x0 = static_cast<int32_t>(fx0);
                const int32_t z0 = static_cast<int32_t>(fz0);
                const float tx   = gx - fx0;
                const float tz   = gz - fz0;
                const float corners[4] = { (1.0f - tx) * (1.0f - tz), tx * (1.0f - tz), (1.0f - tx) * tz, tx * tz };
                const float amount     = column.volume * to_height;
                for (int32_t corner = 0; corner < 4; corner++)
                {
                    const int32_t cx = x0 + (corner & 1);
                    const int32_t cz = z0 + (corner >> 1);
                    for (int32_t j = -1; j <= 1; j++)
                    {
                        for (int32_t k = -1; k <= 1; k++)
                        {
                            if (!in_window(s, cx + k, cz + j))
                            {
                                continue;
                            }
                            const size_t i = cell_index(cx + k, cz + j);
                            if (s.source[i] == 0.0f)
                            {
                                s.touched.push_back(static_cast<int32_t>(i));
                            }
                            s.source[i] += amount * corners[corner] * binomial[j + 1] * binomial[k + 1];
                        }
                    }
                }
            }
        }

        void add_source(State& s, size_t i, float amount)
        {
            if (s.source[i] == 0.0f)
            {
                s.touched.push_back(static_cast<int32_t>(i));
            }
            s.source[i] += amount;
        }

        // the hull stands where its cells are, water it pushes can only rise around it, so whatever the splat left
        // under the hull walks down the distance to the nearest open water, the bow wave forms ahead of the hull and
        // the trough it leaves stays behind it instead of a mound the hull would carry along and sink into
        void push_out_of_hull(State& s, const vector<OceanWakeColumn>& columns)
        {
            struct HullCell
            {
                int32_t cx;
                int32_t cz;
            };
            thread_local vector<HullCell> cells;
            thread_local vector<HullCell> frontier;
            cells.clear();
            frontier.clear();

            s.stamp = s.stamp == numeric_limits<uint32_t>::max() ? 1 : s.stamp + 1;
            if (s.stamp == 1)
            {
                fill(s.hull_stamp.begin(), s.hull_stamp.end(), 0u);
            }

            for (const OceanWakeColumn& column : columns)
            {
                if (!(column.volume > 0.0f) || !isfinite(column.x) || !isfinite(column.z))
                {
                    continue;
                }
                const int32_t cx = static_cast<int32_t>(floorf(column.x / OceanWake::cell_size));
                const int32_t cz = static_cast<int32_t>(floorf(column.z / OceanWake::cell_size));
                if (!in_window(s, cx, cz))
                {
                    continue;
                }
                const size_t i = cell_index(cx, cz);
                if (s.hull_stamp[i] != s.stamp)
                {
                    s.hull_stamp[i]    = s.stamp;
                    s.hull_distance[i] = 0;
                    cells.push_back({ cx, cz });
                }
            }
            if (cells.empty())
            {
                return;
            }

            // breadth first from the rim, open water is distance 0
            static const int32_t offsets[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (const HullCell& cell : cells)
            {
                for (const auto& offset : offsets)
                {
                    if (s.hull_stamp[cell_index(cell.cx + offset[0], cell.cz + offset[1])] != s.stamp)
                    {
                        s.hull_distance[cell_index(cell.cx, cell.cz)] = 1;
                        frontier.push_back(cell);
                        break;
                    }
                }
            }
            for (size_t head = 0; head < frontier.size(); head++)
            {
                const HullCell cell     = frontier[head];
                const uint32_t distance = s.hull_distance[cell_index(cell.cx, cell.cz)];
                for (const auto& offset : offsets)
                {
                    const size_t n = cell_index(cell.cx + offset[0], cell.cz + offset[1]);
                    if (s.hull_stamp[n] == s.stamp && s.hull_distance[n] == 0)
                    {
                        s.hull_distance[n] = distance + 1;
                        frontier.push_back({ cell.cx + offset[0], cell.cz + offset[1] });
                    }
                }
            }

            // deepest cells first, each hands its water to the neighbours one step closer to the rim
            for (size_t k = frontier.size(); k-- > 0;)
            {
                const HullCell cell     = frontier[k];
                const size_t i          = cell_index(cell.cx, cell.cz);
                const float amount      = s.source[i];
                if (amount == 0.0f)
                {
                    continue;
                }
                const uint32_t distance = s.hull_distance[i];

                uint32_t targets = 0;
                for (const auto& offset : offsets)
                {
                    const size_t n = cell_index(cell.cx + offset[0], cell.cz + offset[1]);
                    targets += (s.hull_stamp[n] != s.stamp || s.hull_distance[n] < distance) ? 1 : 0;
                }
                if (targets == 0)
                {
                    continue;
                }

                const float share = amount / static_cast<float>(targets);
                s.source[i]       = 0.0f;
                for (const auto& offset : offsets)
                {
                    const int32_t nx = cell.cx + offset[0];
                    const int32_t nz = cell.cz + offset[1];
                    const size_t n   = cell_index(nx, nz);
                    if (!in_window(s, nx, nz))
                    {
                        continue;
                    }
                    if (s.hull_stamp[n] == s.stamp)
                    {
                        if (s.hull_distance[n] < distance)
                        {
                            add_source(s, n, share);
                        }
                        continue;
                    }

                    // open water, spread over the open cells around it so the rim does not take a one cell spike
                    static const float binomial[3] = { 0.25f, 0.5f, 0.25f };
                    float weight_sum = 0.0f;
                    for (int32_t j = -1; j <= 1; j++)
                    {
                        for (int32_t k = -1; k <= 1; k++)
                        {
                            const size_t m = cell_index(nx + k, nz + j);
                            weight_sum    += (s.hull_stamp[m] != s.stamp && in_window(s, nx + k, nz + j)) ? binomial[j + 1] * binomial[k + 1] : 0.0f;
                        }
                    }
                    for (int32_t j = -1; j <= 1; j++)
                    {
                        for (int32_t k = -1; k <= 1; k++)
                        {
                            const size_t m = cell_index(nx + k, nz + j);
                            if (s.hull_stamp[m] != s.stamp && in_window(s, nx + k, nz + j))
                            {
                                add_source(s, m, share * binomial[j + 1] * binomial[k + 1] / weight_sum);
                            }
                        }
                    }
                }
            }
        }

        // the edge of the window swallows waves so nothing wraps around the torus
        void update_sponge(State& s)
        {
            for (int32_t i = 0; i < res; i++)
            {
                const int32_t lx    = (i - s.origin_x) & mask;
                const int32_t lz    = (i - s.origin_z) & mask;
                const int32_t ex    = min(lx, res - 1 - lx);
                const int32_t ez    = min(lz, res - 1 - lz);
                const float fx      = ex < sponge_cells ? 1.0f - static_cast<float>(ex) / sponge_cells : 0.0f;
                const float fz      = ez < sponge_cells ? 1.0f - static_cast<float>(ez) / sponge_cells : 0.0f;
                s.sponge_x[i]       = sponge_rate * fx * fx;
                s.sponge_z[i]       = sponge_rate * fz * fz;
            }
        }

        // linear shallow water waves, symplectic euler, stable while c * dt / dx stays under 0.7 (here ~0.13)
        float step(State& s, bool apply_source)
        {
            const float inv_dx2 = 1.0f / (OceanWake::cell_size * OceanWake::cell_size);
            const float damping = max(cvar_damping.GetValue(), 0.0f);
            const float decay   = expf(-step_dt * 1.1f / foam_life);
            float peak          = 0.0f;

            if (apply_source)
            {
                for (int32_t i : s.touched)
                {
                    if (s.speed2[i] > 0.0f)
                    {
                        s.height[i] = clamp(s.height[i] + s.source[i], -height_max, height_max);
                        s.foam[i]   = min(s.foam[i] + fabsf(s.source[i]) * 2.5f, 1.0f);
                    }
                    s.source[i] = 0.0f;
                }
                s.touched.clear();
            }

            for (int32_t z = 0; z < res; z++)
            {
                const int32_t up   = ((z + 1) & mask) * res;
                const int32_t down = ((z - 1) & mask) * res;
                const int32_t row  = z * res;
                for (int32_t x = 0; x < res; x++)
                {
                    const int32_t i = row + x;
                    if (s.speed2[i] <= 0.0f)
                    {
                        s.velocity[i] = 0.0f;
                        continue;
                    }

                    // a dry neighbour mirrors the cell, the beach reflects the wave
                    const float h       = s.height[i];
                    const int32_t left  = row + ((x - 1) & mask);
                    const int32_t right = row + ((x + 1) & mask);
                    const int32_t north = up + x;
                    const int32_t south = down + x;
                    const float hl      = s.speed2[left]  > 0.0f ? s.height[left]  : h;
                    const float hr      = s.speed2[right] > 0.0f ? s.height[right] : h;
                    const float hn      = s.speed2[north] > 0.0f ? s.height[north] : h;
                    const float hs      = s.speed2[south] > 0.0f ? s.height[south] : h;
                    const float laplace = (hl + hr + hn + hs - 4.0f * h) * inv_dx2;

                    const float rate = damping + max(s.sponge_x[x], s.sponge_z[z]);
                    s.velocity[i]    = (s.velocity[i] + s.speed2[i] * laplace * step_dt) * max(1.0f - rate * step_dt, 0.0f);
                }
            }

            for (int32_t z = 0; z < res; z++)
            {
                const int32_t row = z * res;
                for (int32_t x = 0; x < res; x++)
                {
                    const int32_t i = row + x;
                    if (s.speed2[i] <= 0.0f)
                    {
                        s.height[i] = 0.0f;
                        s.foam[i]   = 0.0f;
                        continue;
                    }

                    const float settle = level_rate + max(s.sponge_x[x], s.sponge_z[z]) * 0.5f;
                    const float v      = s.velocity[i];
                    s.height[i]        = clamp((s.height[i] + v * step_dt) * (1.0f - settle * step_dt), -height_max, height_max);
                    // only water thrown up or dropping fast breaks into whitewater, a travelling ripple stays clear,
                    // the foam fades where it was made so the wake leaves a trail
                    s.foam[i]          = min(s.foam[i] * decay + max(fabsf(v) - 1.5f, 0.0f) * step_dt * 0.5f, 1.0f);
                    peak               = max(peak, fabsf(s.height[i]));
                }
            }

            return peak;
        }

        void clear_locked(State& s)
        {
            if (!s.height.empty())
            {
                fill(s.height.begin(), s.height.end(), 0.0f);
                fill(s.velocity.begin(), s.velocity.end(), 0.0f);
                fill(s.foam.begin(), s.foam.end(), 0.0f);
                fill(s.source.begin(), s.source.end(), 0.0f);
            }
            s.touched.clear();
            s.bodies.clear();
            s.accumulator = 0.0f;
            s.quiet_time  = 0.0f;
            s.upload_zero = s.active;
            s.active      = false;
        }
    }

    bool OceanWake::IsEnabled()
    {
        return cvar_wake.GetValue() != 0;
    }

    void OceanWake::Clear()
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        clear_locked(s);
    }

    void OceanWake::Displace(uint64_t id, const OceanWakeColumn* columns, uint32_t count)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (count == 0 || !columns)
        {
            s.bodies.erase(id);
            return;
        }

        Body& body = s.bodies[id];
        body.current.assign(columns, columns + count);
        body.reported = true;
        body.idle     = 0;
        s.active      = true;
        s.quiet_time  = 0.0f;
    }

    void OceanWake::Tick(float dt, const Vector3& focus, float sea_level)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (!IsEnabled())
        {
            clear_locked(s);
            return;
        }

        ensure_cells(s);
        move_window(s, focus, sea_level);
        if (!s.active)
        {
            return;
        }

        // the source is the change of what each body holds out of the water since the last tick, a body that
        // just arrived or left adds nothing, otherwise a car spawned in the sea would throw a tsunami
        for (auto it = s.bodies.begin(); it != s.bodies.end();)
        {
            Body& body = it->second;
            if (!body.reported && ++body.idle > stale_ticks)
            {
                it = s.bodies.erase(it);
                continue;
            }
            if (body.reported)
            {
                if (body.has_previous)
                {
                    splat(s, body.current, 1.0f);
                    splat(s, body.previous, -1.0f);
                    push_out_of_hull(s, body.current);
                }
                body.previous     = body.current;
                body.has_previous = true;
                body.reported     = false;
            }
            ++it;
        }

        update_sponge(s);

        s.accumulator = min(s.accumulator + max(dt, 0.0f), step_dt * steps_max);
        float peak    = 0.0f;
        bool first    = true;
        while (s.accumulator >= step_dt)
        {
            s.accumulator -= step_dt;
            peak           = max(peak, step(s, first));
            first          = false;
        }

        // a calm sea with nothing in it stops simulating and uploading
        s.quiet_time = (s.bodies.empty() && peak < 0.002f) ? s.quiet_time + dt : 0.0f;
        if (s.quiet_time > 2.0f)
        {
            clear_locked(s);
        }
    }

    float OceanWake::SampleHeight(float world_x, float world_z)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        return sample_locked(s, world_x, world_z);
    }

    void OceanWake::SampleHeights(const float* xz, float* heights, uint32_t count)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        for (uint32_t i = 0; i < count; i++)
        {
            heights[i] = sample_locked(s, xz[i * 2 + 0], xz[i * 2 + 1]);
        }
    }

    bool OceanWake::CollectUpload(vector<uint16_t>& texels)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (s.height.empty() || (!s.active && !s.upload_zero))
        {
            return false;
        }

        const size_t count = static_cast<size_t>(res) * res;
        texels.resize(count * 2);
        for (size_t i = 0; i < count; i++)
        {
            texels[i * 2 + 0] = vertex_pack::float_to_half(s.height[i]);
            texels[i * 2 + 1] = vertex_pack::float_to_half(s.foam[i]);
        }
        s.upload_zero = false;
        return true;
    }

    Vector4 OceanWake::GetMapping()
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        return Vector4(s.origin_x * cell_size, s.origin_z * cell_size, 1.0f / window_size, s.active && s.origin_valid ? 1.0f : 0.0f);
    }
}
