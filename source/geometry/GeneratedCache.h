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

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>
#include <map>
#include <mutex>
#include <sstream>

namespace spartan::generated_cache
{
    struct Statistics { uint64_t hits = 0, missing = 0, invalid = 0; double milliseconds = 0; };
    inline std::mutex statistics_mutex;
    inline std::map<std::string, Statistics> statistics;
    inline void ResetStatistics() { std::lock_guard lock(statistics_mutex); statistics.clear(); }
    inline std::vector<std::string> GetStatistics()
    {
        std::lock_guard lock(statistics_mutex);
        std::vector<std::string> lines;
        for (const auto& [category, stats] : statistics)
        {
            std::ostringstream line;
            line << category << ": " << stats.hits << " hits, " << stats.missing
                 << " missing/changed keys, " << stats.invalid << " invalid files, "
                 << stats.milliseconds << " ms accumulated cache reads";
            lines.push_back(line.str());
        }
        return lines;
    }
    struct ReadMeasurement
    {
        std::string category;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        bool hit = false, missing = false;
        ~ReadMeasurement()
        {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::lock_guard lock(statistics_mutex);
            auto& stats = statistics[category];
            if (hit) ++stats.hits;
            else if (missing) ++stats.missing;
            else ++stats.invalid;
            stats.milliseconds += ms;
        }
    };

    // Version the caller's key when changing a generator or its binary layout.
    struct Hash
    {
        uint64_t value = 14695981039346656037ull;
        void Bytes(const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; i++) value = (value ^ bytes[i]) * 1099511628211ull;
        }
        template<typename T> void Add(const T& v)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            Bytes(&v, sizeof(v));
        }
        void Add(const std::string& v) { Add(static_cast<uint64_t>(v.size())); Bytes(v.data(), v.size()); }
        template<typename T> void Add(const std::vector<T>& v)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            Add(static_cast<uint64_t>(v.size())); Bytes(v.data(), v.size() * sizeof(T));
        }
    };

    inline std::filesystem::path Path(const std::string& world_resources, const char* category, uint64_t key)
    {
        if (world_resources.empty()) return {};
        return std::filesystem::path(world_resources) / "generated_cache" / category / (std::to_string(key) + ".bin");
    }

    inline constexpr uint64_t maximum_payload_size = 512ull * 1024 * 1024 - 16;
    // The byte codec also accepts the original uncompressed cache format.
    bool ReadPayload(const std::filesystem::path& path, uint64_t key, std::vector<uint8_t>& bytes);
    void WritePayload(const std::filesystem::path& path, uint64_t key, const std::vector<uint8_t>& bytes);
    struct MaintenanceResult { uint64_t removed = 0, bytes_removed = 0; };
    // Only disposable generated_cache/<category>/<hash>.bin files are eligible.
    MaintenanceResult Maintain(const std::string& world_resources, uint64_t budget = 2ull * 1024 * 1024 * 1024);

    template<typename... Vectors> bool Load(const std::filesystem::path& path, uint64_t key, Vectors&... output) try
    {
        if (path.empty()) return false;
        ReadMeasurement measurement{path.parent_path().filename().string()};
        std::vector<uint8_t> bytes;
        if (!ReadPayload(path, key, bytes))
        {
            std::error_code error;
            measurement.missing = !std::filesystem::exists(path, error);
            return false;
        }
        size_t cursor = 0;
        auto read = [&](auto& values)
        {
            using T = typename std::decay_t<decltype(values)>::value_type;
            static_assert(std::is_trivially_copyable_v<T>);
            if (bytes.size() - cursor < sizeof(uint64_t)) return false;
            uint64_t count;
            memcpy(&count, bytes.data() + cursor, sizeof(count)); cursor += sizeof(count);
            if (count > (bytes.size() - cursor) / sizeof(T)) return false;
            values.resize(static_cast<size_t>(count));
            if (count) memcpy(values.data(), bytes.data() + cursor, values.size() * sizeof(T));
            cursor += values.size() * sizeof(T);
            return true;
        };
        std::tuple<std::decay_t<Vectors>...> loaded;
        if (!std::apply([&](auto&... values) { return (read(values) && ...); }, loaded) || cursor != bytes.size()) return false;
        std::tie(output...) = std::move(loaded);
        measurement.hit = true;
        return true;
    }
    catch (const std::exception&) { return false; }

    template<typename... Vectors> void Save(const std::filesystem::path& path, uint64_t key, const Vectors&... input) try
    {
        if (path.empty()) return;
        std::vector<uint8_t> bytes;
        bool fits = true;
        auto append = [&](const auto& values)
        {
            using T = typename std::decay_t<decltype(values)>::value_type;
            static_assert(std::is_trivially_copyable_v<T>);
            const uint64_t count = values.size();
            const size_t start = bytes.size();
            if (!fits || maximum_payload_size - start < sizeof(count) ||
                count > (maximum_payload_size - start - sizeof(count)) / sizeof(T))
            {
                fits = false;
                return;
            }
            bytes.resize(start + sizeof(count) + values.size() * sizeof(T));
            memcpy(bytes.data() + start, &count, sizeof(count));
            if (count) memcpy(bytes.data() + start + sizeof(count), values.data(), values.size() * sizeof(T));
        };
        (append(input), ...);
        if (fits) WritePayload(path, key, bytes);
    }
    catch (const std::exception&) {} // Allocation/storage failure must not fail a world save.
}
