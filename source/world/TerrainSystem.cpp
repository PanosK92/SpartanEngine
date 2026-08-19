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

//= INCLUDES ============================
#include "pch.h"
#include "TerrainSystem.h"
#include "../rhi/RHI_Texture.h"
#include "../core/ThreadPool.h"
#include <random>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <queue>
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // erosion runs in cell units, one unit of height equals one cell of horizontal spacing,
        // which keeps every constant below independent of terrain density and scale
        struct erosion_settings
        {
            // hydraulic, droplet model after musgrave 1989, mei 2007 and beyer 2015
            float droplets_per_cell   = 2.0f;
            uint32_t droplet_lifetime = 48;
            uint32_t erode_radius     = 1;
            float inertia             = 0.10f;
            float capacity_factor     = 0.5f;
            float capacity_min        = 0.0005f;
            float erode_rate          = 0.30f;
            float deposit_rate        = 0.30f;
            float evaporation         = 0.03f;
            float gravity             = 2.0f;
            float speed_max           = 3.0f;
            float drag                = 0.05f;

            // thermal, talus angle of repose, steep because one cell already averages a wide
            // patch of ground and real cliffs must survive
            float talus_angle_deg  = 50.0f;
            float thermal_rate     = 0.4f;
            uint32_t thermal_steps = 6;

            // differential erosion, soft and hard rock plus altitude banded strata
            float hardness_amount    = 0.5f;
            float hardness_frequency = 5.0f;
            float strata_amount      = 0.2f;
            float strata_period      = 2.5f;

            // a source height map is usually already an eroded landform, so the solver is only
            // allowed to deviate from it by this fraction of the terrain's vertical relief
            float budget_relief = 0.03f;

            // erosion can only sharpen detail that already exists, on a smooth or quantised
            // height map it just blurs, so seed fine relief for it to organise into rills
            float detail_relief          = 0.012f;
            float detail_wavelength_cell = 8.0f;
            float deterrace_strength     = 0.5f;

            // rain does work on slopes, a flat plain stays a flat plain
            float slope_reference = 0.25f;

            // dry land is guaranteed to stay this far above the water
            float freeboard_cell = 0.05f;

            // hydraulic and thermal are interleaved so valleys and screes co-evolve
            uint32_t passes = 3;
        };

        struct erosion_brush
        {
            vector<int32_t> offset_x;
            vector<int32_t> offset_z;
            vector<float> weight;
        };

        // a radial falloff brush, eroding through it instead of a single cell is what keeps
        // channels smooth, a point sampled erode pits the heightfield
        erosion_brush build_erosion_brush(uint32_t radius)
        {
            erosion_brush brush;
            int32_t r = static_cast<int32_t>(radius);
            float sum = 0.0f;

            for (int32_t z = -r; z <= r; z++)
            {
                for (int32_t x = -r; x <= r; x++)
                {
                    float distance = sqrtf(static_cast<float>(x * x + z * z));
                    if (distance > static_cast<float>(r))
                    {
                        continue;
                    }

                    float w = 1.0f - distance / static_cast<float>(r + 1);
                    brush.offset_x.push_back(x);
                    brush.offset_z.push_back(z);
                    brush.weight.push_back(w);
                    sum += w;
                }
            }

            for (float& w : brush.weight)
            {
                w /= sum;
            }

            return brush;
        }

        uint32_t erosion_hash(uint32_t x, uint32_t z, uint32_t seed)
        {
            uint32_t h = x * 374761393u + z * 668265263u + seed * 362437u;
            h          = (h ^ (h >> 13)) * 1274126177u;
            return h ^ (h >> 16);
        }

        float erosion_value_noise(float x, float z, uint32_t seed)
        {
            float base_x = floorf(x);
            float base_z = floorf(z);
            uint32_t ix  = static_cast<uint32_t>(static_cast<int32_t>(base_x) + 8192);
            uint32_t iz  = static_cast<uint32_t>(static_cast<int32_t>(base_z) + 8192);

            float tx = x - base_x;
            float tz = z - base_z;
            tx       = tx * tx * (3.0f - 2.0f * tx);
            tz       = tz * tz * (3.0f - 2.0f * tz);

            const float to_unit = 1.0f / 4294967295.0f;
            float n00 = static_cast<float>(erosion_hash(ix,     iz,     seed)) * to_unit;
            float n10 = static_cast<float>(erosion_hash(ix + 1, iz,     seed)) * to_unit;
            float n01 = static_cast<float>(erosion_hash(ix,     iz + 1, seed)) * to_unit;
            float n11 = static_cast<float>(erosion_hash(ix + 1, iz + 1, seed)) * to_unit;

            float a = n00 + (n10 - n00) * tx;
            float b = n01 + (n11 - n01) * tx;

            return a + (b - a) * tz;
        }

        float erosion_fbm(float x, float z, uint32_t seed, uint32_t octaves)
        {
            float value     = 0.0f;
            float amplitude = 0.5f;
            float total     = 0.0f;

            for (uint32_t octave = 0; octave < octaves; octave++)
            {
                value     += erosion_value_noise(x, z, seed + octave * 97u) * amplitude;
                total     += amplitude;
                x         *= 2.03f;
                z         *= 2.03f;
                amplitude *= 0.5f;
            }

            return value / total;
        }

        // material erodibility, low frequency noise gives resistant ridges and soft basins
        void build_erodibility(vector<float>& erodibility, uint32_t width, uint32_t depth, const erosion_settings& settings, uint32_t seed)
        {
            float frequency_x = settings.hardness_frequency / static_cast<float>(width);
            float frequency_z = settings.hardness_frequency / static_cast<float>(depth);

            auto fill = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    float x = static_cast<float>(index % width) * frequency_x;
                    float z = static_cast<float>(index / width) * frequency_z;
                    float n = erosion_fbm(x, z, seed, 4) * 2.0f - 1.0f;

                    erodibility[index] = clamp(1.0f + settings.hardness_amount * n, 0.25f, 2.0f);
                }
            };

            ThreadPool::ParallelLoop(fill, width * depth);
        }

        float erosion_smoothstep(float edge0, float edge1, float value)
        {
            float t = clamp((value - edge0) / max(edge1 - edge0, 1e-6f), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        // local slope in cell units, the grid is isotropic so a central difference is enough
        float erosion_slope(const vector<float>& heights, uint32_t width, uint32_t depth, int32_t x, int32_t z)
        {
            int32_t x0 = max(x - 1, 0);
            int32_t x1 = min(x + 1, static_cast<int32_t>(width) - 1);
            int32_t z0 = max(z - 1, 0);
            int32_t z1 = min(z + 1, static_cast<int32_t>(depth) - 1);

            float dx = (heights[static_cast<size_t>(z) * width + x1] - heights[static_cast<size_t>(z) * width + x0]) / static_cast<float>(x1 - x0);
            float dz = (heights[static_cast<size_t>(z1) * width + x] - heights[static_cast<size_t>(z0) * width + x]) / static_cast<float>(z1 - z0);

            return sqrtf(dx * dx + dz * dz);
        }

        // removes the stair steps an eight bit height map bakes in and injects fine relief on
        // slopes, the droplets then organise that relief into gullies instead of smoothing it away
        void seed_detail(
            vector<float>& heights,
            const vector<float>& original,
            vector<float>& scratch,
            uint32_t width,
            uint32_t depth,
            float sea_level,
            float relief,
            const erosion_settings& settings,
            uint32_t seed
        )
        {
            int32_t last_x       = static_cast<int32_t>(width) - 1;
            int32_t last_z       = static_cast<int32_t>(depth) - 1;
            float detail         = settings.detail_relief * relief;
            float frequency      = 1.0f / max(settings.detail_wavelength_cell, 1.0f);
            float shore_fade     = 0.08f * relief;
            float freeboard      = settings.freeboard_cell;

            auto apply = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    scratch[index] = heights[index];

                    // the sea bed is left exactly as it was, noise there would grow new islands
                    if (original[index] <= sea_level)
                    {
                        continue;
                    }

                    int32_t x = static_cast<int32_t>(index % width);
                    int32_t z = static_cast<int32_t>(index / width);
                    if (x < 1 || z < 1 || x >= last_x || z >= last_z)
                    {
                        continue;
                    }

                    float blurred =
                        (heights[index - width - 1] + heights[index - width] * 2.0f + heights[index - width + 1] +
                         heights[index - 1] * 2.0f + heights[index] * 4.0f + heights[index + 1] * 2.0f +
                         heights[index + width - 1] + heights[index + width] * 2.0f + heights[index + width + 1]) * 0.0625f;

                    float h = heights[index] + (blurred - heights[index]) * settings.deterrace_strength;

                    float steep = erosion_smoothstep(0.0f, settings.slope_reference, erosion_slope(heights, width, depth, x, z));
                    float shore = erosion_smoothstep(0.0f, shore_fade, original[index] - sea_level);
                    float n     = erosion_fbm(static_cast<float>(x) * frequency, static_cast<float>(z) * frequency, seed + 31u, 3) * 2.0f - 1.0f;

                    h += n * detail * steep * shore;

                    scratch[index] = max(h, sea_level + freeboard);
                }
            };

            ThreadPool::ParallelLoop(apply, width * depth);

            heights.swap(scratch);
        }

        // cumulative spawn weights so droplets land where there is slope to work with, sampling
        // uniformly instead would spend most of the budget stirring flat ground
        void build_spawn_table(
            vector<float>& table,
            const vector<float>& heights,
            uint32_t width,
            uint32_t depth,
            float sea_level,
            const erosion_settings& settings
        )
        {
            int32_t last_x = static_cast<int32_t>(width) - 3;
            int32_t last_z = static_cast<int32_t>(depth) - 3;

            auto weigh = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    table[index] = 0.0f;

                    int32_t x = static_cast<int32_t>(index % width);
                    int32_t z = static_cast<int32_t>(index / width);
                    if (x < 2 || z < 2 || x >= last_x || z >= last_z || heights[index] <= sea_level)
                    {
                        continue;
                    }

                    float steep  = erosion_smoothstep(0.0f, settings.slope_reference, erosion_slope(heights, width, depth, x, z));
                    table[index] = 0.05f + 0.95f * steep * steep;
                }
            };
            ThreadPool::ParallelLoop(weigh, width * depth);

            double running = 0.0;
            for (uint32_t index = 0; index < width * depth; index++)
            {
                running     += table[index];
                table[index] = static_cast<float>(running);
            }

            float total = table[width * depth - 1];
            if (total <= 0.0f)
            {
                return;
            }

            float inverse = 1.0f / total;
            auto normalize = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    table[index] *= inverse;
                }
            };
            ThreadPool::ParallelLoop(normalize, width * depth);
        }

        // thermal weathering, any slope steeper than the talus angle sheds material downhill,
        // this is what produces cliff faces with scree fans at their base
        void thermal_erosion(
            vector<float>& heights,
            vector<float>& outflow,
            vector<float>& excess,
            vector<float>& scratch,
            uint32_t width,
            uint32_t depth,
            float talus,
            float rate,
            uint32_t steps
        )
        {
            const int32_t neighbor_x[8]  = { -1,  0,  1, -1, 1, -1, 0, 1 };
            const int32_t neighbor_z[8]  = { -1, -1, -1,  0, 0,  1, 1, 1 };
            const float neighbor_dist[8] = { 1.4142136f, 1.0f, 1.4142136f, 1.0f, 1.0f, 1.4142136f, 1.0f, 1.4142136f };

            int32_t last_x = static_cast<int32_t>(width) - 1;
            int32_t last_z = static_cast<int32_t>(depth) - 1;

            for (uint32_t step = 0; step < steps; step++)
            {
                // the scatter is expressed as two gathers so it stays lock free
                auto measure = [&](uint32_t start, uint32_t end)
                {
                    for (uint32_t index = start; index < end; index++)
                    {
                        outflow[index] = 0.0f;
                        excess[index]  = 0.0f;

                        int32_t x = static_cast<int32_t>(index % width);
                        int32_t z = static_cast<int32_t>(index / width);
                        if (x < 1 || z < 1 || x >= last_x || z >= last_z)
                        {
                            continue;
                        }

                        float h            = heights[index];
                        float excess_total = 0.0f;
                        float excess_max   = 0.0f;

                        for (uint32_t n = 0; n < 8; n++)
                        {
                            size_t neighbor  = static_cast<size_t>(z + neighbor_z[n]) * width + (x + neighbor_x[n]);
                            float difference = h - heights[neighbor] - talus * neighbor_dist[n];
                            if (difference > 0.0f)
                            {
                                excess_total += difference;
                                excess_max    = max(excess_max, difference);
                            }
                        }

                        excess[index]  = excess_total;
                        outflow[index] = rate * excess_max * 0.5f;
                    }
                };
                ThreadPool::ParallelLoop(measure, width * depth);

                auto redistribute = [&](uint32_t start, uint32_t end)
                {
                    for (uint32_t index = start; index < end; index++)
                    {
                        float h = heights[index] - outflow[index];

                        int32_t x = static_cast<int32_t>(index % width);
                        int32_t z = static_cast<int32_t>(index / width);
                        if (x >= 1 && z >= 1 && x < last_x && z < last_z)
                        {
                            for (uint32_t n = 0; n < 8; n++)
                            {
                                size_t source = static_cast<size_t>(z + neighbor_z[n]) * width + (x + neighbor_x[n]);
                                if (excess[source] <= 0.0f)
                                {
                                    continue;
                                }

                                float difference = heights[source] - heights[index] - talus * neighbor_dist[n];
                                if (difference > 0.0f)
                                {
                                    h += outflow[source] * (difference / excess[source]);
                                }
                            }
                        }

                        scratch[index] = h;
                    }
                };
                ThreadPool::ParallelLoop(redistribute, width * depth);

                heights.swap(scratch);
            }
        }

        // droplet based hydraulic erosion, carves gullies and lays alluvium in the valley floors
        void hydraulic_erosion(
            vector<float>& heights,
            const vector<float>& original,
            const vector<float>& erodibility,
            const vector<float>& spawn_table,
            const erosion_brush& brush,
            uint32_t width,
            uint32_t depth,
            float sea_level,
            float budget,
            uint32_t droplet_count,
            uint32_t seed,
            const erosion_settings& settings
        )
        {
            int32_t last_x     = static_cast<int32_t>(width) - 1;
            int32_t last_z     = static_cast<int32_t>(depth) - 1;
            size_t brush_taps  = brush.weight.size();
            float strata_scale = 6.2831853f / max(settings.strata_period, 0.05f);
            float limit_x      = static_cast<float>(width) - 3.0f;
            float limit_z      = static_cast<float>(depth) - 3.0f;
            float freeboard    = settings.freeboard_cell;
            float inv_budget   = 1.0f / max(budget, 1e-4f);
            uint32_t cells     = width * depth;

            // droplets run in parallel and write to shared cells, the occasional lost update is
            // far below the noise floor of the simulation and buys a linear speedup
            auto simulate = [&](uint32_t start, uint32_t end)
            {
                mt19937 rng(seed + start * 2654435761u + 1u);
                uniform_real_distribution<float> unit(0.0f, 1.0f);
                uniform_real_distribution<float> spawn_angle(0.0f, 6.2831853f);

                // a land cell may never sink into the sea and a sea cell may never rise out of
                // it, so the coastline the height map describes survives the whole simulation
                auto floor_of = [&](size_t cell) -> float
                {
                    return original[cell] > sea_level
                        ? max(original[cell] - budget, sea_level + freeboard)
                        : -numeric_limits<float>::max();
                };

                auto ceiling_of = [&](size_t cell) -> float
                {
                    return original[cell] > sea_level
                        ? original[cell] + budget
                        : sea_level - freeboard;
                };

                for (uint32_t droplet = start; droplet < end; droplet++)
                {
                    size_t spawn = static_cast<size_t>(
                        lower_bound(spawn_table.begin(), spawn_table.end(), unit(rng)) - spawn_table.begin()
                    );
                    if (spawn >= cells)
                    {
                        spawn = cells - 1;
                    }

                    float position_x = static_cast<float>(spawn % width) + unit(rng);
                    float position_z = static_cast<float>(spawn / width) + unit(rng);
                    position_x       = clamp(position_x, 2.0f, limit_x - 0.001f);
                    position_z       = clamp(position_z, 2.0f, limit_z - 0.001f);

                    float direction_x = 0.0f;
                    float direction_z = 0.0f;
                    float speed       = 1.0f;
                    float water       = 1.0f;
                    float sediment    = 0.0f;

                    int32_t cell_x = 0;
                    int32_t cell_z = 0;

                    // spreads whatever is still carried over the brush footprint
                    auto dump_sediment = [&](float amount)
                    {
                        if (amount <= 0.0f)
                        {
                            return;
                        }

                        for (size_t tap = 0; tap < brush_taps; tap++)
                        {
                            int32_t brush_x = cell_x + brush.offset_x[tap];
                            int32_t brush_z = cell_z + brush.offset_z[tap];
                            if (brush_x < 1 || brush_z < 1 || brush_x >= last_x || brush_z >= last_z)
                            {
                                continue;
                            }

                            size_t cell = static_cast<size_t>(brush_z) * width + brush_x;
                            heights[cell] = min(heights[cell] + amount * brush.weight[tap], ceiling_of(cell));
                        }
                    };

                    for (uint32_t life = 0; life < settings.droplet_lifetime; life++)
                    {
                        cell_x       = static_cast<int32_t>(position_x);
                        cell_z       = static_cast<int32_t>(position_z);
                        float frac_x = position_x - static_cast<float>(cell_x);
                        float frac_z = position_z - static_cast<float>(cell_z);
                        size_t index = static_cast<size_t>(cell_z) * width + cell_x;

                        float h00 = heights[index];
                        float h10 = heights[index + 1];
                        float h01 = heights[index + width];
                        float h11 = heights[index + width + 1];

                        float height_here =
                            h00 * (1.0f - frac_x) * (1.0f - frac_z) +
                            h10 * frac_x * (1.0f - frac_z) +
                            h01 * (1.0f - frac_x) * frac_z +
                            h11 * frac_x * frac_z;

                        // rain does not carve the sea bed, sediment arriving there is gone for good
                        if (height_here <= sea_level)
                        {
                            break;
                        }

                        // analytic gradient of the bilinear patch
                        float gradient_x = (h10 - h00) * (1.0f - frac_z) + (h11 - h01) * frac_z;
                        float gradient_z = (h01 - h00) * (1.0f - frac_x) + (h11 - h10) * frac_x;

                        direction_x = direction_x * settings.inertia - gradient_x * (1.0f - settings.inertia);
                        direction_z = direction_z * settings.inertia - gradient_z * (1.0f - settings.inertia);

                        float length = sqrtf(direction_x * direction_x + direction_z * direction_z);
                        if (length < 1e-6f)
                        {
                            float angle = spawn_angle(rng);
                            direction_x = cosf(angle);
                            direction_z = sinf(angle);
                        }
                        else
                        {
                            direction_x /= length;
                            direction_z /= length;
                        }

                        position_x += direction_x;
                        position_z += direction_z;

                        if (position_x < 2.0f || position_z < 2.0f || position_x >= limit_x || position_z >= limit_z)
                        {
                            break;
                        }

                        int32_t next_x = static_cast<int32_t>(position_x);
                        int32_t next_z = static_cast<int32_t>(position_z);
                        float next_fx  = position_x - static_cast<float>(next_x);
                        float next_fz  = position_z - static_cast<float>(next_z);
                        size_t next    = static_cast<size_t>(next_z) * width + next_x;

                        float n00 = heights[next];
                        float n10 = heights[next + 1];
                        float n01 = heights[next + width];
                        float n11 = heights[next + width + 1];

                        float height_next =
                            n00 * (1.0f - next_fx) * (1.0f - next_fz) +
                            n10 * next_fx * (1.0f - next_fz) +
                            n01 * (1.0f - next_fx) * next_fz +
                            n11 * next_fx * next_fz;

                        float delta    = height_next - height_here;
                        float capacity = max(-delta, settings.capacity_min) * speed * water * settings.capacity_factor;

                        if (delta > 0.0f || sediment > capacity)
                        {
                            // uphill fills the pit exactly, otherwise shed the surplus load
                            float amount = (delta > 0.0f)
                                ? min(delta, sediment)
                                : (sediment - capacity) * settings.deposit_rate;

                            amount    = max(amount, 0.0f);
                            sediment -= amount;

                            float w00 = (1.0f - frac_x) * (1.0f - frac_z);
                            float w10 = frac_x * (1.0f - frac_z);
                            float w01 = (1.0f - frac_x) * frac_z;
                            float w11 = frac_x * frac_z;

                            heights[index]             = min(heights[index]             + amount * w00, ceiling_of(index));
                            heights[index + 1]         = min(heights[index + 1]         + amount * w10, ceiling_of(index + 1));
                            heights[index + width]     = min(heights[index + width]     + amount * w01, ceiling_of(index + width));
                            heights[index + width + 1] = min(heights[index + width + 1] + amount * w11, ceiling_of(index + width + 1));
                        }
                        else
                        {
                            // strata band the rock by altitude, the noise field varies it laterally
                            float strata = 1.0f + settings.strata_amount * sinf(height_here * strata_scale);

                            // erosion fades out as a cell spends its displacement budget, so the
                            // landform the height map describes is never washed away
                            float spent  = fabsf(heights[index] - original[index]) * inv_budget;
                            float left   = clamp(1.0f - spent * spent, 0.0f, 1.0f);

                            float soft   = erodibility[index] * strata * left;
                            float amount = min((capacity - sediment) * settings.erode_rate * soft, -delta);
                            amount       = clamp(amount, 0.0f, max(heights[index] - floor_of(index), 0.0f));

                            sediment += amount;

                            for (size_t tap = 0; tap < brush_taps; tap++)
                            {
                                int32_t brush_x = cell_x + brush.offset_x[tap];
                                int32_t brush_z = cell_z + brush.offset_z[tap];
                                if (brush_x < 1 || brush_z < 1 || brush_x >= last_x || brush_z >= last_z)
                                {
                                    continue;
                                }

                                size_t cell   = static_cast<size_t>(brush_z) * width + brush_x;
                                heights[cell] = max(heights[cell] - amount * brush.weight[tap], floor_of(cell));
                            }
                        }

                        speed = sqrtf(max(0.0f, speed * speed - delta * settings.gravity));
                        speed = min(speed * (1.0f - settings.drag), settings.speed_max);
                        water *= 1.0f - settings.evaporation;

                        if (water < 0.01f)
                        {
                            break;
                        }
                    }

                    // whatever is still in suspension settles, throwing it away would delete mass
                    // from the terrain and grind every mountain down over successive passes
                    dump_sediment(sediment);
                }
            };

            ThreadPool::ParallelLoop(simulate, droplet_count);
        }

        // keeps every cell inside its erosion budget and on the correct side of the shoreline
        void clamp_to_budget(
            vector<float>& heights,
            const vector<float>& original,
            uint32_t width,
            uint32_t depth,
            float sea_level,
            float budget,
            float freeboard
        )
        {
            auto apply = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    if (original[index] > sea_level)
                    {
                        float low  = max(original[index] - budget, sea_level + freeboard);
                        float high = original[index] + budget;
                        heights[index] = clamp(heights[index], low, high);
                    }
                    else
                    {
                        heights[index] = min(heights[index], sea_level - freeboard);
                    }
                }
            };

            ThreadPool::ParallelLoop(apply, width * depth);
        }

        // pulls back cells that stick out past every neighbor, ridges survive because they are
        // never an extremum along their own crest
        void remove_spikes(vector<float>& heights, vector<float>& scratch, uint32_t width, uint32_t depth, float threshold)
        {
            const int32_t neighbor_x[8] = { -1,  0,  1, -1, 1, -1, 0, 1 };
            const int32_t neighbor_z[8] = { -1, -1, -1,  0, 0,  1, 1, 1 };

            int32_t last_x = static_cast<int32_t>(width) - 1;
            int32_t last_z = static_cast<int32_t>(depth) - 1;

            auto filter = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    scratch[index] = heights[index];

                    int32_t x = static_cast<int32_t>(index % width);
                    int32_t z = static_cast<int32_t>(index / width);
                    if (x < 1 || z < 1 || x >= last_x || z >= last_z)
                    {
                        continue;
                    }

                    float h       = heights[index];
                    float sum     = 0.0f;
                    float lowest  = numeric_limits<float>::max();
                    float highest = -numeric_limits<float>::max();

                    for (uint32_t n = 0; n < 8; n++)
                    {
                        float neighbor = heights[static_cast<size_t>(z + neighbor_z[n]) * width + (x + neighbor_x[n])];
                        sum    += neighbor;
                        lowest  = min(lowest, neighbor);
                        highest = max(highest, neighbor);
                    }

                    if (h > highest + threshold || h < lowest - threshold)
                    {
                        float mean     = sum * 0.125f;
                        scratch[index] = mean + (h - mean) * 0.25f;
                    }
                }
            };

            ThreadPool::ParallelLoop(filter, width * depth);

            heights.swap(scratch);
        }

        float brush_falloff_weight(float distance, float radius, float falloff)
        {
            if (radius <= 0.0f || distance >= radius)
            {
                return 0.0f;
            }

            float t = distance / radius;
            float soft = 1.0f - t;
            soft = soft * soft * (3.0f - 2.0f * soft);
            float hard = 1.0f;
            float f = clamp(falloff, 0.0f, 1.0f);
            return lerp(hard, soft, f);
        }
    }

        float TerrainSystem::ComputeSurfaceAreaKm2(const vector<RHI_Vertex_PosTexNorTan>& vertices, const vector<uint32_t>& indices)
        {
            uint32_t triangle_count = static_cast<uint32_t>(indices.size() / 3);
            vector<float> partial_areas(triangle_count);
            
            auto compute_areas = [&](uint32_t start_tri, uint32_t end_tri)
            {
                for (uint32_t t = start_tri; t < end_tri; t++)
                {
                    size_t i        = static_cast<size_t>(t) * 3;
                    const auto& v0  = vertices[indices[i + 0]].pos;
                    const auto& v1  = vertices[indices[i + 1]].pos;
                    const auto& v2  = vertices[indices[i + 2]].pos;
                    
                    float e1x = v1[0] - v0[0], e1y = v1[1] - v0[1], e1z = v1[2] - v0[2];
                    float e2x = v2[0] - v0[0], e2y = v2[1] - v0[1], e2z = v2[2] - v0[2];
                    float cx  = e1y * e2z - e1z * e2y;
                    float cy  = e1z * e2x - e1x * e2z;
                    float cz  = e1x * e2y - e1y * e2x;
                    
                    partial_areas[t] = 0.5f * sqrtf(cx * cx + cy * cy + cz * cz);
                }
            };
            ThreadPool::ParallelLoop(compute_areas, triangle_count);
            
            float area_m2 = 0.0f;
            for (float a : partial_areas)
                area_m2 += a;
            
            return area_m2 / 1'000'000.0f;
        }
        
        void TerrainSystem::GetValuesFromHeightMap(
            vector<float>& height_data_out,
            RHI_Texture* height_texture,
            float min_y,
            float max_y,
            uint32_t smoothing,
            bool create_border
        )
        {
            vector<byte> height_data = height_texture->GetMip(0, 0)->bytes;
            SP_ASSERT(height_data.size() > 0);

            const uint32_t width  = height_texture->GetWidth();
            const uint32_t height = height_texture->GetHeight();

            // map texture bytes to height values
            uint32_t bytes_per_pixel = (height_texture->GetChannelCount() * height_texture->GetBitsPerChannel()) / 8;
            uint32_t pixel_count     = width * height;
            // scanlines can be padded for alignment, so derive the stride instead of assuming it
            size_t row_stride        = height_data.size() / height;
            height_data_out.resize(pixel_count);

            auto map_height = [&](uint32_t start_pixel, uint32_t end_pixel)
            {
                for (uint32_t pixel = start_pixel; pixel < end_pixel; pixel++)
                {
                    uint32_t x = pixel % width;
                    uint32_t y = pixel / width;

                    // the first row of the image is north and north is +z, so rows are read back
                    // to front, taking them in order mirrors the terrain against its height map
                    size_t byte_index = static_cast<size_t>(height - 1 - y) * row_stride + static_cast<size_t>(x) * bytes_per_pixel;

                    float normalized_value = static_cast<float>(height_data[byte_index]) / 255.0f;
                    height_data_out[pixel] = min_y + normalized_value * (max_y - min_y);
                }
            };
            ThreadPool::ParallelLoop(map_height, pixel_count);

            // smooth height map to reduce hard edges
            for (uint32_t iteration = 0; iteration < smoothing; iteration++)
            {
                vector<float> smoothed_height_data(height_data_out.size());
                
                auto smooth_pixel = [&](uint32_t start_idx, uint32_t end_idx)
                {
                    for (uint32_t idx = start_idx; idx < end_idx; idx++)
                    {
                        uint32_t x     = idx % width;
                        uint32_t y     = idx / width;
                        float sum      = height_data_out[idx];
                        uint32_t count = 1;
                        
                        for (int ny = -1; ny <= 1; ++ny)
                        {
                            for (int nx = -1; nx <= 1; ++nx)
                            {
                                if (nx == 0 && ny == 0)
                                {
                                    continue;
                                }
                                
                                int neighbor_x = static_cast<int>(x) + nx;
                                int neighbor_y = static_cast<int>(y) + ny;
                                
                                if (neighbor_x >= 0 && neighbor_x < static_cast<int>(width) && 
                                    neighbor_y >= 0 && neighbor_y < static_cast<int>(height))
                                {
                                    sum += height_data_out[neighbor_y * width + neighbor_x];
                                    count++;
                                }
                            }
                        }
                        
                        smoothed_height_data[idx] = sum / static_cast<float>(count);
                    }
                };
                
                ThreadPool::ParallelLoop(smooth_pixel, width * height);
                height_data_out = move(smoothed_height_data);
            }
        
            // create border mountains to prevent player from leaving the terrain
            if (create_border)
            {
                const uint32_t border_backface_width = 2;
                const uint32_t border_plateau_width  = 25;
                const uint32_t border_blend_width    = 20;
                const float border_height_max        = 150.0f;

                auto apply_border = [&](uint32_t start_index, uint32_t end_index)
                {
                    for (uint32_t index = start_index; index < end_index; index++)
                    {
                        uint32_t x        = index % width;
                        uint32_t y        = index / width;
                        uint32_t min_dist = min({x, width - 1 - x, y, height - 1 - y});
                        
                        if (min_dist < border_backface_width)
                        {
                            height_data_out[index] = min_y;
                        }
                        else if (min_dist < border_backface_width + border_plateau_width)
                        {
                            height_data_out[index] += border_height_max;
                        }
                        else if (min_dist < border_backface_width + border_plateau_width + border_blend_width)
                        {
                            float blend = 1.0f - static_cast<float>(min_dist - (border_backface_width + border_plateau_width)) / static_cast<float>(border_blend_width);
                            height_data_out[index] += blend * border_height_max;
                        }
                    }
                };
                ThreadPool::ParallelLoop(apply_border, width * height);
            }
        }

        void TerrainSystem::DensifyHeightMap(vector<float>& height_data, uint32_t width, uint32_t height, uint32_t density)
        {
            if (density <= 1)
            {
                return;
            }
        
            uint32_t dense_width  = density * (width - 1) + 1;
            uint32_t dense_height = density * (height - 1) + 1;
            vector<float> dense_height_data(dense_width * dense_height);
        
            auto get_height = [&height_data, width, height](uint32_t x, uint32_t y) -> float
            {
                return height_data[min(y, height - 1) * width + min(x, width - 1)];
            };
        
            // bilinear interpolation to increase resolution
            auto compute_dense_pixel = [&](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t index = start_index; index < end_index; index++)
                {
                    uint32_t x = index % dense_width;
                    uint32_t y = index / dense_width;

                    float u      = static_cast<float>(x) / static_cast<float>(density);
                    float v      = static_cast<float>(y) / static_cast<float>(density);
                    uint32_t x0  = static_cast<uint32_t>(floor(u));
                    uint32_t x1  = min(x0 + 1, width - 1);
                    uint32_t y0  = static_cast<uint32_t>(floor(v));
                    uint32_t y1  = min(y0 + 1, height - 1);
                    float dx     = u - static_cast<float>(x0);
                    float dy     = v - static_cast<float>(y0);

                    float h00 = get_height(x0, y0);
                    float h10 = get_height(x1, y0);
                    float h01 = get_height(x0, y1);
                    float h11 = get_height(x1, y1);

                    dense_height_data[y * dense_width + x] = 
                        (1.0f - dx) * (1.0f - dy) * h00 +
                        dx * (1.0f - dy) * h10 +
                        (1.0f - dx) * dy * h01 +
                        dx * dy * h11;
                }
            };
        
            ThreadPool::ParallelLoop(compute_dense_pixel, dense_width * dense_height);
            height_data = move(dense_height_data);
        }

        void TerrainSystem::GeneratePositions(vector<Vector3>& positions, const vector<float>& height_map, uint32_t width, uint32_t height, uint32_t density, uint32_t scale)
        {
            SP_ASSERT_MSG(!height_map.empty(), "height map is empty");
        
            positions.resize(width * height);
        
            uint32_t base_width  = (width - 1) / density + 1;
            uint32_t base_height = (height - 1) / density + 1;
            float extent_x       = static_cast<float>(base_width - 1) * scale;
            float extent_z       = static_cast<float>(base_height - 1) * scale;
            float scale_x        = extent_x / static_cast<float>(width - 1);
            float scale_z        = extent_z / static_cast<float>(height - 1);
            float offset_x       = extent_x / 2.0f;
            float offset_z       = extent_z / 2.0f;
        
            auto generate_position = [&](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t index = start_index; index < end_index; index++)
                {
                    uint32_t x       = index % width;
                    uint32_t y       = index / width;
                    float centered_x = static_cast<float>(x) * scale_x - offset_x;
                    float centered_z = static_cast<float>(y) * scale_z - offset_z;
                    positions[index] = Vector3(centered_x, height_map[index], centered_z);
                }
            };
        
            ThreadPool::ParallelLoop(generate_position, width * height);
        }

        void TerrainSystem::ApplyErosion(vector<Vector3>& positions, uint32_t width, uint32_t height, float level_sea, float intensity, TerrainErosionMaps* maps_out)
        {
            if (maps_out)
            {
                maps_out->wear.clear();
                maps_out->deposition.clear();
            }

            if (positions.size() < static_cast<size_t>(width) * height || width < 16 || height < 16 || intensity <= 0.0f)
            {
                return;
            }

            erosion_settings settings;
            settings.droplets_per_cell *= intensity;
            settings.budget_relief     *= clamp(intensity, 0.25f, 4.0f);

            // horizontal spacing between two samples, heights are normalized by it so the
            // simulation behaves identically at any terrain density or scale
            float cell_size = fabsf(positions[1].x - positions[0].x);
            if (cell_size < 1e-4f)
            {
                cell_size = 1.0f;
            }
            float inv_cell_size = 1.0f / cell_size;

            uint32_t cell_count = width * height;
            vector<float> heights(cell_count);
            vector<float> original(cell_count);
            vector<float> erodibility(cell_count);
            vector<float> spawn_table(cell_count);
            vector<float> scratch_a(cell_count);
            vector<float> scratch_b(cell_count);
            vector<float> scratch_c(cell_count);

            auto to_cell_units = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    heights[index]  = positions[index].y * inv_cell_size;
                    original[index] = heights[index];
                }
            };
            ThreadPool::ParallelLoop(to_cell_units, cell_count);

            float lowest  = numeric_limits<float>::max();
            float highest = -numeric_limits<float>::max();
            for (uint32_t index = 0; index < cell_count; index++)
            {
                lowest  = min(lowest, original[index]);
                highest = max(highest, original[index]);
            }

            float relief = max(highest - lowest, 1e-3f);
            float budget = settings.budget_relief * relief;
            float sea    = level_sea * inv_cell_size;
            uint32_t seed = width * 3000017u + height * 41u + 11111u;

            seed_detail(heights, original, scratch_a, width, height, sea, relief, settings, seed);
            build_erodibility(erodibility, width, height, settings, seed);
            build_spawn_table(spawn_table, heights, width, height, sea, settings);

            // reference taken after the detail seed so the exported delta is erosion only, the
            // clamp and hydraulic solvers keep using original as their budget reference
            vector<float> pre_erosion;
            if (maps_out)
            {
                pre_erosion = heights;
            }

            erosion_brush brush = build_erosion_brush(settings.erode_radius);

            float talus             = tanf(settings.talus_angle_deg * 0.0174532925f);
            uint32_t passes         = max(settings.passes, 1u);
            uint64_t droplet_total  = static_cast<uint64_t>(static_cast<float>(cell_count) * settings.droplets_per_cell);
            uint32_t droplets_pass  = static_cast<uint32_t>(max<uint64_t>(droplet_total / passes, 1ull));
            uint32_t thermal_pass   = max(settings.thermal_steps / passes, 1u);

            // rivers and screes shape each other, so alternate the two solvers
            for (uint32_t pass = 0; pass < passes; pass++)
            {
                hydraulic_erosion(
                    heights,
                    original,
                    erodibility,
                    spawn_table,
                    brush,
                    width,
                    height,
                    sea,
                    budget,
                    droplets_pass,
                    seed + pass * 7919u,
                    settings
                );

                thermal_erosion(
                    heights,
                    scratch_a,
                    scratch_b,
                    scratch_c,
                    width,
                    height,
                    talus,
                    settings.thermal_rate,
                    thermal_pass
                );

                clamp_to_budget(heights, original, width, height, sea, budget, settings.freeboard_cell);
            }

            remove_spikes(heights, scratch_a, width, height, 0.35f);
            clamp_to_budget(heights, original, width, height, sea, budget, settings.freeboard_cell);

            auto to_world_units = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    positions[index].y = heights[index] * cell_size;
                }
            };
            ThreadPool::ParallelLoop(to_world_units, cell_count);

            // split the signed delta into the two signals texturing actually wants, bedrock that
            // the water scoured clean and sediment that came to rest on top of it
            if (maps_out)
            {
                maps_out->wear.resize(cell_count);
                maps_out->deposition.resize(cell_count);

                auto split_delta = [&](uint32_t start, uint32_t end)
                {
                    for (uint32_t index = start; index < end; index++)
                    {
                        const float delta              = (heights[index] - pre_erosion[index]) * cell_size;
                        maps_out->wear[index]          = max(-delta, 0.0f);
                        maps_out->deposition[index]    = max(delta, 0.0f);
                    }
                };
                ThreadPool::ParallelLoop(split_delta, cell_count);
            }
        }

        void TerrainSystem::GenerateVerticesAndIndices(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const vector<Vector3>& positions, uint32_t width, uint32_t height)
        {
            SP_ASSERT_MSG(!positions.empty(), "positions are empty");

            const float inv_width  = 1.0f / static_cast<float>(width - 1);
            const float inv_height = 1.0f / static_cast<float>(height - 1);
            
            auto gen_vertices = [&](uint32_t start_idx, uint32_t end_idx)
            {
                for (uint32_t idx = start_idx; idx < end_idx; idx++)
                {
                    uint32_t x   = idx % width;
                    uint32_t y   = idx / width;
                    vertices[idx] = RHI_Vertex_PosTexNorTan(positions[idx], Vector2(x * inv_width, y * inv_height));
                }
            };
            ThreadPool::ParallelLoop(gen_vertices, width * height);
            
            uint32_t quad_count = (width - 1) * (height - 1);
            auto gen_indices = [&](uint32_t start_quad, uint32_t end_quad)
            {
                for (uint32_t quad = start_quad; quad < end_quad; quad++)
                {
                    uint32_t x  = quad % (width - 1);
                    uint32_t y  = quad / (width - 1);
                    uint32_t k  = quad * 6;
                    uint32_t bl = y * width + x;
                    uint32_t br = bl + 1;
                    uint32_t tl = bl + width;
                    uint32_t tr = tl + 1;
                    
                    indices[k]     = br;
                    indices[k + 1] = bl;
                    indices[k + 2] = tl;
                    indices[k + 3] = br;
                    indices[k + 4] = tl;
                    indices[k + 5] = tr;
                }
            };
            ThreadPool::ParallelLoop(gen_indices, quad_count);
        }

        void TerrainSystem::GenerateNormals(vector<RHI_Vertex_PosTexNorTan>& vertices, uint32_t width, uint32_t height)
        {
            SP_ASSERT_MSG(!vertices.empty(), "vertices are empty");
            SP_ASSERT_MSG(width >= 2 && height >= 2, "grid is too small");

            // positions are in metres, a unit-grid gradient turns a 25 m cell into a cliff
            float cell_x = 1.0f;
            float cell_z = 1.0f;
            if (vertices.size() > 1)
            {
                cell_x = max(fabsf(vertices[1].pos[0] - vertices[0].pos[0]), 1e-3f);
            }
            if (vertices.size() > width)
            {
                cell_z = max(fabsf(vertices[width].pos[2] - vertices[0].pos[2]), 1e-3f);
            }

            const float inv_span_x = 0.5f / cell_x;
            const float inv_span_z = 0.5f / cell_z;

            auto write_normal = [&vertices](uint32_t vertex_idx, float dh_dx, float dh_dz)
            {
                float nx      = -dh_dx;
                float ny      = 1.0f;
                float nz      = -dh_dz;
                float inv_len = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
                nx *= inv_len;
                ny *= inv_len;
                nz *= inv_len;
                vertices[vertex_idx].set_normal(Vector3(nx, ny, nz));

                float proj      = nx;
                float tx        = 1.0f - nx * proj;
                float ty        = -ny * proj;
                float tz        = -nz * proj;
                float t_inv_len = 1.0f / sqrtf(tx * tx + ty * ty + tz * tz);
                vertices[vertex_idx].set_tangent(Vector3(tx * t_inv_len, ty * t_inv_len, tz * t_inv_len));
            };

            uint32_t interior_count = (width - 2) * (height - 2);
            if (interior_count > 0)
            {
                auto compute_interior = [&](uint32_t start, uint32_t end)
                {
                    for (uint32_t index = start; index < end; index++)
                    {
                        uint32_t interior_width = width - 2;
                        uint32_t i              = (index % interior_width) + 1;
                        uint32_t j              = (index / interior_width) + 1;
                        uint32_t vertex_idx     = j * width + i;

                        float h_left   = vertices[vertex_idx - 1].pos[1];
                        float h_right  = vertices[vertex_idx + 1].pos[1];
                        float h_bottom = vertices[vertex_idx - width].pos[1];
                        float h_top    = vertices[vertex_idx + width].pos[1];

                        write_normal(
                            vertex_idx,
                            (h_right - h_left) * inv_span_x,
                            (h_top - h_bottom) * inv_span_z
                        );
                    }
                };
                ThreadPool::ParallelLoop(compute_interior, interior_count);
            }

            uint32_t edge_count = 2 * width + 2 * (height - 2);
            auto compute_edges = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t edge_idx = start; edge_idx < end; edge_idx++)
                {
                    uint32_t i = 0;
                    uint32_t j = 0;
                    uint32_t perimeter = 2 * width + 2 * (height - 2);

                    if (edge_idx < width)
                    {
                        i = edge_idx;
                        j = 0;
                    }
                    else if (edge_idx < width + height - 1)
                    {
                        i = width - 1;
                        j = edge_idx - width + 1;
                    }
                    else if (edge_idx < 2 * width + height - 2)
                    {
                        i = 2 * width + height - 3 - edge_idx;
                        j = height - 1;
                    }
                    else
                    {
                        i = 0;
                        j = perimeter - edge_idx;
                    }

                    uint32_t index   = j * width + i;
                    uint32_t i_left  = (i > 0) ? i - 1 : i;
                    uint32_t i_right = (i < width - 1) ? i + 1 : i;
                    uint32_t j_bot   = (j > 0) ? j - 1 : j;
                    uint32_t j_top   = (j < height - 1) ? j + 1 : j;

                    float h_left  = vertices[j * width + i_left].pos[1];
                    float h_right = vertices[j * width + i_right].pos[1];
                    float h_bot   = vertices[j_bot * width + i].pos[1];
                    float h_top   = vertices[j_top * width + i].pos[1];

                    float span_x = ((i_right != i_left) ? static_cast<float>(i_right - i_left) : 1.0f) * cell_x;
                    float span_z = ((j_top != j_bot) ? static_cast<float>(j_top - j_bot) : 1.0f) * cell_z;

                    write_normal(
                        index,
                        (h_right - h_left) / span_x,
                        (h_top - h_bot) / span_z
                    );
                }
            };
            ThreadPool::ParallelLoop(compute_edges, edge_count);
        }

        void TerrainSystem::ApplyPerlinNoise(vector<Vector3>& positions, uint32_t width, uint32_t height, float level_sea, float amplitude, float frequency, uint32_t octaves, float persistence)
        {
            auto fade = [](float t) -> float { return t * t * t * (t * (t * 6 - 15) + 10); };
            auto lerp = [](float a, float b, float t) -> float { return a + t * (b - a); };
        
            // initialize permutation table and gradients
            vector<uint8_t> permutation(512);
            vector<Vector2> gradients(256);
            
            mt19937 gen(width * 4000037u + height * 53u + 22222u);
            uniform_real_distribution<float> dist(-1.0f, 1.0f);

            for (uint32_t i = 0; i < 256; ++i)
            {
                permutation[i] = static_cast<uint8_t>(i);
                Vector2 grad(dist(gen), dist(gen));
                gradients[i] = grad.Normalized();
            }

            for (uint32_t i = 255; i > 0; --i)
            {
                uniform_int_distribution<uint32_t> swap_dist(0, i);
                swap(permutation[i], permutation[swap_dist(gen)]);
            }
            
            for (uint32_t i = 0; i < 256; ++i)
                permutation[256 + i] = permutation[i];
        
            auto perlin_noise = [&](float x, float z) -> float
            {
                int X = static_cast<int>(floor(x)) & 255;
                int Z = static_cast<int>(floor(z)) & 255;
                x -= floor(x);
                z -= floor(z);
        
                float u = fade(x);
                float v = fade(z);
        
                int aa = permutation[permutation[X] + Z];
                int ab = permutation[permutation[X] + Z + 1];
                int ba = permutation[permutation[X + 1] + Z];
                int bb = permutation[permutation[X + 1] + Z + 1];
        
                float grad00 = gradients[aa & 255].x * x + gradients[aa & 255].y * z;
                float grad10 = gradients[ba & 255].x * (x - 1) + gradients[ba & 255].y * z;
                float grad01 = gradients[ab & 255].x * x + gradients[ab & 255].y * (z - 1);
                float grad11 = gradients[bb & 255].x * (x - 1) + gradients[bb & 255].y * (z - 1);
        
                return lerp(lerp(grad00, grad10, u), lerp(grad01, grad11, u), v);
            };
        
            // noise fades in above the water line, applying it to the sea bed would lift half of
            // it into new islands and applying it to the shore would break the coastline apart
            float shore_fade = max(amplitude * 4.0f, 1e-3f);

            auto apply_noise = [&](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t index = start_index; index < end_index; ++index)
                {
                    float above_water = positions[index].y - level_sea;
                    if (above_water <= 0.0f)
                    {
                        continue;
                    }

                    uint32_t x = index % width;
                    uint32_t z = index / width;

                    float scaled_x          = static_cast<float>(x) * frequency;
                    float scaled_z          = static_cast<float>(z) * frequency;
                    float noise_value       = 0.0f;
                    float current_amplitude = amplitude;
                    float current_frequency = 1.0f;
                    float max_amplitude     = 0.0f;
        
                    for (uint32_t octave = 0; octave < octaves; ++octave)
                    {
                        noise_value       += perlin_noise(scaled_x * current_frequency, scaled_z * current_frequency) * current_amplitude;
                        max_amplitude     += current_amplitude;
                        current_amplitude *= persistence;
                        current_frequency *= 2.0f;
                    }
        
                    float gate = clamp(above_water / shore_fade, 0.0f, 1.0f);
                    gate       = gate * gate * (3.0f - 2.0f * gate);

                    float shifted = positions[index].y + (noise_value / max_amplitude) * amplitude * gate;
                    positions[index].y = max(shifted, level_sea + 0.01f);
                }
            };
        
            ThreadPool::ParallelLoop(apply_noise, width * height);
        }
    

    TerrainGridMapping TerrainSystem::ComputeGridMapping(
        uint32_t dense_width,
        uint32_t dense_height,
        uint32_t density,
        uint32_t scale
    )
    {
        TerrainGridMapping mapping;
        uint32_t dens = max(density, 1u);
        uint32_t base_width  = (dense_width - 1) / dens + 1;
        uint32_t base_height = (dense_height - 1) / dens + 1;
        mapping.extent_x = static_cast<float>(base_width - 1) * static_cast<float>(scale);
        mapping.extent_z = static_cast<float>(base_height - 1) * static_cast<float>(scale);
        mapping.scale_x  = mapping.extent_x / static_cast<float>(dense_width - 1);
        mapping.scale_z  = mapping.extent_z / static_cast<float>(dense_height - 1);
        mapping.offset_x = mapping.extent_x / 2.0f;
        mapping.offset_z = mapping.extent_z / 2.0f;
        return mapping;
    }

    void TerrainSystem::CreateFlatHeightfield(
        vector<float>& height_data_out,
        vector<Vector3>& positions_out,
        uint32_t base_width,
        uint32_t base_height,
        uint32_t density,
        uint32_t scale,
        float sea_level
    )
    {
        density = max(density, 1u);
        base_width  = max(base_width, 2u);
        base_height = max(base_height, 2u);

        height_data_out.assign(base_width * base_height, sea_level);
        DensifyHeightMap(height_data_out, base_width, base_height, density);

        uint32_t dense_width  = density * (base_width - 1) + 1;
        uint32_t dense_height = density * (base_height - 1) + 1;
        GeneratePositions(positions_out, height_data_out, dense_width, dense_height, density, scale);
    }

    void TerrainSystem::SyncHeightDataFromPositions(
        vector<float>& height_data,
        const vector<Vector3>& positions
    )
    {
        height_data.resize(positions.size());
        auto copy = [&](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                height_data[i] = positions[i].y;
            }
        };
        ThreadPool::ParallelLoop(copy, static_cast<uint32_t>(positions.size()));
    }

    float TerrainSystem::SampleHeight(
        const vector<Vector3>& positions,
        uint32_t width,
        uint32_t height,
        float world_x,
        float world_z,
        const TerrainGridMapping& mapping
    )
    {
        if (positions.empty() || width < 2 || height < 2)
        {
            return 0.0f;
        }

        float gx = (world_x + mapping.offset_x) / mapping.scale_x;
        float gz = (world_z + mapping.offset_z) / mapping.scale_z;
        int ix   = clamp(static_cast<int>(floor(gx)), 0, static_cast<int>(width) - 2);
        int iz   = clamp(static_cast<int>(floor(gz)), 0, static_cast<int>(height) - 2);
        float fx = gx - static_cast<float>(ix);
        float fz = gz - static_cast<float>(iz);

        float h00 = positions[static_cast<size_t>(iz) * width + ix].y;
        float h10 = positions[static_cast<size_t>(iz) * width + ix + 1].y;
        float h01 = positions[static_cast<size_t>(iz + 1) * width + ix].y;
        float h11 = positions[static_cast<size_t>(iz + 1) * width + ix + 1].y;

        return (h00 + fx * (h10 - h00)) + fz * ((h01 + fx * (h11 - h01)) - (h00 + fx * (h10 - h00)));
    }

    Vector3 TerrainSystem::SampleNormal(
        const vector<Vector3>& positions,
        uint32_t width,
        uint32_t height,
        float world_x,
        float world_z,
        const TerrainGridMapping& mapping
    )
    {
        if (positions.empty() || width < 2 || height < 2)
        {
            return Vector3::Up;
        }

        const float step_x = max(mapping.scale_x, epsilon);
        const float step_z = max(mapping.scale_z, epsilon);

        const float h_left = SampleHeight(
            positions, width, height, world_x - step_x, world_z, mapping
        );
        const float h_right = SampleHeight(
            positions, width, height, world_x + step_x, world_z, mapping
        );
        const float h_bottom = SampleHeight(
            positions, width, height, world_x, world_z - step_z, mapping
        );
        const float h_top = SampleHeight(
            positions, width, height, world_x, world_z + step_z, mapping
        );

        const float dh_dx = (h_right - h_left) / (2.0f * step_x);
        const float dh_dz = (h_top - h_bottom) / (2.0f * step_z);

        Vector3 normal(-dh_dx, 1.0f, -dh_dz);
        if (normal.LengthSquared() < epsilon)
        {
            return Vector3::Up;
        }

        normal.Normalize();
        return normal;
    }

    bool TerrainSystem::RaycastHeightfield(
        const Ray& ray,
        const vector<Vector3>& positions,
        uint32_t width,
        uint32_t height,
        const TerrainGridMapping& mapping,
        Vector3& hit_out,
        float max_distance
    )
    {
        if (positions.empty() || width < 2 || height < 2)
        {
            return false;
        }

        // camera picking stores a far-plane world point in direction
        Vector3 direction = ray.GetDirection() - ray.GetStart();
        if (direction.LengthSquared() < epsilon)
        {
            direction = ray.GetDirection();
        }
        if (direction.LengthSquared() < epsilon)
        {
            return false;
        }
        direction.Normalize();

        const float step = max(mapping.scale_x, mapping.scale_z) * 0.5f;
        float t = 0.0f;
        bool was_above = true;

        while (t <= max_distance)
        {
            Vector3 p = ray.GetStart() + direction * t;
            float h = SampleHeight(positions, width, height, p.x, p.z, mapping);
            bool above = p.y >= h;

            if (was_above && !above)
            {
                float t0 = max(0.0f, t - step);
                float t1 = t;
                for (int i = 0; i < 8; i++)
                {
                    float tm = (t0 + t1) * 0.5f;
                    Vector3 pm = ray.GetStart() + direction * tm;
                    float hm = SampleHeight(positions, width, height, pm.x, pm.z, mapping);
                    if (pm.y >= hm)
                    {
                        t0 = tm;
                    }
                    else
                    {
                        t1 = tm;
                    }
                }
                hit_out = ray.GetStart() + direction * t1;
                hit_out.y = SampleHeight(positions, width, height, hit_out.x, hit_out.z, mapping);
                return true;
            }

            was_above = above;
            t += step;
        }

        return false;
    }

    void TerrainSystem::ApplyBrush(
        vector<Vector3>& positions,
        vector<float>* height_data,
        uint32_t width,
        uint32_t height,
        const TerrainGridMapping& mapping,
        const Vector3& world_center,
        const TerrainBrush& brush
    )
    {
        if (positions.empty() || width < 2 || height < 2 || brush.radius <= 0.0f)
        {
            return;
        }

        float gx = (world_center.x + mapping.offset_x) / mapping.scale_x;
        float gz = (world_center.z + mapping.offset_z) / mapping.scale_z;
        int radius_cells_x = static_cast<int>(ceil(brush.radius / max(mapping.scale_x, 0.001f))) + 1;
        int radius_cells_z = static_cast<int>(ceil(brush.radius / max(mapping.scale_z, 0.001f))) + 1;
        int cx = static_cast<int>(floor(gx));
        int cz = static_cast<int>(floor(gz));
        int x0 = max(0, cx - radius_cells_x);
        int z0 = max(0, cz - radius_cells_z);
        int x1 = min(static_cast<int>(width) - 1, cx + radius_cells_x);
        int z1 = min(static_cast<int>(height) - 1, cz + radius_cells_z);

        vector<float> smooth_src;
        if (brush.mode == TerrainBrushMode::Smooth)
        {
            smooth_src.resize(positions.size());
            for (size_t i = 0; i < positions.size(); i++)
            {
                smooth_src[i] = positions[i].y;
            }
        }

        for (int z = z0; z <= z1; z++)
        {
            for (int x = x0; x <= x1; x++)
            {
                size_t idx = static_cast<size_t>(z) * width + x;
                Vector3& pos = positions[idx];
                float dx = pos.x - world_center.x;
                float dz = pos.z - world_center.z;
                float dist = sqrtf(dx * dx + dz * dz);
                float weight = brush_falloff_weight(dist, brush.radius, brush.falloff);
                if (weight <= 0.0f)
                {
                    continue;
                }

                float delta = brush.strength * weight;

                if (brush.mode == TerrainBrushMode::Raise)
                {
                    pos.y += delta;
                }
                else if (brush.mode == TerrainBrushMode::Lower)
                {
                    pos.y -= delta;
                }
                else if (brush.mode == TerrainBrushMode::Flatten)
                {
                    pos.y = lerp(pos.y, brush.target_height, clamp(weight * (brush.strength / 10.0f), 0.0f, 1.0f));
                }
                else if (brush.mode == TerrainBrushMode::Smooth)
                {
                    float sum = 0.0f;
                    uint32_t count = 0;
                    for (int nz = -1; nz <= 1; nz++)
                    {
                        for (int nx = -1; nx <= 1; nx++)
                        {
                            int sx = x + nx;
                            int sz = z + nz;
                            if (sx >= 0 && sx < static_cast<int>(width) && sz >= 0 && sz < static_cast<int>(height))
                            {
                                sum += smooth_src[static_cast<size_t>(sz) * width + sx];
                                count++;
                            }
                        }
                    }
                    if (count > 0)
                    {
                        float avg = sum / static_cast<float>(count);
                        pos.y = lerp(pos.y, avg, clamp(weight * (brush.strength / 10.0f), 0.0f, 1.0f));
                    }
                }

                if (height_data && idx < height_data->size())
                {
                    (*height_data)[idx] = pos.y;
                }
            }
        }
    }

    bool TerrainSystem::ApplyHeightEdit(
        vector<Vector3>& positions,
        vector<float>* height_data,
        uint32_t width,
        uint32_t height,
        const TerrainHeightEdit& edit,
        const float* weights,
        uint32_t x0,
        uint32_t z0,
        uint32_t x1,
        uint32_t z1
    )
    {
        if (positions.empty() || width < 2 || height < 2)
        {
            return false;
        }

        const uint32_t x_end = min(x1, width);
        const uint32_t z_end = min(z1, height);
        const uint32_t x_start = min(x0, x_end);
        const uint32_t z_start = min(z0, z_end);
        if (x_end <= x_start || z_end <= z_start)
        {
            return false;
        }

        atomic<uint32_t> changed{0};
        auto apply = [&](uint32_t start, uint32_t end)
        {
            for (uint32_t row = start; row < end; row++)
            {
                const uint32_t z = z_start + row;
                for (uint32_t x = x_start; x < x_end; x++)
                {
                    const uint32_t i = z * width + x;
                    if (i >= positions.size())
                    {
                        continue;
                    }

                    float w = weights ? weights[i] : 1.0f;
                    if (w <= 1e-4f)
                    {
                        continue;
                    }

                    w = clamp(w, 0.0f, 1.0f);
                    const float original = positions[i].y;
                    float h = original;

                    if (edit.op == TerrainEditOp::Add)
                    {
                        h = original + edit.amount * w;
                    }
                    else if (edit.op == TerrainEditOp::Set)
                    {
                        h = lerp(original, edit.target, w);
                    }
                    else if (edit.op == TerrainEditOp::LowerTo)
                    {
                        h = min(original, lerp(original, edit.target, w));
                    }
                    else if (edit.op == TerrainEditOp::RaiseTo)
                    {
                        h = max(original, lerp(original, edit.target, w));
                    }

                    if (fabsf(h - original) > 0.01f)
                    {
                        changed.fetch_add(1, memory_order_relaxed);
                    }

                    positions[i].y = h;
                    if (height_data && i < height_data->size())
                    {
                        (*height_data)[i] = h;
                    }
                }
            }
        };
        ThreadPool::ParallelLoop(apply, z_end - z_start);

        return changed.load(memory_order_relaxed) > 0;
    }

    namespace
    {
        void chamfer_distance(
            vector<float>& dist,
            const vector<uint8_t>& seed,
            uint32_t width,
            uint32_t height,
            float cell_x,
            float cell_z
        )
        {
            const uint32_t count = width * height;
            const float inf      = 1.0e8f;
            const float diag     = sqrtf(cell_x * cell_x + cell_z * cell_z);
            dist.assign(count, inf);

            for (uint32_t i = 0; i < count; i++)
            {
                if (seed[i])
                {
                    dist[i] = 0.0f;
                }
            }

            auto relax = [&](uint32_t x, uint32_t z, int nx, int nz, float step)
            {
                int rx = static_cast<int>(x) + nx;
                int rz = static_cast<int>(z) + nz;
                if (rx < 0 || rz < 0 ||
                    rx >= static_cast<int>(width) ||
                    rz >= static_cast<int>(height))
                {
                    return;
                }

                uint32_t src = static_cast<uint32_t>(rz) * width +
                    static_cast<uint32_t>(rx);
                uint32_t dst = z * width + x;
                dist[dst] = min(dist[dst], dist[src] + step);
            };

            for (uint32_t z = 0; z < height; z++)
            {
                for (uint32_t x = 0; x < width; x++)
                {
                    relax(x, z, -1,  0, cell_x);
                    relax(x, z,  0, -1, cell_z);
                    relax(x, z, -1, -1, diag);
                    relax(x, z,  1, -1, diag);
                }
            }

            for (int z = static_cast<int>(height) - 1; z >= 0; z--)
            {
                for (int x = static_cast<int>(width) - 1; x >= 0; x--)
                {
                    uint32_t ux = static_cast<uint32_t>(x);
                    uint32_t uz = static_cast<uint32_t>(z);
                    relax(ux, uz,  1,  0, cell_x);
                    relax(ux, uz,  0,  1, cell_z);
                    relax(ux, uz,  1,  1, diag);
                    relax(ux, uz, -1,  1, diag);
                }
            }
        }

        void flood_ocean_from_border(
            vector<uint8_t>& is_ocean,
            const vector<uint8_t>& is_land,
            uint32_t width,
            uint32_t height
        )
        {
            const uint32_t count = width * height;
            is_ocean.assign(count, 0);
            queue<uint32_t> pending;

            auto try_seed = [&](uint32_t x, uint32_t z)
            {
                uint32_t i = z * width + x;
                if (is_land[i] || is_ocean[i])
                {
                    return;
                }

                is_ocean[i] = 1;
                pending.push(i);
            };

            for (uint32_t x = 0; x < width; x++)
            {
                try_seed(x, 0);
                try_seed(x, height - 1);
            }
            for (uint32_t z = 1; z + 1 < height; z++)
            {
                try_seed(0, z);
                try_seed(width - 1, z);
            }

            const int nx[4] = { 1, -1, 0, 0 };
            const int nz[4] = { 0, 0, 1, -1 };
            while (!pending.empty())
            {
                uint32_t i = pending.front();
                pending.pop();
                uint32_t x = i % width;
                uint32_t z = i / width;
                for (uint32_t n = 0; n < 4; n++)
                {
                    int rx = static_cast<int>(x) + nx[n];
                    int rz = static_cast<int>(z) + nz[n];
                    if (rx < 0 || rz < 0 ||
                        rx >= static_cast<int>(width) ||
                        rz >= static_cast<int>(height))
                    {
                        continue;
                    }

                    uint32_t ni = static_cast<uint32_t>(rz) * width +
                        static_cast<uint32_t>(rx);
                    if (is_land[ni] || is_ocean[ni])
                    {
                        continue;
                    }

                    is_ocean[ni] = 1;
                    pending.push(ni);
                }
            }
        }
    }

    bool TerrainSystem::ApplyCoastalProfile(
        vector<Vector3>& positions,
        vector<float>* height_data,
        uint32_t width,
        uint32_t height,
        const TerrainGridMapping& mapping,
        float sea_level,
        float freeboard,
        float beach_width
    )
    {
        const uint32_t count = width * height;
        if (positions.size() < count || width < 3 || height < 3)
        {
            return false;
        }

        freeboard   = max(freeboard, 0.4f);
        beach_width = max(beach_width, mapping.scale_x * 2.0f);

        const float cell_x = max(mapping.scale_x, 0.001f);
        const float cell_z = max(mapping.scale_z, 0.001f);
        const float wet    = sea_level + 0.05f;
        const float lip    = 0.12f;

        vector<uint8_t> is_land(count, 0);
        for (uint32_t i = 0; i < count; i++)
        {
            is_land[i] = positions[i].y > wet ? 1 : 0;
        }

        vector<uint8_t> is_ocean;
        flood_ocean_from_border(is_ocean, is_land, width, height);

        // interior puddles become land, the coastline itself is not grown
        for (uint32_t i = 0; i < count; i++)
        {
            if (!is_ocean[i])
            {
                is_land[i] = 1;
            }
        }

        vector<float> dist_ocean;
        chamfer_distance(dist_ocean, is_ocean, width, height, cell_x, cell_z);

        atomic<uint32_t> changed{0};
        auto sculpt = [&](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                float original = positions[i].y;
                float h        = original;

                if (is_land[i])
                {
                    float t = clamp(dist_ocean[i] / beach_width, 0.0f, 1.0f);
                    t = t * t * (3.0f - 2.0f * t);
                    float beach_y = lerp(sea_level + lip, sea_level + freeboard, t);
                    h = max(original, beach_y);
                }

                if (abs(h - original) > 0.01f)
                {
                    changed.fetch_add(1, memory_order_relaxed);
                }

                positions[i].y = h;
                if (height_data && i < height_data->size())
                {
                    (*height_data)[i] = h;
                }
            }
        };
        ThreadPool::ParallelLoop(sculpt, count);

        return changed.load(memory_order_relaxed) > 0;
    }

    void TerrainSystem::ApplyIslandShore(
        vector<Vector3>& positions,
        vector<float>* height_data,
        uint32_t width,
        uint32_t height,
        const TerrainGridMapping& mapping,
        float shore_width,
        float edge_height_local
    )
    {
        if (positions.empty() || width < 2 || height < 2 || shore_width <= 0.0f)
        {
            return;
        }

        const float cell_x = max(mapping.scale_x, 0.001f);
        const float cell_z = max(mapping.scale_z, 0.001f);

        auto bend = [&](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                const uint32_t x = i % width;
                const uint32_t z = i / width;

                // meters to nearest map border
                const float dist_x = static_cast<float>(min(x, width - 1 - x)) * cell_x;
                const float dist_z = static_cast<float>(min(z, height - 1 - z)) * cell_z;
                const float dist_edge = min(dist_x, dist_z);

                if (dist_edge >= shore_width)
                {
                    continue;
                }

                // 0 at border, 1 inland past shore_width
                float t = dist_edge / shore_width;
                t = t * t * (3.0f - 2.0f * t);

                positions[i].y = lerp(edge_height_local, positions[i].y, t);

                if (height_data && i < height_data->size())
                {
                    (*height_data)[i] = positions[i].y;
                }
            }
        };
        ThreadPool::ParallelLoop(bend, static_cast<uint32_t>(positions.size()));
    }

    namespace
    {
        // percentile stretch, raw geomorphometry outputs are heavy tailed and unusable without it
        // a plain min/max normalize gets destroyed by a single outlier cell
        void autolevel(vector<float>& values, float percentile_low = 0.02f, float percentile_high = 0.98f)
        {
            if (values.empty())
            {
                return;
            }

            vector<float> sorted = values;
            sort(sorted.begin(), sorted.end());

            const size_t last = sorted.size() - 1;
            const float low   = sorted[static_cast<size_t>(percentile_low * static_cast<float>(last))];
            const float high  = sorted[static_cast<size_t>(percentile_high * static_cast<float>(last))];
            const float range = max(high - low, 1e-6f);

            auto stretch = [&values, low, range](uint32_t start, uint32_t end)
            {
                for (uint32_t i = start; i < end; i++)
                {
                    values[i] = clamp((values[i] - low) / range, 0.0f, 1.0f);
                }
            };
            ThreadPool::ParallelLoop(stretch, static_cast<uint32_t>(values.size()));
        }

        // signed stretch around zero, keeps the sign meaningful and maps zero onto 0.5
        void autolevel_signed(vector<float>& values, float percentile = 0.98f)
        {
            if (values.empty())
            {
                return;
            }

            vector<float> magnitudes(values.size());
            for (size_t i = 0; i < values.size(); i++)
            {
                magnitudes[i] = fabsf(values[i]);
            }
            sort(magnitudes.begin(), magnitudes.end());

            const float scale = max(magnitudes[static_cast<size_t>(percentile * static_cast<float>(magnitudes.size() - 1))], 1e-6f);

            auto stretch = [&values, scale](uint32_t start, uint32_t end)
            {
                for (uint32_t i = start; i < end; i++)
                {
                    values[i] = clamp(values[i] / scale, -1.0f, 1.0f) * 0.5f + 0.5f;
                }
            };
            ThreadPool::ParallelLoop(stretch, static_cast<uint32_t>(values.size()));
        }

        float sample_grid(const vector<float>& grid, uint32_t width, uint32_t height, int32_t x, int32_t z)
        {
            x = clamp(x, 0, static_cast<int32_t>(width) - 1);
            z = clamp(z, 0, static_cast<int32_t>(height) - 1);
            return grid[static_cast<size_t>(z) * width + x];
        }

        float sample_bilinear(const float* grid, uint32_t width, uint32_t height, float x, float z)
        {
            x = clamp(x, 0.0f, static_cast<float>(width) - 1.0f);
            z = clamp(z, 0.0f, static_cast<float>(height) - 1.0f);
            const int32_t x0 = static_cast<int32_t>(floorf(x));
            const int32_t z0 = static_cast<int32_t>(floorf(z));
            const int32_t x1 = min(x0 + 1, static_cast<int32_t>(width) - 1);
            const int32_t z1 = min(z0 + 1, static_cast<int32_t>(height) - 1);
            const float tx = x - static_cast<float>(x0);
            const float tz = z - static_cast<float>(z0);
            const float a = grid[static_cast<size_t>(z0) * width + x0];
            const float b = grid[static_cast<size_t>(z0) * width + x1];
            const float c = grid[static_cast<size_t>(z1) * width + x0];
            const float d = grid[static_cast<size_t>(z1) * width + x1];
            return lerp(lerp(a, b, tx), lerp(c, d, tx), tz);
        }

        Vector3 sample_position_bilinear(
            const vector<Vector3>& positions,
            uint32_t width,
            uint32_t height,
            float x,
            float z
        )
        {
            x = clamp(x, 0.0f, static_cast<float>(width) - 1.0f);
            z = clamp(z, 0.0f, static_cast<float>(height) - 1.0f);
            const int32_t x0 = static_cast<int32_t>(floorf(x));
            const int32_t z0 = static_cast<int32_t>(floorf(z));
            const int32_t x1 = min(x0 + 1, static_cast<int32_t>(width) - 1);
            const int32_t z1 = min(z0 + 1, static_cast<int32_t>(height) - 1);
            const float tx = x - static_cast<float>(x0);
            const float tz = z - static_cast<float>(z0);
            const Vector3 a = positions[static_cast<size_t>(z0) * width + x0];
            const Vector3 b = positions[static_cast<size_t>(z0) * width + x1];
            const Vector3 c = positions[static_cast<size_t>(z1) * width + x0];
            const Vector3 d = positions[static_cast<size_t>(z1) * width + x1];
            return Vector3::Lerp(Vector3::Lerp(a, b, tx), Vector3::Lerp(c, d, tx), tz);
        }

        // box downsample of an arbitrary source grid onto the analysis grid
        void resample_grid(
            vector<float>& destination,
            uint32_t destination_width,
            uint32_t destination_height,
            const vector<float>& source,
            uint32_t source_width,
            uint32_t source_height
        )
        {
            destination.resize(static_cast<size_t>(destination_width) * destination_height);

            const float step_x = static_cast<float>(source_width)  / static_cast<float>(destination_width);
            const float step_z = static_cast<float>(source_height) / static_cast<float>(destination_height);

            auto fill = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    const uint32_t dx = index % destination_width;
                    const uint32_t dz = index / destination_width;

                    const int32_t x0 = static_cast<int32_t>(static_cast<float>(dx)        * step_x);
                    const int32_t x1 = max(static_cast<int32_t>(static_cast<float>(dx + 1) * step_x), x0 + 1);
                    const int32_t z0 = static_cast<int32_t>(static_cast<float>(dz)        * step_z);
                    const int32_t z1 = max(static_cast<int32_t>(static_cast<float>(dz + 1) * step_z), z0 + 1);

                    float sum   = 0.0f;
                    float count = 0.0f;
                    for (int32_t z = z0; z < z1; z++)
                    {
                        for (int32_t x = x0; x < x1; x++)
                        {
                            sum   += sample_grid(source, source_width, source_height, x, z);
                            count += 1.0f;
                        }
                    }

                    destination[index] = sum / max(count, 1.0f);
                }
            };
            ThreadPool::ParallelLoop(fill, destination_width * destination_height);
        }

        // priority flood sink fill, barnes 2014, so flow routing never dead ends in a pit
        // without this every closed depression eats the flow accumulation that should have
        // continued downstream and the river network breaks into disconnected fragments
        void fill_sinks(vector<float>& heights, uint32_t width, uint32_t height, float epsilon_slope)
        {
            const size_t cell_count = static_cast<size_t>(width) * height;
            vector<float> filled(cell_count, numeric_limits<float>::max());
            vector<uint8_t> closed(cell_count, 0);

            struct node
            {
                float value;
                uint32_t index;
                bool operator<(const node& other) const { return value > other.value; } // min heap
            };
            priority_queue<node> open;

            for (uint32_t x = 0; x < width; x++)
            {
                const uint32_t top    = x;
                const uint32_t bottom = (height - 1) * width + x;
                filled[top]           = heights[top];
                filled[bottom]        = heights[bottom];
                open.push({ heights[top], top });
                open.push({ heights[bottom], bottom });
            }
            for (uint32_t z = 1; z < height - 1; z++)
            {
                const uint32_t left  = z * width;
                const uint32_t right = z * width + width - 1;
                filled[left]         = heights[left];
                filled[right]        = heights[right];
                open.push({ heights[left], left });
                open.push({ heights[right], right });
            }

            const int32_t neighbor_x[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
            const int32_t neighbor_z[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };

            while (!open.empty())
            {
                const node current = open.top();
                open.pop();

                if (closed[current.index])
                {
                    continue;
                }
                closed[current.index] = 1;

                const int32_t cx = static_cast<int32_t>(current.index % width);
                const int32_t cz = static_cast<int32_t>(current.index / width);

                for (uint32_t n = 0; n < 8; n++)
                {
                    const int32_t nx = cx + neighbor_x[n];
                    const int32_t nz = cz + neighbor_z[n];
                    if (nx < 0 || nz < 0 || nx >= static_cast<int32_t>(width) || nz >= static_cast<int32_t>(height))
                    {
                        continue;
                    }

                    const uint32_t neighbor_index = static_cast<uint32_t>(nz) * width + static_cast<uint32_t>(nx);
                    if (closed[neighbor_index])
                    {
                        continue;
                    }

                    // the epsilon keeps a gradient across the filled surface so routing still drains
                    const float raised   = max(heights[neighbor_index], filled[current.index] + epsilon_slope);
                    filled[neighbor_index] = raised;
                    open.push({ raised, neighbor_index });
                }
            }

            for (size_t i = 0; i < cell_count; i++)
            {
                heights[i] = filled[i] == numeric_limits<float>::max() ? heights[i] : filled[i];
            }
        }

        // multiple flow direction accumulation, freeman 1991, weights proportional to (tan beta)^p
        // single direction d8 produces one pixel wide rivers that look like scratches, mfd spreads
        // the flow into the broad wet bands that vegetation actually follows
        void accumulate_flow(
            vector<float>& accumulation,
            const vector<float>& heights,
            uint32_t width,
            uint32_t height,
            float cell_size
        )
        {
            const size_t cell_count = static_cast<size_t>(width) * height;
            accumulation.assign(cell_count, 1.0f);

            // process cells from high to low so every donor is resolved before its receivers
            vector<uint32_t> order(cell_count);
            for (uint32_t i = 0; i < cell_count; i++)
            {
                order[i] = i;
            }
            sort(order.begin(), order.end(), [&heights](uint32_t a, uint32_t b) { return heights[a] > heights[b]; });

            const int32_t neighbor_x[8]  = { -1, 0, 1, -1, 1, -1, 0, 1 };
            const int32_t neighbor_z[8]  = { -1, -1, -1, 0, 0, 1, 1, 1 };
            const float   diagonal       = 1.41421356f;
            const float   distances[8]   = { diagonal, 1.0f, diagonal, 1.0f, 1.0f, diagonal, 1.0f, diagonal };
            const float   exponent       = 1.1f;

            float weights[8];
            for (uint32_t o = 0; o < cell_count; o++)
            {
                const uint32_t index = order[o];
                const int32_t cx     = static_cast<int32_t>(index % width);
                const int32_t cz     = static_cast<int32_t>(index / width);
                const float   center = heights[index];

                float weight_total = 0.0f;
                for (uint32_t n = 0; n < 8; n++)
                {
                    weights[n]       = 0.0f;
                    const int32_t nx = cx + neighbor_x[n];
                    const int32_t nz = cz + neighbor_z[n];
                    if (nx < 0 || nz < 0 || nx >= static_cast<int32_t>(width) || nz >= static_cast<int32_t>(height))
                    {
                        continue;
                    }

                    const float drop = center - heights[static_cast<size_t>(nz) * width + static_cast<size_t>(nx)];
                    if (drop <= 0.0f)
                    {
                        continue;
                    }

                    weights[n]    = powf(drop / (distances[n] * cell_size), exponent);
                    weight_total += weights[n];
                }

                if (weight_total <= 0.0f)
                {
                    continue;
                }

                const float share = accumulation[index] / weight_total;
                for (uint32_t n = 0; n < 8; n++)
                {
                    if (weights[n] <= 0.0f)
                    {
                        continue;
                    }

                    const uint32_t neighbor_index = static_cast<uint32_t>(cz + neighbor_z[n]) * width + static_cast<uint32_t>(cx + neighbor_x[n]);
                    accumulation[neighbor_index] += weights[n] * share;
                }
            }
        }

        void compute_flow_wetness(
            vector<float>& flow_out,
            const vector<float>& heights,
            uint32_t width,
            uint32_t height,
            float cell_size
        )
        {
            vector<float> drained = heights;
            fill_sinks(drained, width, height, cell_size * 1e-4f);

            vector<float> accumulation;
            accumulate_flow(accumulation, drained, width, height, cell_size);

            const uint32_t cell_count = width * height;
            flow_out.resize(cell_count);
            auto to_wetness = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    const int32_t x = static_cast<int32_t>(index % width);
                    const int32_t z = static_cast<int32_t>(index / width);

                    const float dx = (sample_grid(heights, width, height, x + 1, z) -
                                      sample_grid(heights, width, height, x - 1, z)) / (2.0f * cell_size);
                    const float dz = (sample_grid(heights, width, height, x, z + 1) -
                                      sample_grid(heights, width, height, x, z - 1)) / (2.0f * cell_size);

                    const float tan_beta = max(sqrtf(dx * dx + dz * dz), 0.01f);
                    const float alpha    = max(accumulation[index], 1.0f) * cell_size;

                    flow_out[index] = logf(alpha / tan_beta);
                }
            };
            ThreadPool::ParallelLoop(to_wetness, cell_count);
            autolevel(flow_out, 0.05f, 0.995f);
        }

        // log drainage area, pinched so only the actual streams survive
        void compute_channel_mask(
            vector<float>& channel_out,
            const vector<float>& heights,
            uint32_t width,
            uint32_t height,
            float cell_size
        )
        {
            vector<float> drained = heights;
            fill_sinks(drained, width, height, cell_size * 1e-4f);

            vector<float> accumulation;
            accumulate_flow(accumulation, drained, width, height, cell_size);

            const uint32_t cell_count = width * height;
            channel_out.resize(cell_count);
            auto to_log = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    channel_out[index] = logf(max(accumulation[index], 1.0f));
                }
            };
            ThreadPool::ParallelLoop(to_log, cell_count);
            autolevel(channel_out, 0.80f, 0.997f);
        }

        // horizon angle in one compass direction, exponential stepping so a handful of samples
        // still reach across the whole map
        float horizon_angle(
            const vector<float>& heights,
            uint32_t width,
            uint32_t height,
            int32_t x,
            int32_t z,
            float direction_x,
            float direction_z,
            float cell_size,
            uint32_t steps
        )
        {
            const float center = heights[static_cast<size_t>(z) * width + x];
            float best         = 0.0f;
            float distance     = 1.0f;

            for (uint32_t step = 0; step < steps; step++)
            {
                const int32_t sx = x + static_cast<int32_t>(direction_x * distance);
                const int32_t sz = z + static_cast<int32_t>(direction_z * distance);
                if (sx < 0 || sz < 0 || sx >= static_cast<int32_t>(width) || sz >= static_cast<int32_t>(height))
                {
                    break;
                }

                const float rise = heights[static_cast<size_t>(sz) * width + sx] - center;
                if (rise > 0.0f)
                {
                    best = max(best, rise / (distance * cell_size));
                }

                distance *= 1.42f;
            }

            return atanf(best);
        }
    }

    void TerrainSystem::ComputeAnalysisMaps(
        TerrainAnalysisMaps& maps_out,
        const vector<Vector3>& positions,
        uint32_t width,
        uint32_t height,
        float level_sea,
        const TerrainErosionMaps* erosion,
        uint32_t resolution_max
    )
    {
        maps_out = TerrainAnalysisMaps();

        const size_t dense_count = static_cast<size_t>(width) * height;
        if (positions.size() < dense_count || width < 16 || height < 16)
        {
            return;
        }

        // the analysis grid is capped, curvature at four meters per texel is still curvature and
        // the horizon marches are what dominate the bake time
        const uint32_t analysis_width  = min(width,  max(resolution_max, 64u));
        const uint32_t analysis_height = min(height, max(resolution_max, 64u));
        const uint32_t cell_count      = analysis_width * analysis_height;

        maps_out.width  = analysis_width;
        maps_out.height = analysis_height;

        // horizontal spacing on the dense grid, scaled up to the analysis grid
        float dense_cell = fabsf(positions[1].x - positions[0].x);
        if (dense_cell < 1e-4f)
        {
            dense_cell = 1.0f;
        }
        const float cell_size = dense_cell * (static_cast<float>(width) / static_cast<float>(analysis_width));

        vector<float> dense_heights(dense_count);
        {
            auto extract = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t i = start; i < end; i++)
                {
                    dense_heights[i] = positions[i].y;
                }
            };
            ThreadPool::ParallelLoop(extract, static_cast<uint32_t>(dense_count));
        }

        vector<float> heights;
        resample_grid(heights, analysis_width, analysis_height, dense_heights, width, height);

        // normalized altitude, kept separate from the rules so the shader can use it for macro tints
        {
            maps_out.height_norm = heights;
            autolevel(maps_out.height_norm, 0.001f, 0.999f);
        }

        // multi scale curvature, laplacian at three octaves so both the gully and the basin register
        {
            maps_out.curvature.resize(cell_count);

            const int32_t radii[3]  = { 1, 4, 16 };
            const float   scales[3] = { 0.5f, 0.32f, 0.18f };

            auto compute = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    const int32_t x = static_cast<int32_t>(index % analysis_width);
                    const int32_t z = static_cast<int32_t>(index / analysis_width);
                    const float center = heights[index];

                    float total = 0.0f;
                    for (uint32_t octave = 0; octave < 3; octave++)
                    {
                        const int32_t r = radii[octave];
                        const float laplacian =
                            sample_grid(heights, analysis_width, analysis_height, x - r, z) +
                            sample_grid(heights, analysis_width, analysis_height, x + r, z) +
                            sample_grid(heights, analysis_width, analysis_height, x, z - r) +
                            sample_grid(heights, analysis_width, analysis_height, x, z + r) -
                            4.0f * center;

                        // divide by the octave span so the octaves are comparable before weighting
                        total += scales[octave] * laplacian / (static_cast<float>(r) * cell_size);
                    }

                    maps_out.curvature[index] = total;
                }
            };
            ThreadPool::ParallelLoop(compute, cell_count);
            autolevel_signed(maps_out.curvature);
        }

        // flow accumulation into a topographic wetness index
        compute_flow_wetness(maps_out.flow, heights, analysis_width, analysis_height, cell_size);

        // sky occlusion and sun path insolation share the horizon marches
        {
            maps_out.occlusion.resize(cell_count);
            maps_out.insolation.resize(cell_count);

            const uint32_t direction_count = 8;
            const uint32_t march_steps     = 24;

            float direction_x[direction_count];
            float direction_z[direction_count];
            for (uint32_t d = 0; d < direction_count; d++)
            {
                const float angle = (static_cast<float>(d) / static_cast<float>(direction_count)) * pi * 2.0f;
                direction_x[d]    = cosf(angle);
                direction_z[d]    = sinf(angle);
            }

            // the sun traces an arc from east to west, sampling it and integrating the visible
            // fraction is what makes north faces read damp and south faces read parched
            const uint32_t sun_samples = 9;

            auto compute = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    const int32_t x = static_cast<int32_t>(index % analysis_width);
                    const int32_t z = static_cast<int32_t>(index / analysis_width);

                    float horizons[direction_count];
                    float open_total = 0.0f;
                    for (uint32_t d = 0; d < direction_count; d++)
                    {
                        horizons[d] = horizon_angle(
                            heights, analysis_width, analysis_height,
                            x, z, direction_x[d], direction_z[d], cell_size, march_steps
                        );
                        open_total += cosf(horizons[d]) * cosf(horizons[d]);
                    }
                    maps_out.occlusion[index] = open_total / static_cast<float>(direction_count);

                    // surface normal from the local gradient, needed for the incidence term
                    const float dx = (sample_grid(heights, analysis_width, analysis_height, x + 1, z) -
                                      sample_grid(heights, analysis_width, analysis_height, x - 1, z)) / (2.0f * cell_size);
                    const float dz = (sample_grid(heights, analysis_width, analysis_height, x, z + 1) -
                                      sample_grid(heights, analysis_width, analysis_height, x, z - 1)) / (2.0f * cell_size);
                    const float normal_length = sqrtf(dx * dx + 1.0f + dz * dz);
                    const float normal_x      = -dx / normal_length;
                    const float normal_y      = 1.0f / normal_length;
                    const float normal_z      = -dz / normal_length;

                    float energy = 0.0f;
                    float energy_max = 0.0f;
                    for (uint32_t s = 0; s < sun_samples; s++)
                    {
                        // east to west arc peaking due south, northern hemisphere summer
                        const float t         = static_cast<float>(s) / static_cast<float>(sun_samples - 1);
                        const float azimuth   = -pi * 0.5f + t * pi;
                        const float elevation = sinf(t * pi) * (pi * 0.42f) + 0.05f;

                        const float sun_x = cosf(elevation) * sinf(azimuth);
                        const float sun_y = sinf(elevation);
                        const float sun_z = cosf(elevation) * cosf(azimuth);

                        const float incidence = max(normal_x * sun_x + normal_y * sun_y + normal_z * sun_z, 0.0f);
                        energy_max += sun_y;
                        if (incidence <= 0.0f)
                        {
                            continue;
                        }

                        // interpolate the horizon between the two marched directions bracketing the azimuth
                        float compass = atan2f(sun_z, sun_x) / (pi * 2.0f);
                        compass       = compass - floorf(compass);
                        const float slot     = compass * static_cast<float>(direction_count);
                        const uint32_t slot0 = static_cast<uint32_t>(slot) % direction_count;
                        const uint32_t slot1 = (slot0 + 1) % direction_count;
                        const float blend    = slot - floorf(slot);
                        const float horizon  = horizons[slot0] * (1.0f - blend) + horizons[slot1] * blend;

                        if (elevation > horizon)
                        {
                            energy += incidence * sun_y;
                        }
                    }

                    maps_out.insolation[index] = energy / max(energy_max, 1e-6f);
                }
            };
            ThreadPool::ParallelLoop(compute, cell_count);
            autolevel(maps_out.occlusion, 0.02f, 0.98f);
            autolevel(maps_out.insolation, 0.02f, 0.98f);
        }

        // talus, ground sitting just under the angle of repose is where scree comes to rest
        // a cliff face is too steep to hold it and flat ground never received it
        {
            maps_out.talus.resize(cell_count);

            const float repose      = 38.0f * deg_to_rad;
            const float band_width  = 14.0f * deg_to_rad;

            auto compute = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    const int32_t x = static_cast<int32_t>(index % analysis_width);
                    const int32_t z = static_cast<int32_t>(index / analysis_width);

                    const float dx = (sample_grid(heights, analysis_width, analysis_height, x + 1, z) -
                                      sample_grid(heights, analysis_width, analysis_height, x - 1, z)) / (2.0f * cell_size);
                    const float dz = (sample_grid(heights, analysis_width, analysis_height, x, z + 1) -
                                      sample_grid(heights, analysis_width, analysis_height, x, z - 1)) / (2.0f * cell_size);
                    const float slope = atanf(sqrtf(dx * dx + dz * dz));

                    // gaussian centred on the repose angle
                    const float offset = (slope - repose) / band_width;
                    float in_band      = expf(-offset * offset);

                    // scree needs a cliff above it to have fallen from, look uphill for one
                    float steepest_above = 0.0f;
                    for (int32_t r = 2; r <= 12; r += 2)
                    {
                        const float up_x = sample_grid(heights, analysis_width, analysis_height, x + r, z) - heights[index];
                        const float up_z = sample_grid(heights, analysis_width, analysis_height, x, z + r) - heights[index];
                        const float dn_x = sample_grid(heights, analysis_width, analysis_height, x - r, z) - heights[index];
                        const float dn_z = sample_grid(heights, analysis_width, analysis_height, x, z - r) - heights[index];
                        const float rise = max(max(up_x, up_z), max(dn_x, dn_z));
                        steepest_above   = max(steepest_above, atanf(rise / (static_cast<float>(r) * cell_size)));
                    }

                    const float cliff_above = clamp((steepest_above - repose) / (0.5f - repose + 0.5f), 0.0f, 1.0f);
                    maps_out.talus[index]   = in_band * (0.35f + 0.65f * cliff_above);
                }
            };
            ThreadPool::ParallelLoop(compute, cell_count);
            autolevel(maps_out.talus, 0.02f, 0.98f);
        }

        // erosion outputs, resampled onto the analysis grid, or a slope derived stand in when the
        // terrain was loaded from a cache written before erosion export existed
        if (erosion && erosion->IsValid(dense_count))
        {
            resample_grid(maps_out.wear,       analysis_width, analysis_height, erosion->wear,       width, height);
            resample_grid(maps_out.deposition, analysis_width, analysis_height, erosion->deposition, width, height);
            // a low percentile at the median would clip half the terrain to zero and stretch the tail,
            // turning both channels into a hard mask instead of the gradient the rules expect
            autolevel(maps_out.wear,       0.05f, 0.99f);
            autolevel(maps_out.deposition, 0.05f, 0.99f);
        }
        else
        {
            maps_out.wear.resize(cell_count);
            maps_out.deposition.resize(cell_count);

            auto approximate = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    // convex and steep reads as scoured, concave and flat reads as filled
                    const float curvature = maps_out.curvature[index] * 2.0f - 1.0f;
                    const float flow      = maps_out.flow[index];
                    maps_out.wear[index]       = clamp(-curvature * 0.5f + flow * 0.5f, 0.0f, 1.0f);
                    maps_out.deposition[index] = clamp(curvature * 0.7f + (1.0f - flow) * 0.3f, 0.0f, 1.0f);
                }
            };
            ThreadPool::ParallelLoop(approximate, cell_count);
        }

        // everything below the water line is sediment by definition, no erosion signal survives there
        {
            auto drown = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    if (heights[index] < level_sea)
                    {
                        maps_out.deposition[index] = 1.0f;
                        maps_out.wear[index]       = 0.0f;
                    }
                }
            };
            ThreadPool::ParallelLoop(drown, cell_count);
        }
    }

    void TerrainSystem::BuildWeightsFromMap(
        vector<float>& weights_out,
        const vector<Vector3>& positions,
        uint32_t width,
        uint32_t height,
        const float* source,
        uint32_t source_width,
        uint32_t source_height,
        const TerrainWeightFromMap& remap
    )
    {
        const uint32_t count = width * height;
        weights_out.assign(count, 0.0f);
        if (!source || source_width < 1 || source_height < 1 || positions.size() < count)
        {
            return;
        }

        const float scale_x = static_cast<float>(source_width)  / static_cast<float>(width);
        const float scale_z = static_cast<float>(source_height) / static_cast<float>(height);
        const float range   = max(remap.value_high - remap.value_low, 1e-4f);

        auto fill = [&](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                const uint32_t x = i % width;
                const uint32_t z = i / width;
                const float fx = (static_cast<float>(x) + 0.5f) * scale_x - 0.5f;
                const float fz = (static_cast<float>(z) + 0.5f) * scale_z - 0.5f;
                const float value = sample_bilinear(source, source_width, source_height, fx, fz);

                float w = clamp((value - remap.value_low) / range, 0.0f, 1.0f);
                w = w * w * (3.0f - 2.0f * w);

                const float h = positions[i].y;
                float height_w = 1.0f;
                if (h < remap.height_min)
                {
                    height_w = remap.height_soft > 1e-4f
                        ? 1.0f - clamp((remap.height_min - h) / remap.height_soft, 0.0f, 1.0f)
                        : 0.0f;
                }
                else if (h > remap.height_max)
                {
                    height_w = remap.height_soft > 1e-4f
                        ? 1.0f - clamp((h - remap.height_max) / remap.height_soft, 0.0f, 1.0f)
                        : 0.0f;
                }
                height_w = height_w * height_w * (3.0f - 2.0f * height_w);

                weights_out[i] = w * height_w;
            }
        };
        ThreadPool::ParallelLoop(fill, count);
    }

    void TerrainSystem::ComputeFlowMap(
        vector<float>& flow_out,
        uint32_t& width_out,
        uint32_t& height_out,
        const vector<Vector3>& positions,
        uint32_t width,
        uint32_t height,
        TerrainFlowSignal signal,
        uint32_t resolution_max
    )
    {
        flow_out.clear();
        width_out  = 0;
        height_out = 0;

        const size_t dense_count = static_cast<size_t>(width) * height;
        if (positions.size() < dense_count || width < 16 || height < 16)
        {
            return;
        }

        const uint32_t analysis_width  = min(width,  max(resolution_max, 64u));
        const uint32_t analysis_height = min(height, max(resolution_max, 64u));

        float dense_cell = fabsf(positions[1].x - positions[0].x);
        if (dense_cell < 1e-4f)
        {
            dense_cell = 1.0f;
        }
        const float cell_size = dense_cell * (static_cast<float>(width) / static_cast<float>(analysis_width));

        vector<float> dense_heights(dense_count);
        auto extract = [&](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                dense_heights[i] = positions[i].y;
            }
        };
        ThreadPool::ParallelLoop(extract, static_cast<uint32_t>(dense_count));

        vector<float> heights;
        resample_grid(heights, analysis_width, analysis_height, dense_heights, width, height);

        if (signal == TerrainFlowSignal::Channel)
        {
            compute_channel_mask(flow_out, heights, analysis_width, analysis_height, cell_size);
        }
        else
        {
            compute_flow_wetness(flow_out, heights, analysis_width, analysis_height, cell_size);
        }

        width_out  = analysis_width;
        height_out = analysis_height;
    }

    bool TerrainSystem::CarveFlowChannels(
        vector<Vector3>& positions,
        vector<float>* height_data,
        uint32_t width,
        uint32_t height,
        const TerrainChannelCarve& params
    )
    {
        vector<float> channel;
        uint32_t channel_width  = 0;
        uint32_t channel_height = 0;
        ComputeFlowMap(
            channel,
            channel_width,
            channel_height,
            positions,
            width,
            height,
            TerrainFlowSignal::Channel
        );

        if (channel.empty() || channel_width == 0 || channel_height == 0)
        {
            return false;
        }

        TerrainWeightFromMap remap;
        remap.value_low   = params.flow_low;
        remap.value_high  = params.flow_high;
        remap.height_min  = params.sea_level - 2.0f;
        remap.height_max  = params.sea_level + params.reach;
        remap.height_soft = max(params.reach * 0.35f, 1.0f);

        vector<float> weights;
        BuildWeightsFromMap(
            weights,
            positions,
            width,
            height,
            channel.data(),
            channel_width,
            channel_height,
            remap
        );

        const float reach = max(params.reach, 1e-3f);
        auto fade_by_reach = [&](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                if (weights[i] <= 0.0f)
                {
                    continue;
                }

                const float above = positions[i].y - params.sea_level;
                float reach_w = 1.0f - clamp(above / reach, 0.0f, 1.0f);
                reach_w = reach_w * reach_w * (3.0f - 2.0f * reach_w);
                weights[i] *= reach_w;
            }
        };
        ThreadPool::ParallelLoop(fade_by_reach, width * height);

        // only open channels the ocean can actually reach, inland bowls stay dry
        {
            const uint32_t count = width * height;
            const float wet = params.sea_level + 0.05f;
            vector<uint8_t> is_land(count, 0);
            for (uint32_t i = 0; i < count; i++)
            {
                is_land[i] = (positions[i].y > wet && weights[i] < 0.15f) ? 1 : 0;
            }

            vector<uint8_t> is_ocean;
            flood_ocean_from_border(is_ocean, is_land, width, height);

            auto keep_draining = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t i = start; i < end; i++)
                {
                    if (!is_ocean[i])
                    {
                        weights[i] = 0.0f;
                    }
                }
            };
            ThreadPool::ParallelLoop(keep_draining, count);
        }

        TerrainHeightEdit edit;
        edit.op     = TerrainEditOp::LowerTo;
        edit.target = params.sea_level - max(params.bed_depth, 0.05f);

        return ApplyHeightEdit(positions, height_data, width, height, edit, weights.data());
    }

    void TerrainSystem::TraceFlowPaths(
        vector<TerrainFlowPath>& paths_out,
        const vector<Vector3>& positions,
        uint32_t width,
        uint32_t height,
        float sea_level,
        uint32_t path_max
    )
    {
        paths_out.clear();

        const size_t dense_count = static_cast<size_t>(width) * height;
        if (positions.size() < dense_count || width < 16 || height < 16 || path_max == 0)
        {
            return;
        }

        const uint32_t analysis_width  = min(width,  1024u);
        const uint32_t analysis_height = min(height, 1024u);
        const uint32_t cell_count      = analysis_width * analysis_height;

        float dense_cell = fabsf(positions[1].x - positions[0].x);
        if (dense_cell < 1e-4f)
        {
            dense_cell = 1.0f;
        }
        const float cell_size = dense_cell * (static_cast<float>(width) / static_cast<float>(analysis_width));

        vector<float> dense_heights(dense_count);
        auto extract = [&](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                dense_heights[i] = positions[i].y;
            }
        };
        ThreadPool::ParallelLoop(extract, static_cast<uint32_t>(dense_count));

        vector<float> heights;
        resample_grid(heights, analysis_width, analysis_height, dense_heights, width, height);

        vector<float> drained = heights;
        fill_sinks(drained, analysis_width, analysis_height, cell_size * 1e-4f);

        vector<float> accumulation;
        accumulate_flow(accumulation, drained, analysis_width, analysis_height, cell_size);

        vector<float> channel;
        compute_channel_mask(channel, heights, analysis_width, analysis_height, cell_size);

        const int32_t neighbor_x[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        const int32_t neighbor_z[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };

        vector<uint32_t> down(cell_count);
        for (uint32_t i = 0; i < cell_count; i++)
        {
            const int32_t cx = static_cast<int32_t>(i % analysis_width);
            const int32_t cz = static_cast<int32_t>(i / analysis_width);
            uint32_t best    = i;
            float best_h     = drained[i];
            for (uint32_t n = 0; n < 8; n++)
            {
                const int32_t nx = cx + neighbor_x[n];
                const int32_t nz = cz + neighbor_z[n];
                if (nx < 0 || nz < 0 ||
                    nx >= static_cast<int32_t>(analysis_width) ||
                    nz >= static_cast<int32_t>(analysis_height))
                {
                    continue;
                }

                const uint32_t ni = static_cast<uint32_t>(nz) * analysis_width + static_cast<uint32_t>(nx);
                if (drained[ni] < best_h)
                {
                    best_h = drained[ni];
                    best   = ni;
                }
            }
            down[i] = best;
        }

        const float channel_on = 0.35f;
        vector<uint32_t> inbound(cell_count, 0);
        for (uint32_t i = 0; i < cell_count; i++)
        {
            if (channel[i] < channel_on)
            {
                continue;
            }

            const uint32_t next = down[i];
            if (next != i && channel[next] >= channel_on)
            {
                inbound[next]++;
            }
        }

        vector<float> channel_acc;
        channel_acc.reserve(cell_count / 8);
        for (uint32_t i = 0; i < cell_count; i++)
        {
            if (channel[i] >= channel_on)
            {
                channel_acc.push_back(accumulation[i]);
            }
        }
        if (channel_acc.size() < 8)
        {
            return;
        }
        sort(channel_acc.begin(), channel_acc.end());
        const float acc_cut = channel_acc[static_cast<size_t>(0.70f * static_cast<float>(channel_acc.size() - 1))];

        vector<uint32_t> seeds;
        for (uint32_t i = 0; i < cell_count; i++)
        {
            if (channel[i] >= channel_on && inbound[i] == 0 && accumulation[i] >= acc_cut)
            {
                seeds.push_back(i);
            }
        }
        sort(seeds.begin(), seeds.end(), [&accumulation](uint32_t a, uint32_t b)
        {
            return accumulation[a] > accumulation[b];
        });

        vector<uint8_t> visited(cell_count, 0);
        const float scale_x = static_cast<float>(width  - 1) / static_cast<float>(analysis_width);
        const float scale_z = static_cast<float>(height - 1) / static_cast<float>(analysis_height);

        for (uint32_t seed : seeds)
        {
            if (visited[seed] || paths_out.size() >= path_max)
            {
                continue;
            }

            vector<uint32_t> cells;
            uint32_t cell  = seed;
            uint32_t guard = 0;
            while (guard++ < cell_count)
            {
                cells.push_back(cell);
                visited[cell] = 1;

                const uint32_t next = down[cell];
                if (next == cell)
                {
                    break;
                }
                if (heights[next] < sea_level)
                {
                    cells.push_back(next);
                    break;
                }
                if (visited[next])
                {
                    cells.push_back(next);
                    break;
                }
                if (channel[next] < 0.20f && heights[next] > sea_level + 1.5f)
                {
                    break;
                }
                cell = next;
            }

            if (cells.size() < 8)
            {
                continue;
            }

            TerrainFlowPath path;
            const float acc_a = max(accumulation[cells.front()], 1.0f);
            const float acc_b = max(accumulation[cells.back()], 1.0f);
            path.width_start = clamp(logf(acc_a) * 1.15f, 6.0f, 16.0f);
            path.width_end   = clamp(logf(acc_b) * 1.35f, 10.0f, 24.0f);

            Vector3 previous = Vector3::Zero;
            bool has_previous = false;
            for (size_t i = 0; i < cells.size(); i++)
            {
                const uint32_t index = cells[i];
                const float ax = static_cast<float>(index % analysis_width) + 0.5f;
                const float az = static_cast<float>(index / analysis_width) + 0.5f;
                const Vector3 point = sample_position_bilinear(
                    positions,
                    width,
                    height,
                    ax * scale_x,
                    az * scale_z
                );

                const bool last = (i + 1 == cells.size());
                if (!has_previous || last || Vector3::Distance(previous, point) >= 28.0f)
                {
                    path.points.push_back(point);
                    previous     = point;
                    has_previous = true;
                }
            }

            if (path.points.size() >= 3)
            {
                paths_out.push_back(move(path));
            }
        }
    }
}
