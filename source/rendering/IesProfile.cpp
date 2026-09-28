/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ====================
#include "pch.h"
#include "IesProfile.h"
#include "../rhi/RHI_Texture.h"
#include "../rhi/RHI_Vertex.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
//===============================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        struct lm63_data
        {
            int photometric_type = 1;
            vector<float> vertical;
            vector<float> horizontal;
            vector<float> candela; // horizontal major, vertical.size() values per horizontal angle
        };

        struct profile_entry
        {
            IesProfileInfo info;
            string key;
        };

        mutex profiles_mutex;
        vector<profile_entry> profiles;
        vector<uint16_t> atlas_texels;
        shared_ptr<RHI_Texture> atlas;
        vector<shared_ptr<RHI_Texture>> atlas_retired;

        const float angle_epsilon = 0.01f;

        string to_upper(string text)
        {
            for (char& c : text)
            {
                c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            }
            return text;
        }

        string trim(const string& text)
        {
            const size_t first = text.find_first_not_of(" \t\r\n");
            if (first == string::npos)
            {
                return "";
            }
            const size_t last = text.find_last_not_of(" \t\r\n");
            return text.substr(first, last - first + 1);
        }

        bool parse_lm63(const string& text, lm63_data& out, string& error)
        {
            const size_t tilt = to_upper(text).find("TILT=");
            if (tilt == string::npos)
            {
                error = "no TILT= line";
                return false;
            }

            size_t line_end = text.find_first_of("\r\n", tilt);
            if (line_end == string::npos)
            {
                line_end = text.size();
            }
            const string tilt_value = to_upper(trim(text.substr(tilt + 5, line_end - tilt - 5)));

            string rest = text.substr(line_end);
            for (char& c : rest)
            {
                if (c == ',')
                {
                    c = ' ';
                }
            }

            vector<double> numbers;
            istringstream stream(rest);
            double number = 0.0;
            while (stream >> number)
            {
                numbers.push_back(number);
            }

            size_t cursor = 0;
            auto next = [&](double& value)
            {
                if (cursor >= numbers.size())
                {
                    return false;
                }
                value = numbers[cursor++];
                return true;
            };

            // tilt data only matters for luminaires rotated away from their test orientation, skip it
            if (tilt_value == "INCLUDE")
            {
                double geometry = 0.0, pairs = 0.0;
                if (!next(geometry) || !next(pairs) || pairs < 0.0)
                {
                    error = "truncated tilt block";
                    return false;
                }
                cursor += 2 * static_cast<size_t>(pairs);
            }

            double lamps = 0.0, lumens_per_lamp = 0.0, multiplier = 0.0, count_vertical = 0.0, count_horizontal = 0.0, type = 0.0;
            double units = 0.0, width = 0.0, length = 0.0, height = 0.0, ballast = 0.0, future = 0.0, watts = 0.0;
            if (!next(lamps) || !next(lumens_per_lamp) || !next(multiplier) || !next(count_vertical) || !next(count_horizontal) || !next(type) ||
                !next(units) || !next(width) || !next(length) || !next(height) || !next(ballast) || !next(future) || !next(watts))
            {
                error = "truncated header";
                return false;
            }

            const size_t nv = static_cast<size_t>(count_vertical);
            const size_t nh = static_cast<size_t>(count_horizontal);
            if (nv < 2 || nh < 1 || nv > 10000 || nh > 10000)
            {
                error = "invalid angle counts";
                return false;
            }

            out.photometric_type = static_cast<int>(type);
            if (out.photometric_type < 1 || out.photometric_type > 3)
            {
                error = "unknown photometric type";
                return false;
            }

            out.vertical.resize(nv);
            out.horizontal.resize(nh);
            out.candela.resize(nv * nh);
            const double scale = multiplier * (ballast > 0.0 ? ballast : 1.0);
            const size_t count = nv + nh + nv * nh;
            if (numbers.size() - cursor < count)
            {
                error = "truncated angle or candela values";
                return false;
            }

            for (size_t i = 0; i < nv; i++)
            {
                out.vertical[i] = static_cast<float>(numbers[cursor++]);
            }
            for (size_t i = 0; i < nh; i++)
            {
                out.horizontal[i] = static_cast<float>(numbers[cursor++]);
            }
            for (size_t i = 0; i < nv * nh; i++)
            {
                out.candela[i] = static_cast<float>(max(numbers[cursor++] * scale, 0.0));
            }

            for (size_t i = 1; i < nv; i++)
            {
                if (out.vertical[i] <= out.vertical[i - 1])
                {
                    error = "vertical angles are not increasing";
                    return false;
                }
            }
            for (size_t i = 1; i < nh; i++)
            {
                if (out.horizontal[i] <= out.horizontal[i - 1])
                {
                    error = "horizontal angles are not increasing";
                    return false;
                }
            }

            return true;
        }

        // false outside the measured range, otherwise the lower index and the blend towards the next one
        bool bracket(const vector<float>& axis, float x, size_t& index, float& t)
        {
            if (x < axis.front() - angle_epsilon || x > axis.back() + angle_epsilon)
            {
                return false;
            }

            x = clamp(x, axis.front(), axis.back());
            const size_t upper = static_cast<size_t>(upper_bound(axis.begin(), axis.end(), x) - axis.begin());
            index = upper == 0 ? 0 : min(upper - 1, axis.size() - 1);
            if (index + 1 >= axis.size())
            {
                index = axis.size() - 1;
                t     = 0.0f;
                return true;
            }

            t = (x - axis[index]) / (axis[index + 1] - axis[index]);
            return true;
        }

        // type c files store a symmetric subset of the planes, fold c back into what was measured
        float fold_type_c(const vector<float>& horizontal, float c)
        {
            const float first = horizontal.front();
            const float last  = horizontal.back();
            if (last <= 90.0f + angle_epsilon && first >= -angle_epsilon)
            {
                c = c > 180.0f ? 360.0f - c : c;
                c = c > 90.0f ? 180.0f - c : c;
            }
            else if (last <= 180.0f + angle_epsilon && first >= -angle_epsilon)
            {
                c = c > 180.0f ? 360.0f - c : c;
            }
            else if (first >= 90.0f - angle_epsilon && last <= 270.0f + angle_epsilon)
            {
                c = c < 90.0f ? 180.0f - c : (c > 270.0f ? 540.0f - c : c);
            }
            return c;
        }

        float candela_at(const lm63_data& data, const Vector3& direction)
        {
            float h = 0.0f;
            float v = 0.0f;
            if (data.photometric_type == 1)
            {
                v = acos(clamp(direction.z, -1.0f, 1.0f)) * rad_to_deg;
                h = atan2(direction.y, direction.x) * rad_to_deg;
                if (h < 0.0f)
                {
                    h += 360.0f;
                }
                h = fold_type_c(data.horizontal, h);
            }
            else
            {
                if (data.photometric_type == 3)
                {
                    h = asin(clamp(direction.x, -1.0f, 1.0f)) * rad_to_deg;
                    v = atan2(direction.y, direction.z) * rad_to_deg;
                }
                else
                {
                    h = atan2(direction.x, direction.z) * rad_to_deg;
                    v = asin(clamp(direction.y, -1.0f, 1.0f)) * rad_to_deg;
                }

                // a half range file is mirrored about its zero plane
                if (data.horizontal.front() >= -angle_epsilon)
                {
                    h = abs(h);
                }
                if (data.vertical.front() >= -angle_epsilon)
                {
                    v = abs(v);
                }
            }

            size_t vi = 0, hi = 0;
            float tv = 0.0f, th = 0.0f;
            if (!bracket(data.vertical, v, vi, tv))
            {
                return 0.0f;
            }
            if (data.horizontal.size() > 1 && !bracket(data.horizontal, h, hi, th))
            {
                return 0.0f;
            }

            const size_t nv = data.vertical.size();
            const size_t nh = data.horizontal.size();
            const size_t vj = min(vi + 1, nv - 1);
            const size_t hj = min(hi + 1, nh - 1);
            auto at = [&](size_t h_index, size_t v_index) { return data.candela[h_index * nv + v_index]; };
            const float a = at(hi, vi) + (at(hi, vj) - at(hi, vi)) * tv;
            const float b = at(hj, vi) + (at(hj, vj) - at(hj, vi)) * tv;
            return a + (b - a) * th;
        }

        Vector3 angles_to_direction(int type, float h_deg, float v_deg)
        {
            const float h = h_deg * deg_to_rad;
            const float v = v_deg * deg_to_rad;
            if (type == 1)
            {
                return Vector3(sin(v) * cos(h), sin(v) * sin(h), cos(v));
            }
            if (type == 3)
            {
                return Vector3(sin(h), cos(h) * sin(v), cos(h) * cos(v));
            }
            return Vector3(sin(h) * cos(v), sin(v), cos(h) * cos(v));
        }

        // tangent of the smallest square tile that holds every direction brighter than 0.2% of the peak
        float compute_tile_tangent(const lm63_data& data)
        {
            float peak = 0.0f;
            for (float value : data.candela)
            {
                peak = max(peak, value);
            }

            const float threshold   = peak * 0.002f;
            const float tangent_max = tan(85.0f * deg_to_rad);
            float tangent           = tan(1.0f * deg_to_rad);
            const size_t nv         = data.vertical.size();
            for (size_t hi = 0; hi < data.horizontal.size(); hi++)
            {
                for (size_t vi = 0; vi < nv; vi++)
                {
                    if (data.candela[hi * nv + vi] <= threshold)
                    {
                        continue;
                    }

                    if (data.photometric_type == 1)
                    {
                        const float gamma = data.vertical[vi];
                        tangent = gamma >= 85.0f ? tangent_max : max(tangent, tan(gamma * deg_to_rad));
                        continue;
                    }

                    const Vector3 direction = angles_to_direction(data.photometric_type, data.horizontal[hi], data.vertical[vi]);
                    if (direction.z <= 0.05f)
                    {
                        tangent = tangent_max;
                        continue;
                    }
                    tangent = max(tangent, max(abs(direction.x), abs(direction.y)) / direction.z);
                }
            }

            return min(tangent * 1.03f, tangent_max);
        }

        void rebuild_atlas()
        {
            const uint32_t count = static_cast<uint32_t>(profiles.size());
            vector<RHI_Texture_Slice> slices(1);
            slices[0].mips.resize(1);
            slices[0].mips[0].bytes.resize(atlas_texels.size() * sizeof(uint16_t));
            memcpy(slices[0].mips[0].bytes.data(), atlas_texels.data(), atlas_texels.size() * sizeof(uint16_t));

            // passes in flight may still sample the previous atlas, keep it alive
            if (atlas)
            {
                atlas_retired.push_back(atlas);
            }
            atlas = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, ies::tile_size, ies::tile_size * count, 1, 1, RHI_Format::R16_Float, RHI_Texture_Srv, "ies_atlas", move(slices));
        }
    }

    uint32_t ies::acquire(const string& file_path)
    {
        if (file_path.empty())
        {
            return 0;
        }

        const string key = filesystem::path(file_path).lexically_normal().generic_string();

        lock_guard<mutex> lock(profiles_mutex);
        for (size_t i = 0; i < profiles.size(); i++)
        {
            if (profiles[i].key == key)
            {
                return static_cast<uint32_t>(i + 1);
            }
        }

        if (profiles.size() >= max_profiles)
        {
            SP_LOG_ERROR("ies profile '%s' rejected, the atlas holds %u profiles", file_path.c_str(), max_profiles);
            return 0;
        }

        ifstream file(file_path, ios::binary);
        if (!file)
        {
            SP_LOG_ERROR("ies profile '%s' could not be opened", file_path.c_str());
            return 0;
        }
        stringstream buffer;
        buffer << file.rdbuf();

        lm63_data data;
        string error;
        if (!parse_lm63(buffer.str(), data, error))
        {
            SP_LOG_ERROR("ies profile '%s' is invalid: %s", file_path.c_str(), error.c_str());
            return 0;
        }

        const float tangent   = compute_tile_tangent(data);
        const uint32_t n      = tile_size;
        const float texel     = 2.0f * tangent / static_cast<float>(n);
        vector<float> candela(static_cast<size_t>(n) * n);
        vector<float> solid_angle_weights(static_cast<size_t>(n) * n);
        float peak = 0.0f;
        for (uint32_t row = 0; row < n; row++)
        {
            const float y = (1.0f - (row + 0.5f) / n * 2.0f) * tangent;
            for (uint32_t column = 0; column < n; column++)
            {
                const float x      = ((column + 0.5f) / n * 2.0f - 1.0f) * tangent;
                const float r2     = 1.0f + x * x + y * y;
                const size_t index = static_cast<size_t>(row) * n + column;
                candela[index]              = candela_at(data, Vector3(x, y, 1.0f) / sqrt(r2));
                solid_angle_weights[index]  = texel * texel / (r2 * sqrt(r2));
                peak                        = max(peak, candela[index]);
            }
        }

        if (peak <= 0.0f)
        {
            SP_LOG_ERROR("ies profile '%s' emits no light in front of the lamp", file_path.c_str());
            return 0;
        }

        float solid_angle = 0.0f;
        const size_t offset = atlas_texels.size();
        atlas_texels.resize(offset + candela.size());
        for (size_t i = 0; i < candela.size(); i++)
        {
            const float normalized  = candela[i] / peak;
            solid_angle            += normalized * solid_angle_weights[i];
            atlas_texels[offset + i] = vertex_pack::float_to_half(normalized);
        }

        // get_info hands out pointers into this vector, it must never reallocate
        profiles.reserve(max_profiles);

        profile_entry entry;
        entry.key               = key;
        entry.info.file_path    = file_path;
        entry.info.extent_rad   = atan(tangent);
        entry.info.solid_angle  = solid_angle;
        entry.info.peak_candela = peak;
        entry.info.lumens       = peak * solid_angle;
        profiles.push_back(entry);
        rebuild_atlas();

        SP_LOG_INFO("ies profile '%s': extent %.1f deg, peak %.0f cd, %.0f lm", file_path.c_str(), entry.info.extent_rad * rad_to_deg, peak, entry.info.lumens);
        return static_cast<uint32_t>(profiles.size());
    }

    const IesProfileInfo* ies::get_info(uint32_t slot)
    {
        lock_guard<mutex> lock(profiles_mutex);
        if (slot == 0 || slot > profiles.size())
        {
            return nullptr;
        }
        return &profiles[slot - 1].info;
    }

    RHI_Texture* ies::get_atlas()
    {
        lock_guard<mutex> lock(profiles_mutex);
        return atlas.get();
    }

    void ies::shutdown()
    {
        lock_guard<mutex> lock(profiles_mutex);
        atlas.reset();
        atlas_retired.clear();
        atlas_texels.clear();
        profiles.clear();
    }
}
