// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
// SPDX-License-Identifier: MIT

#include "pch.h"
#include "GeneratedCache.h"
#include "../../third_party/lz4/lz4.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <unordered_set>
#include <SDL3/SDL_iostream.h>
#if defined(__EMSCRIPTEN__)
// xxhash includes this inside extern "C" on wasm, emscripten's version carries templates, so it goes in first
#include <arm_neon.h>
#endif
#define XXH_INLINE_ALL
#include "../../third_party/xxhash/xxhash.h"

namespace spartan::generated_cache
{
    uint64_t HashBytes(const void* data, size_t size) { return XXH3_64bits(data, size); }

    namespace
    {
        constexpr uint64_t magic = 0x3145484341435053ull; // SPCACHE1
        constexpr uint32_t version = 2;
        constexpr size_t header_size = 40;
        std::array<std::mutex, 64> file_mutexes;
        std::mutex maintenance_mutex;
        std::atomic<uint64_t> serial = 0;
        std::mutex checksum_mutex;
        std::map<std::pair<uint64_t, uint64_t>, uint64_t> legacy_checksums;
        struct ChecksumEntry { uint64_t original, size, fast; };
        std::mutex touched_mutex;
        std::unordered_set<uint64_t> session_touched;

        // Avoids OS path-canonicalization syscalls on every cache read while remaining stable
        // across relative and absolute paths for the same category/filename pair.
        uint64_t PathIdentityHash(const std::filesystem::path& path)
        {
            std::string key = path.parent_path().filename().string();
            key.push_back('/');
            key += path.filename().string();
            for (char& c : key)
            {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            }
            return HashBytes(key.data(), key.size());
        }

        std::mutex& FileMutex(const std::filesystem::path& path)
        {
            return file_mutexes[PathIdentityHash(path) % file_mutexes.size()];
        }

        void MarkTouched(const std::filesystem::path& path)
        {
            const uint64_t id = PathIdentityHash(path);
            std::lock_guard lock(touched_mutex);
            session_touched.insert(id);
        }

        bool ReadWholeFile(const std::filesystem::path& path, size_t max_bytes, std::vector<uint8_t>& out)
        {
            const std::u8string u8 = path.generic_u8string();
            const std::string utf8(reinterpret_cast<const char*>(u8.data()), u8.size());
            SDL_IOStream* stream = SDL_IOFromFile(utf8.c_str(), "rb");
            if (!stream)
            {
                return false;
            }
            struct IoGuard
            {
                SDL_IOStream* s;
                ~IoGuard() { SDL_CloseIO(s); }
            } guard{stream};

            const Sint64 file_size = SDL_GetIOSize(stream);
            if (file_size < 16 || static_cast<uint64_t>(file_size) > max_bytes)
            {
                return false;
            }

            const size_t total = static_cast<size_t>(file_size);
            out.resize(total);
            size_t offset = 0;
            while (offset < total)
            {
                const size_t bytes_read = SDL_ReadIO(stream, out.data() + offset, total - offset);
                if (bytes_read == 0)
                {
                    return false;
                }
                offset += bytes_read;
            }
            return true;
        }

        // Caller holds the file's lock. Never delete the previous file on a failed replacement.
        void WriteEncoded(const std::filesystem::path& path, uint64_t key, uint64_t checksum,
            uint64_t raw_size, uint32_t codec, const void* payload, size_t payload_size)
        {
            std::error_code error;
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
            if (error) return;
            const auto temporary = path.string() + ".tmp." +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." + std::to_string(serial++);
            struct TemporaryFile
            {
                std::filesystem::path path;
                ~TemporaryFile() { std::error_code ignored; std::filesystem::remove(path, ignored); }
            } cleanup{temporary};
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(&key), sizeof(key));
            file.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
            file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
            file.write(reinterpret_cast<const char*>(&raw_size), sizeof(raw_size));
            file.write(reinterpret_cast<const char*>(&version), sizeof(version));
            file.write(reinterpret_cast<const char*>(&codec), sizeof(codec));
            if (payload_size) file.write(static_cast<const char*>(payload), static_cast<std::streamsize>(payload_size));
            file.close();
            if (file) std::filesystem::rename(temporary, path, error);
        }

