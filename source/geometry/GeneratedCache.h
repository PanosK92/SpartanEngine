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

namespace spartan::generated_cache
{
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

    template<typename... Vectors> bool Load(const std::filesystem::path& path, uint64_t key, Vectors&... output)
    {
        if (path.empty()) return false;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return false;
        const auto length = file.tellg();
        // Bound allocations and reject truncated/corrupt caches before touching caller data.
        if (length < 16 || length > 512ll * 1024 * 1024) return false;
        file.seekg(0);
        uint64_t stored_key = 0, checksum = 0;
        file.read(reinterpret_cast<char*>(&stored_key), sizeof(stored_key));
        file.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
        if (stored_key != key) return false;
        std::vector<uint8_t> bytes(static_cast<size_t>(length) - 16);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return false;
        Hash hash; hash.Bytes(bytes.data(), bytes.size());
        if (hash.value != checksum) return false;
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

    template<typename... Vectors> void Save(const std::filesystem::path& path, uint64_t key, const Vectors&... input)
    {
        if (path.empty()) return;
        std::vector<uint8_t> bytes;
        auto append = [&](const auto& values)
        {
            using T = typename std::decay_t<decltype(values)>::value_type;
            static_assert(std::is_trivially_copyable_v<T>);
            const uint64_t count = values.size();
            const size_t start = bytes.size();
            bytes.resize(start + sizeof(count) + values.size() * sizeof(T));
            memcpy(bytes.data() + start, &count, sizeof(count));
            if (count) memcpy(bytes.data() + start + sizeof(count), values.data(), values.size() * sizeof(T));
        };
        (append(input), ...);
        if (bytes.size() > 512ull * 1024 * 1024 - 16) return;
        Hash hash; hash.Bytes(bytes.data(), bytes.size());
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return;
        static std::atomic<uint64_t> serial = 0;
        const auto temporary = path.string() + ".tmp." +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." + std::to_string(serial++);
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(&key), sizeof(key));
            file.write(reinterpret_cast<const char*>(&hash.value), sizeof(hash.value));
            file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            file.close();
            if (!file) { std::filesystem::remove(temporary, error); return; }
        }
        // Publish complete files only. A simultaneous writer of the same key is equivalent.
        std::filesystem::rename(temporary, path, error);
        if (error)
        {
            std::filesystem::remove(path, error);
            error.clear();
            std::filesystem::rename(temporary, path, error);
        }
        if (error) std::filesystem::remove(temporary, error);
    }
}
