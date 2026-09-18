// Copyright(c) 2015-2026 Panos Karabelas
// SPDX-License-Identifier: MIT

#include "GeneratedCache.h"
#include "../../third_party/lz4/lz4.h"
#include <algorithm>
#include <array>
#include <cctype>

namespace spartan::generated_cache
{
    namespace
    {
        constexpr uint64_t magic = 0x3145484341435053ull; // SPCACHE1
        constexpr uint32_t version = 1;
        constexpr size_t header_size = 40;
        std::array<std::mutex, 64> file_mutexes;
        std::mutex maintenance_mutex;
        std::atomic<uint64_t> serial = 0;

        std::mutex& FileMutex(const std::filesystem::path& path)
        {
            auto name = std::filesystem::absolute(path).lexically_normal().generic_string();
#ifdef _WIN32
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
            return file_mutexes[std::hash<std::string>{}(name) % file_mutexes.size()];
        }

        // Caller holds the file's lock. Never delete the previous file on a failed replacement.
        void Write(const std::filesystem::path& path, uint64_t key, const std::vector<uint8_t>& bytes)
        {
            if (bytes.size() > maximum_payload_size) return;
            const int size = static_cast<int>(bytes.size());
            std::vector<char> compressed(static_cast<size_t>(LZ4_compressBound(size)));
            const int packed_size = size ? LZ4_compress_default(reinterpret_cast<const char*>(bytes.data()), compressed.data(), size, static_cast<int>(compressed.size())) : 0;
            const uint32_t codec = packed_size > 0 && static_cast<size_t>(packed_size) < bytes.size() ? 1u : 0u;
            const uint64_t raw_size = bytes.size();
            Hash hash; hash.Bytes(bytes.data(), bytes.size());
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
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
            file.write(reinterpret_cast<const char*>(&hash.value), sizeof(hash.value));
            file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
            file.write(reinterpret_cast<const char*>(&raw_size), sizeof(raw_size));
            file.write(reinterpret_cast<const char*>(&version), sizeof(version));
            file.write(reinterpret_cast<const char*>(&codec), sizeof(codec));
            if (codec) file.write(compressed.data(), packed_size);
            else if (size) file.write(reinterpret_cast<const char*>(bytes.data()), size);
            file.close();
            if (file) std::filesystem::rename(temporary, path, error);
        }
    }

    bool ReadPayload(const std::filesystem::path& path, uint64_t key, std::vector<uint8_t>& bytes)
    {
        try
        {
            std::lock_guard lock(FileMutex(path));
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) return false;
            const auto length = file.tellg();
            if (length < 16 || length > static_cast<std::streamoff>(maximum_payload_size + header_size)) return false;
            file.seekg(0);
            uint64_t stored_key = 0, checksum = 0, marker = 0;
            file.read(reinterpret_cast<char*>(&stored_key), sizeof(stored_key));
            file.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
            if (!file || stored_key != key) return false;
            if (length >= 24) file.read(reinterpret_cast<char*>(&marker), sizeof(marker));
            // In a valid legacy file this slot is a vector count, bounded far below magic.
            const bool legacy = marker != magic;
            uint64_t raw_size = static_cast<uint64_t>(length) - 16;
            uint32_t stored_version = 0, codec = 0;
            size_t payload_offset = 16;
            if (!legacy)
            {
                if (length < static_cast<std::streamoff>(header_size)) return false;
                file.read(reinterpret_cast<char*>(&raw_size), sizeof(raw_size));
                file.read(reinterpret_cast<char*>(&stored_version), sizeof(stored_version));
                file.read(reinterpret_cast<char*>(&codec), sizeof(codec));
                if (!file || stored_version != version || codec > 1) return false;
                payload_offset = header_size;
            }
            if (raw_size > maximum_payload_size) return false;
            const size_t stored_size = static_cast<size_t>(length) - payload_offset;
            if ((!codec && stored_size != raw_size) || (codec && (stored_size == 0 || stored_size >= raw_size))) return false;
            std::vector<uint8_t> decoded(static_cast<size_t>(raw_size));
            file.seekg(static_cast<std::streamoff>(payload_offset));
            if (codec)
            {
                std::vector<char> packed(stored_size);
                if (!file.read(packed.data(), static_cast<std::streamsize>(packed.size()))) return false;
                if (LZ4_decompress_safe(packed.data(), reinterpret_cast<char*>(decoded.data()), static_cast<int>(stored_size), static_cast<int>(raw_size)) != static_cast<int>(raw_size)) return false;
            }
            else if (raw_size && !file.read(reinterpret_cast<char*>(decoded.data()), static_cast<std::streamsize>(raw_size))) return false;
            file.close(); // Windows must release the reader before migration replaces the file.
            Hash hash; hash.Bytes(decoded.data(), decoded.size());
            if (hash.value != checksum) return false;
            // Migration failure (read-only folder/full disk) must not invalidate a successful read.
            if (legacy) { try { Write(path, key, decoded); } catch (const std::exception&) {} }
            std::error_code error;
            std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), error);
            bytes = std::move(decoded);
            return true;
        }
        catch (const std::exception&) { return false; } // A cache failure is a rebuild, never a failed world load.
    }

    void WritePayload(const std::filesystem::path& path, uint64_t key, const std::vector<uint8_t>& bytes)
    {
        try { std::lock_guard lock(FileMutex(path)); Write(path, key, bytes); }
        catch (const std::exception&) {} // Cache storage is optional.
    }

    MaintenanceResult Maintain(const std::string& world_resources, uint64_t budget)
    {
        MaintenanceResult result;
        if (world_resources.empty()) return result;
        std::unique_lock maintenance_lock(maintenance_mutex, std::try_to_lock);
        if (!maintenance_lock) return result;
        try
        {
            const auto root = std::filesystem::path(world_resources) / "generated_cache";
            std::error_code error;
            if (!std::filesystem::is_directory(root, error) || std::filesystem::is_symlink(std::filesystem::symlink_status(root, error)) || error) return result;
            struct Entry { std::filesystem::path path; uint64_t size; std::filesystem::file_time_type time; };
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
                    entries.push_back({entry.path(), size, entry.last_write_time()});
                    total += size;
                }
            }
            std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.time < b.time; });
            for (const auto& entry : entries)
            {
                if (total <= budget) break;
                std::lock_guard lock(FileMutex(entry.path));
                // A load/write since the scan makes this entry recent, so leave it alone.
                error.clear();
                if (std::filesystem::last_write_time(entry.path, error) != entry.time || error) continue;
                if (std::filesystem::remove(entry.path, error))
                {
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