        void Write(const std::filesystem::path& path, uint64_t key, const std::vector<uint8_t>& bytes)
        {
            if (bytes.size() > maximum_payload_size) return;
            const int size = static_cast<int>(bytes.size());
            std::vector<char> compressed(static_cast<size_t>(LZ4_compressBound(size)));
            const int packed_size = size ? LZ4_compress_default(reinterpret_cast<const char*>(bytes.data()), compressed.data(), size, static_cast<int>(compressed.size())) : 0;
            const bool packed = packed_size > 0 && static_cast<size_t>(packed_size) < bytes.size();
            WriteEncoded(path, key, HashBytes(bytes.data(), bytes.size()), bytes.size(), packed ? 1u : 0u,
                packed ? static_cast<const void*>(compressed.data()) : bytes.data(), packed ? packed_size : bytes.size());
        }
    }

    void ClearTouched()
    {
        std::lock_guard lock(touched_mutex);
        session_touched.clear();
    }

    bool ReadPayload(const std::filesystem::path& path, uint64_t key, std::vector<uint8_t>& bytes)
    {
        try
        {
            std::vector<uint8_t> raw;
            {
                std::lock_guard lock(FileMutex(path));
                if (!ReadWholeFile(path, maximum_payload_size + header_size, raw))
                {
                    return false;
                }
            }

            const size_t length = raw.size();
            if (length < 16) return false;
            uint64_t stored_key = 0, checksum = 0, marker = 0;
            memcpy(&stored_key, raw.data(), sizeof(stored_key));
            memcpy(&checksum, raw.data() + 8, sizeof(checksum));
            if (stored_key != key) return false;
            if (length >= 24) memcpy(&marker, raw.data() + 16, sizeof(marker));
            // In a valid legacy file this slot is a vector count, bounded far below magic.
            const bool legacy = marker != magic;
            uint64_t raw_size = static_cast<uint64_t>(length) - 16;
            uint32_t stored_version = 0, codec = 0;
            size_t payload_offset = 16;
            if (!legacy)
            {
                if (length < header_size) return false;
                memcpy(&raw_size, raw.data() + 24, sizeof(raw_size));
                memcpy(&stored_version, raw.data() + 32, sizeof(stored_version));
                memcpy(&codec, raw.data() + 36, sizeof(codec));
                if ((stored_version != 1 && stored_version != version) || codec > 1) return false;
                payload_offset = header_size;
            }
            if (raw_size > maximum_payload_size) return false;
            const size_t stored_size = length - payload_offset;
            if ((!codec && stored_size != raw_size) || (codec && (stored_size == 0 || stored_size >= raw_size))) return false;
            std::vector<uint8_t> decoded(static_cast<size_t>(raw_size));
            if (codec)
            {
                const char* source = reinterpret_cast<const char*>(raw.data() + payload_offset);
                if (LZ4_decompress_safe(source, reinterpret_cast<char*>(decoded.data()), static_cast<int>(stored_size), static_cast<int>(raw_size)) != static_cast<int>(raw_size)) return false;
            }
            else if (raw_size)
            {
                memcpy(decoded.data(), raw.data() + payload_offset, static_cast<size_t>(raw_size));
            }
            raw.clear();
            raw.shrink_to_fit();

            const uint64_t fast_checksum = HashBytes(decoded.data(), decoded.size());
            if (stored_version < 2)
            {
                // Learn a fast digest only after validating the original FNV
                // checksum. The small index avoids rewriting multi-GB bake files
                // merely to change their checksum algorithm.
                const auto id = std::make_pair(checksum, raw_size);
                bool known = false;
                {
                    std::lock_guard lock(checksum_mutex);
                    const auto found = legacy_checksums.find(id);
                    if (found != legacy_checksums.end())
                    {
                        if (found->second != fast_checksum) return false;
                        known = true;
                    }
                }
                if (!known)
                {
                    Hash hash; hash.Bytes(decoded.data(), decoded.size());
                    if (hash.value != checksum) return false;
                    std::lock_guard lock(checksum_mutex);
                    legacy_checksums[id] = fast_checksum;
                }
            }
            else if (fast_checksum != checksum) return false;
            MarkTouched(path);
            bytes = std::move(decoded);
            return true;
        }
        catch (const std::exception&) { return false; } // A cache failure is a rebuild, never a failed world load.
    }

    void WritePayload(const std::filesystem::path& path, uint64_t key, const std::vector<uint8_t>& bytes)
    {
        try
        {
            std::lock_guard lock(FileMutex(path));
            Write(path, key, bytes);
            MarkTouched(path);
        }
        catch (const std::exception&) {} // Cache storage is optional.
    }

    void LoadChecksumIndex(const std::string& resources)
    {
        std::vector<ChecksumEntry> entries;
        constexpr uint64_t key = 1;
        if (!Load(Path(resources, "checksums", key), key, entries)) return;
        std::lock_guard lock(checksum_mutex);
        for (const auto& entry : entries)
            legacy_checksums[{entry.original, entry.size}] = entry.fast;
    }

    void SaveChecksumIndex(const std::string& resources)
    {
        std::vector<ChecksumEntry> entries;
        {
            std::lock_guard lock(checksum_mutex);
            entries.reserve(legacy_checksums.size());
            for (const auto& [id, fast] : legacy_checksums)
                entries.push_back({id.first, id.second, fast});
        }
        if (entries.empty()) return;
        constexpr uint64_t key = 1;
        Save(Path(resources, "checksums", key), key, entries);
    }

    MaintenanceResult Maintain(const std::string& world_resources, uint64_t budget)
    {
        MaintenanceResult result;
        if (world_resources.empty()) return result;
        std::unique_lock maintenance_lock(maintenance_mutex, std::try_to_lock);
        if (!maintenance_lock) return result;
        std::filesystem::file_time_type protect_since;
        {
            std::lock_guard lock(statistics_mutex);
            protect_since = session_started;
        }
        try
        {
            const auto root = std::filesystem::path(world_resources) / "generated_cache";
            std::error_code error;
            if (!std::filesystem::is_directory(root, error) || std::filesystem::is_symlink(std::filesystem::symlink_status(root, error)) || error) return result;
            std::unordered_set<uint64_t> touched_snapshot;
            {
                std::lock_guard lock(touched_mutex);
                touched_snapshot = session_touched;
            }
            const auto now = std::filesystem::file_time_type::clock::now();
            const auto touch_threshold = now - std::chrono::hours(1);
            struct Entry { std::filesystem::path path; uint64_t size; std::filesystem::file_time_type time; bool active; };
            std::vector<Entry> entries;
            uint64_t total = 0;
            // Do not recurse through links or walk outside this disposable cache directory.
            for (const auto& category : std::filesystem::directory_iterator(root))
            {
                if (category.is_symlink() || !category.is_directory()) continue;
                for (const auto& entry : std::filesystem::directory_iterator(category.path()))
                {
                    if (entry.is_symlink() || !entry.is_regular_file() || entry.path().extension() != ".bin") continue;
                    const auto stem = entry.path().stem().string();
                    if (stem.empty() || stem.size() > 20 || !std::all_of(stem.begin(), stem.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
                    const auto size = entry.file_size();
                    const auto mtime = entry.last_write_time();
                    const bool active = touched_snapshot.contains(PathIdentityHash(entry.path()));
                    if (active && mtime < touch_threshold)
                    {
                        // Refresh LRU timestamp off the load path so cross-session eviction still knows this entry is hot.
                        std::error_code touch_error;
                        std::filesystem::last_write_time(entry.path(), now, touch_error);
                    }
                    entries.push_back({entry.path(), size, mtime, active});
                    total += size;
                }
            }
            std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.time < b.time; });
            for (const auto& entry : entries)
            {
                if (total <= budget) break;
                if (entry.active || entry.time >= protect_since) continue;
                std::lock_guard lock(FileMutex(entry.path));
                // A load/write since the scan makes this entry recent, so leave it alone.
                error.clear();
                if (std::filesystem::last_write_time(entry.path, error) != entry.time || error) continue;
                if (std::filesystem::remove(entry.path, error))
                {
                    // Remove any companion .mesh sidecar (e.g. imported_models).
                    auto sidecar = entry.path;
                    sidecar.replace_extension(".mesh");
                    std::error_code sidecar_error;
                    std::filesystem::remove(sidecar, sidecar_error);
                    total -= entry.size;
                    ++result.removed;
                    result.bytes_removed += entry.size;
                }
            }
        }
        catch (const std::exception&) {} // Incomplete enumeration is safe; try again next save/load.
        return result;
    }
}
