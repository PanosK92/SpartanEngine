/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
#include <span>

namespace spartan::generated_cache
{
    struct Statistics { uint64_t hits = 0, missing = 0, invalid = 0; double milliseconds = 0; };
    inline std::mutex statistics_mutex;
    inline std::map<std::string, Statistics> statistics;
    inline auto session_started = std::filesystem::file_time_type::clock::now();
    inline void ResetStatistics()
    {
        std::lock_guard lock(statistics_mutex);
        statistics.clear();
        session_started = std::filesystem::file_time_type::clock::now();
    }
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

    // Fast bulk hashing for derived payloads. The incremental Hash below keeps
    // existing recipe keys stable, so old geometry bakes remain reusable.
    uint64_t HashBytes(const void* data, size_t size);
    void LoadChecksumIndex(const std::string& resources);
    void SaveChecksumIndex(const std::string& resources);

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
    // Evict unused history, retaining this session's working set even if it
    // exceeds the soft budget. Otherwise large worlds rebuild on every load.
    MaintenanceResult Maintain(const std::string& world_resources, uint64_t budget = 2ull * 1024 * 1024 * 1024);

    template<typename... Vectors> bool Decode(std::span<const uint8_t> bytes, Vectors&... output) try
    {
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
        return true;
    }
    catch (const std::exception&) { return false; }

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
        measurement.hit = Decode(bytes, output...);
        return measurement.hit;
    }
    catch (const std::exception&) { return false; }

    template<typename... Vectors> std::vector<uint8_t> Encode(const Vectors&... input)
    {
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
        return fits ? std::move(bytes) : std::vector<uint8_t>{};
    }

    template<typename... Vectors> void Save(const std::filesystem::path& path, uint64_t key, const Vectors&... input) try
    {
        if (path.empty()) return;
        auto bytes = Encode(input...);
        if (!bytes.empty()) WritePayload(path, key, bytes);
    }
    catch (const std::exception&) {} // Allocation/storage failure must not fail a world save.
}
