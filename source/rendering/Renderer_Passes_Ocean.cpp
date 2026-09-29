/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================================
#include "pch.h"
#include "Renderer_Internal.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_Shader.h"
#include "../rhi/RHI_Texture.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Vertex.h"
#include "../core/ThreadPool.h"
#include "../core/Timer.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Water.h"
#include "../world/components/Terrain.h"
#include "OceanShore.h"
//===========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        atomic<shared_ptr<const vector<Vector4>>>
            ocean_heights_cache;
        future<void> ocean_readback_copy_task;
        uint32_t ocean_readback_copy_index =
            numeric_limits<uint32_t>::max();
        uint32_t ocean_readback_written_mask = 0;

        Renderer_Buffer ocean_height_readback_type(
            const uint32_t index
        )
        {
            return static_cast<Renderer_Buffer>(
                static_cast<uint32_t>(
                    Renderer_Buffer::OceanHeightsReadback0
                ) +
                index
            );
        }

    }

    namespace ocean_shore
    {
        namespace
        {
            // mirrors common_ocean_shore.hlsl
            constexpr float g          = 9.81f;
            constexpr float breaker    = 0.78f;
            constexpr float rest_depth = 0.3f;
            constexpr float obliquity  = 0.35f;
            constexpr float swash_span = 0.72f;
            constexpr float runup      = 0.55f;
            constexpr float pi         = 3.14159265f;
            constexpr float two_pi     = 6.2831853f;

            // how far offshore the surf reaches, and the field resolution cap
            constexpr float reach            = 400.0f;
            constexpr uint32_t max_dimension = 2048;

            struct Field
            {
                uint32_t width  = 0;
                uint32_t height = 0;
                float origin_x  = 0.0f; // world xz of texel (0, 0)
                float origin_z  = 0.0f;
                float cell_x    = 1.0f;
                float cell_z    = 1.0f;
                vector<Vector4> texels; // x = signed distance, yz = shoreward direction, w = beach slope
            };

            atomic<shared_ptr<const Field>> field_current;
            shared_ptr<Field> field_pending;
            future<void> field_task;
            shared_ptr<RHI_Texture> texture;
            shared_ptr<RHI_Texture> texture_retired;
            const void* key_terrain = nullptr;
            float key_sea_level     = numeric_limits<float>::max();
            atomic<float> wave_height{0.0f};
            atomic<float> wave_period{9.0f};
            atomic<float> wave_time{0.0f};
            atomic<float> swell_x{1.0f};
            atomic<float> swell_z{0.0f};

            // exact 1d squared euclidean distance transform (felzenszwalb and huttenlocher), spacing in metres
            void distance_1d(const double* f, double* d, int n, double spacing, int* v, double* z)
            {
                constexpr double inf = 1e20;
                auto parabola = [&](int q, int p)
                {
                    double xq = q * spacing;
                    double xp = p * spacing;
                    return ((f[q] + xq * xq) - (f[p] + xp * xp)) / (2.0 * (xq - xp));
                };

                int k = 0;
                v[0]  = 0;
                z[0]  = -inf;
                z[1]  = inf;
                for (int q = 1; q < n; q++)
                {
                    double s = parabola(q, v[k]);
                    while (s <= z[k])
                    {
                        k--;
                        s = parabola(q, v[k]);
                    }
                    k++;
                    v[k]     = q;
                    z[k]     = s;
                    z[k + 1] = inf;
                }

                k = 0;
                for (int q = 0; q < n; q++)
                {
                    while (z[k + 1] < q * spacing)
                    {
                        k++;
                    }
                    double dx = (q - v[k]) * spacing;
                    d[q]      = dx * dx + f[v[k]];
                }
            }

            // metres from every cell to the nearest seed cell
            vector<float> distance_2d(const vector<uint8_t>& seed, uint32_t w, uint32_t h, float cell_x, float cell_z)
            {
                const uint32_t n = max(w, h);
                vector<double> grid(static_cast<size_t>(w) * h);
                vector<double> in(n), out(n), z(n + 1);
                vector<int> v(n);
                for (size_t i = 0; i < grid.size(); i++)
                {
                    grid[i] = seed[i] ? 0.0 : 1e20;
                }

                for (uint32_t row = 0; row < h; row++)
                {
                    distance_1d(&grid[static_cast<size_t>(row) * w], out.data(), w, cell_x, v.data(), z.data());
                    memcpy(&grid[static_cast<size_t>(row) * w], out.data(), w * sizeof(double));
                }

                for (uint32_t column = 0; column < w; column++)
                {
                    for (uint32_t row = 0; row < h; row++)
                    {
                        in[row] = grid[static_cast<size_t>(row) * w + column];
                    }
                    distance_1d(in.data(), out.data(), h, cell_z, v.data(), z.data());
                    for (uint32_t row = 0; row < h; row++)
                    {
                        grid[static_cast<size_t>(row) * w + column] = out[row];
                    }
                }

                vector<float> result(grid.size());
                for (size_t i = 0; i < grid.size(); i++)
                {
                    result[i] = static_cast<float>(sqrt(grid[i]));
                }
                return result;
            }

            // separable gaussian, clamped at the edges
            void blur(vector<float>& grid, uint32_t w, uint32_t h, float sigma)
            {
                const int radius = max(1, static_cast<int>(ceilf(sigma * 3.0f)));
                vector<float> weights(radius * 2 + 1);
                float total = 0.0f;
                for (int i = -radius; i <= radius; i++)
                {
                    weights[i + radius] = expf(-0.5f * (i * i) / (sigma * sigma));
                    total              += weights[i + radius];
                }
                for (float& weight : weights)
                {
                    weight /= total;
                }

                vector<float> temp(grid.size());
                for (uint32_t row = 0; row < h; row++)
                {
                    for (uint32_t column = 0; column < w; column++)
                    {
                        float sum = 0.0f;
                        for (int i = -radius; i <= radius; i++)
                        {
                            int x = clamp(static_cast<int>(column) + i, 0, static_cast<int>(w) - 1);
                            sum  += grid[static_cast<size_t>(row) * w + x] * weights[i + radius];
                        }
                        temp[static_cast<size_t>(row) * w + column] = sum;
                    }
                }
                for (uint32_t row = 0; row < h; row++)
                {
                    for (uint32_t column = 0; column < w; column++)
                    {
                        float sum = 0.0f;
                        for (int i = -radius; i <= radius; i++)
                        {
                            int y = clamp(static_cast<int>(row) + i, 0, static_cast<int>(h) - 1);
                            sum  += temp[static_cast<size_t>(y) * w + column] * weights[i + radius];
                        }
                        grid[static_cast<size_t>(row) * w + column] = sum;
                    }
                }
            }

            void build_field(Field& field, const vector<float>& heights, float sea_level)
            {
                const uint32_t w     = field.width;
                const uint32_t h     = field.height;
                const size_t count   = static_cast<size_t>(w) * h;
                const float cell     = min(field.cell_x, field.cell_z);
                auto index           = [w](uint32_t x, uint32_t y) { return static_cast<size_t>(y) * w + x; };

                // the sea is the water connected to the map border, inland hollows below sea level stay land
                vector<uint8_t> ocean(count, 0);
                {
                    vector<size_t> queue;
                    auto seed = [&](uint32_t x, uint32_t y)
                    {
                        size_t i = index(x, y);
                        if (!ocean[i] && heights[i] <= sea_level)
                        {
                            ocean[i] = 1;
                            queue.push_back(i);
                        }
                    };
                    for (uint32_t x = 0; x < w; x++)
                    {
                        seed(x, 0);
                        seed(x, h - 1);
                    }
                    for (uint32_t y = 0; y < h; y++)
                    {
                        seed(0, y);
                        seed(w - 1, y);
                    }
                    for (size_t head = 0; head < queue.size(); head++)
                    {
                        uint32_t x = static_cast<uint32_t>(queue[head] % w);
                        uint32_t y = static_cast<uint32_t>(queue[head] / w);
                        if (x > 0)     seed(x - 1, y);
                        if (x + 1 < w) seed(x + 1, y);
                        if (y > 0)     seed(x, y - 1);
                        if (y + 1 < h) seed(x, y + 1);
                    }
                }

                vector<uint8_t> land(count);
                for (size_t i = 0; i < count; i++)
                {
                    land[i] = ocean[i] ? 0 : 1;
                }
                vector<float> to_land  = distance_2d(land, w, h, field.cell_x, field.cell_z);
                vector<float> to_ocean = distance_2d(ocean, w, h, field.cell_x, field.cell_z);

                // signed distance, positive offshore, refined near the coast from the height gradient
                // so the zero crossing follows the interpolated waterline instead of cell centres
                vector<float> distance(count);
                for (uint32_t y = 0; y < h; y++)
                {
                    for (uint32_t x = 0; x < w; x++)
                    {
                        size_t i = index(x, y);
                        float s  = ocean[i] ? to_land[i] - 0.5f * cell : -(to_ocean[i] - 0.5f * cell);
                        if (fabsf(s) < 1.5f * cell)
                        {
                            float gx = (heights[index(min(x + 1, w - 1), y)] - heights[index(x > 0 ? x - 1 : 0, y)]) / (2.0f * field.cell_x);
                            float gz = (heights[index(x, min(y + 1, h - 1))] - heights[index(x, y > 0 ? y - 1 : 0)]) / (2.0f * field.cell_z);
                            float gl = sqrtf(gx * gx + gz * gz);
                            if (gl > 1e-3f)
                            {
                                s = clamp((sea_level - heights[i]) / gl, -1.5f * cell, 1.5f * cell);
                            }
                        }
                        distance[i] = clamp(s, -500.0f, reach * 4.0f);
                    }
                }
                blur(distance, w, h, 1.0f);

                // a softer copy steers the crest lean, so coastline stair steps never reach the waves
                vector<float> steer = distance;
                blur(steer, w, h, 3.0f);

                // beach slope, depth over distance averaged over the nearshore, spread onto the sand as well
                vector<float> slope_sum(count, 0.0f);
                vector<float> slope_weight(count, 0.0f);
                for (size_t i = 0; i < count; i++)
                {
                    if (ocean[i] && distance[i] > cell * 0.5f && distance[i] < 300.0f)
                    {
                        slope_sum[i]    = clamp((sea_level - heights[i]) / distance[i], 0.01f, 0.3f);
                        slope_weight[i] = 1.0f;
                    }
                }
                blur(slope_sum, w, h, 4.0f);
                blur(slope_weight, w, h, 4.0f);

                field.texels.resize(count);
                for (uint32_t y = 0; y < h; y++)
                {
                    for (uint32_t x = 0; x < w; x++)
                    {
                        size_t i = index(x, y);
                        float gx = (steer[index(min(x + 1, w - 1), y)] - steer[index(x > 0 ? x - 1 : 0, y)]) / (2.0f * field.cell_x);
                        float gz = (steer[index(x, min(y + 1, h - 1))] - steer[index(x, y > 0 ? y - 1 : 0)]) / (2.0f * field.cell_z);
                        float gl = sqrtf(gx * gx + gz * gz);
                        float dx = gl > 1e-4f ? -gx / gl : 0.0f;
                        float dz = gl > 1e-4f ? -gz / gl : 0.0f;
                        float m  = slope_weight[i] > 1e-3f ? slope_sum[i] / slope_weight[i] : 0.05f;
                        field.texels[i] = Vector4(distance[i], dx, dz, clamp(m, 0.01f, 0.3f));
                    }
                }
            }

            void upload(const shared_ptr<Field>& field)
            {
                const size_t count = field->texels.size();
                vector<RHI_Texture_Slice> slices(1);
                slices[0].mips.resize(1);
                slices[0].mips[0].bytes.resize(count * 4 * sizeof(uint16_t));
                uint16_t* halves = reinterpret_cast<uint16_t*>(slices[0].mips[0].bytes.data());
                for (size_t i = 0; i < count; i++)
                {
                    const Vector4& t  = field->texels[i];
                    halves[i * 4 + 0] = vertex_pack::float_to_half(t.x);
                    halves[i * 4 + 1] = vertex_pack::float_to_half(t.y);
                    halves[i * 4 + 2] = vertex_pack::float_to_half(t.z);
                    halves[i * 4 + 3] = vertex_pack::float_to_half(t.w);
                }

                texture_retired = texture;
                texture = make_shared<RHI_Texture>(
                    RHI_Texture_Type::Type2D,
                    field->width, field->height, 1, 1,
                    RHI_Format::R16G16B16A16_Float, RHI_Texture_Srv,
                    "ocean_shore", move(slices)
                );
                texture->PrepareForGpu();
                field_current.store(static_pointer_cast<const Field>(field), memory_order_release);
            }

            uint32_t hash_u(uint32_t x)
            {
                x ^= x >> 16;
                x *= 0x7feb352du;
                x ^= x >> 15;
                x *= 0x846ca68bu;
                x ^= x >> 16;
                return x;
            }

            float hash(int n)
            {
                return (hash_u(static_cast<uint32_t>(n)) & 0x00ffffffu) / 16777216.0f;
            }

            float hash(int cx, int cz, int salt)
            {
                uint32_t key = static_cast<uint32_t>(cx) * 0x9e3779b1u ^ static_cast<uint32_t>(cz) * 0x85ebca77u ^ static_cast<uint32_t>(salt) * 0xc2b2ae3du;
                return (hash_u(key) & 0x00ffffffu) / 16777216.0f;
            }

            float noise(float px, float pz, int salt)
            {
                float ix = floorf(px);
                float iz = floorf(pz);
                float fx = px - ix;
                float fz = pz - iz;
                fx       = fx * fx * (3.0f - 2.0f * fx);
                fz       = fz * fz * (3.0f - 2.0f * fz);
                int cx   = static_cast<int>(ix);
                int cz   = static_cast<int>(iz);
                float a  = hash(cx, cz, salt);
                float b  = hash(cx + 1, cz, salt);
                float d  = hash(cx, cz + 1, salt);
                float e  = hash(cx + 1, cz + 1, salt);
                return lerp(lerp(a, b, fx), lerp(d, e, fx), fz);
            }

            float group(float n)
            {
                float set = 0.5f + 0.5f * cosf(n * (two_pi / 7.0f));
                return lerp(0.55f, 1.45f, set * 0.6f + hash(static_cast<int>(n)) * 0.4f);
            }

            float travel_time(float distance, float slope, float c_deep)
            {
                float s      = max(distance, 0.0f);
                float s_deep = max((c_deep * c_deep / g - rest_depth) / slope, 0.0f);
                float k      = 2.0f / (slope * sqrtf(g));
                return k * (sqrtf(slope * min(s, s_deep) + rest_depth) - sqrtf(rest_depth)) + max(s - s_deep, 0.0f) / c_deep;
            }

            float limit(float height, float depth)
            {
                float cap   = breaker * max(depth, 0.0f);
                float ratio = height / max(cap, 1e-4f);
                return height / sqrtf(sqrtf(1.0f + ratio * ratio * ratio * ratio));
            }

            float smoothstep(float edge0, float edge1, float x)
            {
                float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            }

            // bilinear with clamped addressing and texel centres, like the gpu sampler
            Vector4 sample(const Field& field, float x, float z)
            {
                float fx = clamp((x - field.origin_x) / field.cell_x, 0.0f, static_cast<float>(field.width - 1));
                float fz = clamp((z - field.origin_z) / field.cell_z, 0.0f, static_cast<float>(field.height - 1));
                uint32_t x0 = min(static_cast<uint32_t>(fx), field.width - 2);
                uint32_t z0 = min(static_cast<uint32_t>(fz), field.height - 2);
                float tx = fx - x0;
                float tz = fz - z0;
                const Vector4& a = field.texels[static_cast<size_t>(z0) * field.width + x0];
                const Vector4& b = field.texels[static_cast<size_t>(z0) * field.width + x0 + 1];
                const Vector4& c = field.texels[static_cast<size_t>(z0 + 1) * field.width + x0];
                const Vector4& d = field.texels[static_cast<size_t>(z0 + 1) * field.width + x0 + 1];
                return (a * (1.0f - tx) + b * tx) * (1.0f - tz) + (c * (1.0f - tx) + d * tx) * tz;
            }
        }

        void tick(const Water* water)
        {
            if (field_task.valid() && field_task.wait_for(chrono::seconds(0)) == future_status::ready)
            {
                field_task.get();
                if (field_pending && !field_pending->texels.empty())
                {
                    upload(field_pending);
                }
                field_pending.reset();
            }

            // bigger swell arrives with a longer period
            const float surf   = water->GetSurfSize();
            const float period = 6.0f + 3.0f * sqrtf(max(surf, 0.0f));
            wave_height.store(surf);
            wave_period.store(period);
            wave_time.store(static_cast<float>(fmod(Timer::GetTimeSec(), static_cast<double>(period) * 2048.0)));

            const Vector3 wind = World::GetWind();
            const float wind_length = sqrtf(wind.x * wind.x + wind.z * wind.z);
            swell_x.store(wind_length > 1e-3f ? wind.x / wind_length : 1.0f);
            swell_z.store(wind_length > 1e-3f ? wind.z / wind_length : 0.0f);

            Terrain* terrain = Terrain::FindActive();
            if (!terrain || !terrain->HasHeightfield() || !terrain->GetHeightMapGpu() || !terrain->GetEntity() || field_task.valid())
            {
                return;
            }

            const float sea_level = water->GetSeaLevel();
            if (terrain->GetHeightMapGpu() == key_terrain && sea_level == key_sea_level)
            {
                return;
            }
            key_terrain   = terrain->GetHeightMapGpu();
            key_sea_level = sea_level;

            // copy a downsampled heightfield here, the worker must not touch the terrain while it can regenerate
            const vector<Vector3>& positions = terrain->GetPositions();
            const uint32_t dense_w = terrain->GetDenseWidth();
            const uint32_t dense_h = terrain->GetDenseHeight();
            const uint32_t step    = max(1u, (max(dense_w, dense_h) + max_dimension - 1) / max_dimension);
            const Vector3 offset   = terrain->GetEntity()->GetMatrix().GetTranslation();

            shared_ptr<Field> field = make_shared<Field>();
            field->width    = (dense_w - 1) / step + 1;
            field->height   = (dense_h - 1) / step + 1;
            field->origin_x = positions[0].x + offset.x;
            field->origin_z = positions[0].z + offset.z;
            field->cell_x   = (positions[dense_w - 1].x - positions[0].x) / static_cast<float>(dense_w - 1) * step;
            field->cell_z   = (positions[static_cast<size_t>(dense_h - 1) * dense_w].z - positions[0].z) / static_cast<float>(dense_h - 1) * step;
            if (field->width < 3 || field->height < 3 || field->cell_x <= 0.0f || field->cell_z <= 0.0f)
            {
                return;
            }

            vector<float> heights(static_cast<size_t>(field->width) * field->height);
            for (uint32_t y = 0; y < field->height; y++)
            {
                for (uint32_t x = 0; x < field->width; x++)
                {
                    heights[static_cast<size_t>(y) * field->width + x] = positions[static_cast<size_t>(y * step) * dense_w + x * step].y + offset.y;
                }
            }

            field_pending = field;
            field_task    = ThreadPool::AddTask([field, heights = move(heights), sea_level]()
            {
                build_field(*field, heights, sea_level);
            });
        }

        void reset()
        {
            if (field_task.valid())
            {
                field_task.wait();
                field_task = future<void>();
            }
            field_pending.reset();
            field_current.store(shared_ptr<const Field>(), memory_order_release);
            texture_retired = texture;
            texture.reset();
            key_terrain   = nullptr;
            key_sea_level = numeric_limits<float>::max();
        }

        void shutdown()
        {
            reset();
            texture_retired.reset();
        }

        RHI_Texture* get_texture()
        {
            return texture && texture->GetResourceState() == ResourceState::PreparedForGpu ? texture.get() : nullptr;
        }

        Vector4 get_mapping()
        {
            shared_ptr<const Field> field = field_current.load(memory_order_acquire);
            if (!field)
            {
                return Vector4::Zero;
            }
            return Vector4(
                field->origin_x - 0.5f * field->cell_x,
                field->origin_z - 0.5f * field->cell_z,
                1.0f / (field->width * field->cell_x),
                1.0f / (field->height * field->cell_z)
            );
        }

        Vector4 get_wave()
        {
            const bool enabled = get_texture() && wave_height.load() > 0.0f;
            return Vector4(wave_height.load(), wave_period.load(), wave_time.load(), enabled ? 1.0f : 0.0f);
        }

        Vector4 get_swell()
        {
            return Vector4(swell_x.load(), swell_z.load(), reach, 0.0f);
        }

        bool evaluate(float grid_x, float grid_z, float sea_level, Vector3& displacement, float& floor_y)
        {
            displacement = Vector3::Zero;
            floor_y      = -100000.0f;

            shared_ptr<const Field> field = field_current.load(memory_order_acquire);
            const float swell_height      = wave_height.load();
            if (!field || swell_height <= 0.0f)
            {
                return false;
            }

            const float min_x = field->origin_x - 0.5f * field->cell_x;
            const float min_z = field->origin_z - 0.5f * field->cell_z;
            if (grid_x <= min_x || grid_z <= min_z || grid_x >= min_x + field->width * field->cell_x || grid_z >= min_z + field->height * field->cell_z)
            {
                return false;
            }

            const Vector4 texel = sample(*field, grid_x, grid_z);
            const float distance = texel.x;
            if (distance > reach)
            {
                return false;
            }

            Terrain* terrain = Terrain::FindActive();
            float bed        = 0.0f;
            if (!terrain || !terrain->SampleHeight(grid_x, grid_z, bed))
            {
                return false;
            }

            const float sx  = swell_x.load();
            const float sz  = swell_z.load();
            float dir_x     = texel.y;
            float dir_z     = texel.z;
            const float len = sqrtf(dir_x * dir_x + dir_z * dir_z);
            dir_x           = len > 1e-3f ? dir_x / len : sx;
            dir_z           = len > 1e-3f ? dir_z / len : sz;
            const float slope = clamp(texel.w, 0.01f, 0.3f);

            const float period = wave_period.load();
            const float time   = wave_time.load();
            const float c_deep = g * period / two_pi;
            const float depth  = sea_level - bed;
            const float wet_d  = max(depth, 0.0f);

            const float exposure = lerp(0.55f, 1.0f, clamp((dir_x * sx + dir_z * sz) * 0.6f + 0.5f, 0.0f, 1.0f));
            const float fade     = 1.0f - smoothstep(reach * 0.5f, reach, distance);
            const float l_deep   = c_deep * period;
            const float shoaling = 1.0f - smoothstep(0.03f * l_deep, 0.1f * l_deep, wet_d);
            const float offshore = swell_height * exposure * fade * shoaling;

            const float psi    = -obliquity * (grid_x * sx + grid_z * sz) / (c_deep * period) + noise(grid_x / 240.0f, grid_z / 240.0f, 7) * 0.6f;
            const float cycles = (travel_time(distance, slope, c_deep) + time) / period + psi;

            const float shoal     = clamp(powf(8.0f / max(wet_d, 0.05f), 0.25f), 1.0f, 1.7f);
            const float h_loc_raw = offshore * lerp(0.7f, 1.3f, noise(grid_x / 90.0f, grid_z / 90.0f, 3)) * shoal;
            const float h_loc     = limit(h_loc_raw, wet_d);
            const float ratio_loc = h_loc_raw / max(breaker * wet_d, 1e-3f);
            const float peak_loc  = lerp(1.0f, 2.8f, clamp(h_loc_raw / max(wet_d, 0.05f) * 1.1f, 0.0f, 1.0f));
            const float mean_loc  = min(0.5f, 1.0f / sqrtf(pi * peak_loc));

            const float asym       = min(clamp(ratio_loc * 0.6f, 0.0f, 1.0f) * 0.65f + smoothstep(0.9f, 1.4f, ratio_loc) * 0.2f, 0.85f);
            const float half_front = 0.5f * (1.0f - asym);
            const float shifted    = cycles + half_front;
            const float n          = floorf(shifted);
            const float u          = shifted - n - half_front;
            const float un         = u < 0.0f ? u / (1.0f - asym) : u / (1.0f + asym);

            const float h_raw    = offshore * lerp(0.6f, 1.4f, noise(grid_x / 60.0f, grid_z / 60.0f, static_cast<int>(n))) * group(n) * shoal;
            const float height   = limit(h_raw, wet_d);
            const float ratio    = h_raw / max(breaker * wet_d, 1e-3f);
            const float breaking = smoothstep(0.75f, 1.25f, ratio);
            const float peak     = lerp(lerp(1.0f, 2.8f, clamp(h_raw / max(wet_d, 0.05f) * 1.1f, 0.0f, 1.0f)), 1.5f, breaking);
            const float crest    = powf(0.5f + 0.5f * cosf(two_pi * un), peak);

            const float c_local   = min(sqrtf(g * (slope * max(distance, 0.0f) + rest_depth)), c_deep);
            const float face_span = half_front * c_local * period;
            const float push      = min(height * 0.6f * smoothstep(0.6f, 1.1f, ratio), 0.35f * face_span);
            const float crest3    = crest * crest * crest;
            displacement = Vector3(dir_x * push * crest3, height * crest - h_loc * mean_loc, dir_z * push * crest3);

            const float shore_cycles = time / period + psi;
            const float ns           = floorf(shore_cycles);
            const float x            = shore_cycles - ns;
            const float swell_shore  = swell_height * exposure * lerp(0.6f, 1.4f, noise(grid_x / 60.0f, grid_z / 60.0f, static_cast<int>(ns))) * group(ns);
            const float run          = runup * swell_shore;
            const float y            = clamp(x / swash_span, 0.0f, 1.0f);
            const float front        = x < swash_span ? run * sinf(pi * powf(y, 0.75f)) : -1.0f;
            const float excess       = front + depth;
            floor_y                  = excess > -2.0f ? bed + clamp(excess * 0.2f, -0.4f, lerp(0.05f, 0.015f, y)) : -100000.0f;

            return true;
        }
    }

    void Renderer::Pass_Ocean()
    {
        RHI_Texture* tex_displacement_a = GetRenderTarget(
            Renderer_RenderTarget::ocean_displacement
        );
        RHI_Texture* tex_displacement_b = GetRenderTarget(
            Renderer_RenderTarget::ocean_displacement_previous
        );
        RHI_Texture* tex_normal = GetRenderTarget(
            Renderer_RenderTarget::ocean_normal
        );
        RHI_Buffer* buffer_heights = GetBuffer(
            Renderer_Buffer::OceanHeights
        );
        if (
            !tex_displacement_a ||
            !tex_displacement_b ||
            !tex_normal ||
            !buffer_heights
        )
        {
            return;
        }

        const Water* water = m_pass_state.ocean;
        if (!water)
        {
            return;
        }

        ocean_shore::tick(water);

        RHI_Shader* shader_init     = GetShader(Renderer_Shader::ocean_spectrum_init_c);
        RHI_Shader* shader_update   = GetShader(Renderer_Shader::ocean_spectrum_update_c);
        RHI_Shader* shader_fft_h    = GetShader(Renderer_Shader::ocean_fft_horizontal_c);
        RHI_Shader* shader_fft_v    = GetShader(Renderer_Shader::ocean_fft_vertical_c);
        RHI_Shader* shader_assemble = GetShader(Renderer_Shader::ocean_assemble_c);
        const bool shaders_ready =
            shader_init     && shader_init->IsCompiled()     &&
            shader_update   && shader_update->IsCompiled()   &&
            shader_fft_h    && shader_fft_h->IsCompiled()    &&
            shader_fft_v    && shader_fft_v->IsCompiled()    &&
            shader_assemble && shader_assemble->IsCompiled();
        if (!shaders_ready)
        {
            return;
        }

        const uint32_t readback_index =
            m_frame_resource_index;
        bool readback_in_use = false;
        if (readback_index != numeric_limits<uint32_t>::max())
        {
            readback_in_use = ResolveOceanHeightReadback(
                readback_index
            );
        }

        RHI_Texture* tex_spectrum = GetRenderTarget(Renderer_RenderTarget::ocean_spectrum);
        RHI_Texture* tex_fft_a    = GetRenderTarget(Renderer_RenderTarget::ocean_fft_a);
        RHI_Texture* tex_fft_b    = GetRenderTarget(Renderer_RenderTarget::ocean_fft_b);

        const float* lengths = water->GetCascadeLengths();
        const uint32_t n     = renderer_ocean_resolution;
        uint32_t cascades    = water->GetCascadeCount();
        cascades             = cascades < 1 ? 1 : cascades;
        cascades             = cascades > renderer_ocean_max_cascades ? renderer_ocean_max_cascades : cascades;

        // wind comes from the world, re-seed the spectrum whenever it changes
        const Vector3 wind = World::GetWind();
        const float len_xz = sqrtf(
            wind.x * wind.x +
            wind.z * wind.z
        );
        const float wind_speed = len_xz;
        const float dir_x =
            len_xz > 0.0001f ?
            wind.x / len_xz :
            1.0f;
        const float dir_z =
            len_xz > 0.0001f ?
            wind.z / len_xz :
            0.0f;
        if (wind != m_pass_state.ocean_wind)
        {
            m_pass_state.ocean_spectrum_dirty = true;
            m_pass_state.ocean_wind           = wind;
        }

        const bool reset_history =
            m_pass_state.ocean_spectrum_dirty;
        if (reset_history)
        {
            m_pass_state.ocean_history.Reset();
        }

        if (
            !reset_history &&
            m_pass_state.ocean_displacement_produced
        )
        {
            m_pass_state.ocean_history.Advance();
        }
        else
        {
            m_pass_state.ocean_history.valid = false;
        }

        RHI_Texture* tex_displacement = m_pass_state.ocean_history.SelectWrite(
            tex_displacement_a,
            tex_displacement_b
        );

        // shared by every ocean dispatch below
        m_pcb_pass_cpu.set(pass_ocean::wind_direction, Vector2(dir_x, dir_z));
        m_pcb_pass_cpu.set(pass_ocean::wind_speed, wind_speed);
        m_pcb_pass_cpu.set(pass_ocean::reset_history, reset_history);
        m_pcb_pass_cpu.set(pass_ocean::cascade_lengths, Vector4(lengths[0], lengths[1], lengths[2], lengths[3]));
        m_pcb_pass_cpu.set(pass_ocean::amplitude, water->GetAmplitude());
        m_pcb_pass_cpu.set(pass_ocean::choppiness, water->GetChoppiness());
        m_pcb_pass_cpu.set(pass_ocean::displacement_scale, water->GetDisplacementScale());
        m_pcb_pass_cpu.set(pass_ocean::normal_strength, water->GetNormalStrength());

        RHI_CommandList::BeginPass("ocean");
        {
            // spectrum init, only when the parameters changed
            if (m_pass_state.ocean_spectrum_dirty)
            {
                RHI_CommandList::SetShader(shader_init, "ocean_spectrum_init");
                RHI_CommandList::SetTexture(Renderer_BindingsUav::ocean_spectrum, tex_spectrum);
                RHI_CommandList::Dispatch(n / 8, n / 8, cascades);

                m_pass_state.ocean_spectrum_dirty = false;
            }

            // spectrum update, evolves the spectrum to the current time
            {
                RHI_CommandList::SetShader(shader_update, "ocean_spectrum_update");
                RHI_CommandList::SetTexture(Renderer_BindingsUav::ocean_spectrum, tex_spectrum);
                RHI_CommandList::SetTexture(Renderer_BindingsUav::ocean_fft_a, tex_fft_a);
                RHI_CommandList::SetTexture(Renderer_BindingsUav::ocean_fft_b, tex_fft_b);
                RHI_CommandList::Dispatch(n / 8, n / 8, cascades);
            }

            // inverse fft, one dimension at a time, in place on the working textures
            {
                RHI_CommandList::SetShader(shader_fft_h, "ocean_fft_horizontal");
                RHI_CommandList::SetTexture(Renderer_BindingsUav::ocean_fft_a, tex_fft_a);
                RHI_CommandList::SetTexture(Renderer_BindingsUav::ocean_fft_b, tex_fft_b);
                RHI_CommandList::Dispatch(1, n, cascades);
            }
            {
                RHI_CommandList::SetShader(shader_fft_v, "ocean_fft_vertical");
                RHI_CommandList::SetTexture(Renderer_BindingsUav::ocean_fft_a, tex_fft_a);
                RHI_CommandList::SetTexture(Renderer_BindingsUav::ocean_fft_b, tex_fft_b);
                RHI_CommandList::Dispatch(1, n, cascades);
            }

            // assemble displacement, surface slope and foam from the spatial-domain fields
            {
                RHI_Buffer* wave_bounds = GetBuffer(Renderer_Buffer::OceanWaveBounds);
                const uint32_t zero_bounds[renderer_ocean_max_cascades] = {};
                RHI_CommandList::UpdateBuffer(wave_bounds, 0, sizeof(zero_bounds), zero_bounds, false);
                RHI_CommandList::SetShader(shader_assemble, "ocean_assemble");
                RHI_CommandList::SetBuffer("ocean_wave_bounds", wave_bounds);
                RHI_CommandList::SetTexture(Renderer_BindingsUav::ocean_fft_a, tex_fft_a);
                RHI_CommandList::SetTexture(Renderer_BindingsUav::ocean_fft_b, tex_fft_b);
                RHI_CommandList::SetTexture(Renderer_BindingsUav::ocean_displacement, tex_displacement);
                RHI_CommandList::SetTexture(Renderer_BindingsUav::ocean_normal, tex_normal);
                RHI_CommandList::PrepareBufferForCompute(
                    buffer_heights
                );
                RHI_CommandList::SetBuffer(Renderer_BindingsUav::ocean_heights, buffer_heights);
                RHI_CommandList::Dispatch(n / 8, n / 8, cascades);
            }

            RHI_Buffer* buffer_readback =
                readback_index !=
                    numeric_limits<uint32_t>::max() &&
                !readback_in_use ?
                GetBuffer(
                    ocean_height_readback_type(
                        readback_index
                    )
                ) :
                nullptr;
            if (buffer_readback)
            {
                RHI_CommandList::PrepareBufferForReadback(
                    buffer_heights
                );
                RHI_CommandList::CopyBufferToBuffer(
                    buffer_heights,
                    buffer_readback,
                    buffer_heights->GetObjectSize()
                );
                ocean_readback_written_mask |=
                    1u << readback_index;
            }

            m_pass_state.ocean_displacement_produced = true;
        }
        RHI_CommandList::EndPass();
    }

    bool Renderer::ResolveOceanHeightReadback(
        const uint32_t readback_index
    )
    {
        if (
            (
                ocean_readback_written_mask &
                (1u << readback_index)
            ) ==
            0
        )
        {
            return false;
        }

        RHI_Buffer* buffer = GetBuffer(
            ocean_height_readback_type(
                readback_index
            )
        );
        if (!buffer || !buffer->GetMappedData())
        {
            return false;
        }

        if (ocean_readback_copy_task.valid())
        {
            if (
                ocean_readback_copy_task.wait_for(
                    chrono::seconds(0)
                ) !=
                future_status::ready
            )
            {
                return
                    ocean_readback_copy_index ==
                    readback_index;
            }

            ocean_readback_copy_task =
                future<void>();
            ocean_readback_copy_index =
                numeric_limits<uint32_t>::max();
        }

        const size_t element_count =
            static_cast<size_t>(
                renderer_ocean_heights_resolution
            ) *
            renderer_ocean_heights_resolution *
            renderer_ocean_max_cascades;
        void* mapped_data = buffer->GetMappedData();
        const size_t object_size = buffer->GetObjectSize();

        ocean_readback_written_mask &=
            ~(1u << readback_index);
        ocean_readback_copy_index = readback_index;
        ocean_readback_copy_task = ThreadPool::AddTask(
            [
                element_count,
                mapped_data,
                object_size
            ]()
            {
                shared_ptr<vector<Vector4>> cache =
                    make_shared<vector<Vector4>>(
                        element_count
                    );
                memcpy(
                    cache->data(),
                    mapped_data,
                    object_size
                );
                ocean_heights_cache.store(
                    static_pointer_cast<
                        const vector<Vector4>
                    >(cache),
                    memory_order_release
                );
            }
        );
        return true;
    }

    void Renderer::ResetOceanHeightReadback()
    {
        if (ocean_readback_copy_task.valid())
        {
            ocean_readback_copy_task.wait();
            ocean_readback_copy_task =
                future<void>();
        }

        ocean_heights_cache.store(
            shared_ptr<const vector<Vector4>>(),
            memory_order_release
        );
        ocean_readback_copy_index =
            numeric_limits<uint32_t>::max();
        ocean_readback_written_mask = 0;
    }

    bool Renderer::GetOceanHeight(const float x, const float z, float& height)
    {
        const Water* water = m_pass_state.ocean;
        shared_ptr<const vector<Vector4>> height_cache =
            ocean_heights_cache.load(
                memory_order_acquire
            );
        if (!water || !height_cache || height_cache->empty())
        {
            return false;
        }

        const int n = static_cast<int>(renderer_ocean_heights_resolution);

        // mirror the gpu sampler, uv = world_xz / cascade_length with wrap addressing and bilinear filtering
        const float* lengths = water->GetCascadeLengths();
        uint32_t cascades    = water->GetCascadeCount();
        cascades             = cascades < 1 ? 1 : cascades;
        cascades             = cascades > renderer_ocean_max_cascades ? renderer_ocean_max_cascades : cascades;

        auto sample_displacement =
            [&](const uint32_t cascade, const float sample_x, const float sample_z)
            {
                const Vector4* slice =
                    height_cache->data() +
                    cascade * n * n;
                // samples come from full resolution texels zero four eight
                const float fx =
                    sample_x / lengths[cascade] * n -
                    0.125f;
                const float fz =
                    sample_z / lengths[cascade] * n -
                    0.125f;
                const int x0   = static_cast<int>(floorf(fx));
                const int z0   = static_cast<int>(floorf(fz));
                const float tx = fx - x0;
                const float tz = fz - z0;
                const int x0w  = x0 & (n - 1);
                const int x1w  = (x0 + 1) & (n - 1);
                const int z0w  = z0 & (n - 1);
                const int z1w  = (z0 + 1) & (n - 1);

                const Vector4& d00 = slice[z0w * n + x0w];
                const Vector4& d10 = slice[z0w * n + x1w];
                const Vector4& d01 = slice[z1w * n + x0w];
                const Vector4& d11 = slice[z1w * n + x1w];

                auto bilinear =
                    [&](const float v00, const float v10, const float v01, const float v11)
                    {
                        return
                            (v00 * (1.0f - tx) + v10 * tx) *
                            (1.0f - tz) +
                            (v01 * (1.0f - tx) + v11 * tx) *
                            tz;
                    };

                return Vector3(
                    bilinear(d00.x, d10.x, d01.x, d11.x),
                    bilinear(d00.y, d10.y, d01.y, d11.y),
                    bilinear(d00.z, d10.z, d01.z, d11.z)
                );
            };

        const float sea_level = water->GetSeaLevel();
        Vector3 shore_displacement;
        float shore_floor = 0.0f;

        // mirrors ocean_cascade_depth_scale in common.hlsl, long waves ease off in the shallows
        Terrain* terrain = Terrain::FindActive();
        auto depth_scale = [&](const uint32_t cascade, const float sample_x, const float sample_z)
        {
            float bed = 0.0f;
            if (!terrain || !terrain->SampleHeight(sample_x, sample_z, bed))
            {
                return 1.0f;
            }
            const float deep = max(lengths[cascade] * 0.12f, 8.0f);
            return lerp(0.4f, 1.0f, ocean_shore::smoothstep(0.25f, deep, max(sea_level - bed, 0.0f)));
        };

        Vector2 grid_position(x, z);
        for (uint32_t iteration = 0; iteration < 3; iteration++)
        {
            Vector2 horizontal_displacement = Vector2::Zero;
            for (uint32_t cascade = 0; cascade < cascades; cascade++)
            {
                const Vector3 displacement = sample_displacement(
                    cascade,
                    grid_position.x,
                    grid_position.y
                ) * depth_scale(cascade, grid_position.x, grid_position.y);
                horizontal_displacement.x += displacement.x;
                horizontal_displacement.y += displacement.z;
            }
            ocean_shore::evaluate(grid_position.x, grid_position.y, sea_level, shore_displacement, shore_floor);
            horizontal_displacement.x += shore_displacement.x;
            horizontal_displacement.y += shore_displacement.z;

            grid_position.x = x - horizontal_displacement.x;
            grid_position.y = z - horizontal_displacement.y;
        }

        height = sea_level;
        for (uint32_t cascade = 0; cascade < cascades; cascade++)
        {
            height += sample_displacement(
                cascade,
                grid_position.x,
                grid_position.y
            ).y * depth_scale(cascade, grid_position.x, grid_position.y);
        }
        ocean_shore::evaluate(grid_position.x, grid_position.y, sea_level, shore_displacement, shore_floor);
        height = max(height + shore_displacement.y, shore_floor);

        return true;
    }
}
