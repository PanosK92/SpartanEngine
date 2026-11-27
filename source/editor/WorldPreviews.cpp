/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "WorldPreviews.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <deque>
#include <filesystem>
#include <future>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>
#include "core/ProgressTracker.h"
#include "core/ThreadPool.h"
#include "core/Timer.h"
#include "file_system/FileSystem.h"
#include "rendering/Renderer.h"
#include "resource/ResourceCache.h"
#include "resource/import/ImageImporter.h"
#include "rhi/RHI_Device.h"
#include "rhi/RHI_Texture.h"
#include "world/World.h"

using namespace std;

namespace
{
    enum class preview_request_type
    {
        none,
        world_file
    };

    struct preview_request_state
    {
        preview_request_type type = preview_request_type::none;
        string world_file_path;
        string preview_path;
        bool waiting_for_load_start = false;
        bool waiting_for_load_finish = false;
        double capture_delay_remaining_sec = 0.0;
    };

    enum class preview_entry_state
    {
        loading,
        ready,
        failed
    };

    struct preview_entry
    {
        preview_entry_state state = preview_entry_state::loading;
        bool allow_delete         = false;
        shared_ptr<spartan::RHI_Texture> texture;
    };

    struct queued_load
    {
        string preview_path;
        bool allow_delete = false;
    };

    struct decoded_preview
    {
        string preview_path;
        bool allow_delete = false;
        bool invalid      = false;
        shared_ptr<spartan::RHI_Texture> texture;
    };

    preview_request_state preview_request;
    unordered_map<string, preview_entry> preview_entries;
    unordered_map<string, string> resolved_preview_paths;
    deque<queued_load> pending_loads;
    vector<future<void>> active_loads;
    mutex decoded_mutex;
    deque<decoded_preview> decoded_previews;
    atomic<bool> shutting_down = false;

    constexpr uint32_t preview_max_dimension      = 512;
    constexpr double preview_capture_delay_sec    = 1.0;
    constexpr uint32_t max_concurrent_loads       = 8;
    constexpr uint32_t max_gpu_uploads_per_frame  = 4;
    constexpr const char* preview_cache_directory = "cache/generated_cache/world_previews";

    void downscale_preview(spartan::RHI_Texture* texture)
    {
        if (!texture)
        {
            return;
        }

        while (
            texture->GetWidth() > preview_max_dimension ||
            texture->GetHeight() > preview_max_dimension
        )
        {
            spartan::RHI_Texture_Mip* mip = texture->GetMip(0, 0);
            if (!mip || mip->bytes.empty())
            {
                return;
            }

            const uint32_t width    = texture->GetWidth();
            const uint32_t height   = texture->GetHeight();
            const uint32_t channels = max(texture->GetChannelCount(), 1u);
            const uint32_t new_width  = max(1u, width / 2);
            const uint32_t new_height = max(1u, height / 2);
            vector<std::byte> output(
                static_cast<size_t>(new_width) * new_height * channels
            );
            const uint8_t* input =
                reinterpret_cast<const uint8_t*>(mip->bytes.data());

            for (uint32_t y = 0; y < new_height; y++)
            {
                for (uint32_t x = 0; x < new_width; x++)
                {
                    const uint32_t src_x = min(x * 2, width - 1);
                    const uint32_t src_y = min(y * 2, height - 1);
                    const uint32_t src_x2 = min(src_x + 1, width - 1);
                    const uint32_t src_y2 = min(src_y + 1, height - 1);
                    const uint32_t dst_index = (y * new_width + x) * channels;
                    for (uint32_t c = 0; c < channels; c++)
                    {
                        const uint32_t sum =
                            input[(src_y * width + src_x) * channels + c] +
                            input[(src_y * width + src_x2) * channels + c] +
                            input[(src_y2 * width + src_x) * channels + c] +
                            input[(src_y2 * width + src_x2) * channels + c];
                        output[dst_index + c] =
                            static_cast<std::byte>(sum / 4);
                    }
                }
            }

            mip->bytes = move(output);
            texture->SetWidth(new_width);
            texture->SetHeight(new_height);
        }
    }

    // a capture taken before the scene presented its first lit frame is pure black and must not stick as the preview
    bool is_blank_preview(spartan::RHI_Texture* texture)
    {
        spartan::RHI_Texture_Mip* mip = texture->GetMip(0, 0);
        if (!mip || mip->bytes.empty() || texture->GetBitsPerChannel() != 8)
        {
            return false;
        }

        const uint32_t channels = max(texture->GetChannelCount(), 1u);
        const uint32_t color_channels = min(channels, 3u);
        const uint8_t* pixels = reinterpret_cast<const uint8_t*>(mip->bytes.data());
        const size_t pixel_count = mip->bytes.size() / channels;
        for (size_t i = 0; i < pixel_count; i++)
        {
            for (uint32_t c = 0; c < color_channels; c++)
            {
                if (pixels[i * channels + c] > 4)
                {
                    return false;
                }
            }
        }

        return true;
    }

    void clear_request()
    {
        preview_request.type                  = preview_request_type::none;
        preview_request.world_file_path.clear();
        preview_request.preview_path.clear();
        preview_request.waiting_for_load_start  = false;
        preview_request.waiting_for_load_finish = false;
        preview_request.capture_delay_remaining_sec = 0.0;
    }

    string normalize_path(string path)
    {
        path = spartan::FileSystem::GetRelativePath(path);
        replace(path.begin(), path.end(), '\\', '/');

        transform(path.begin(), path.end(), path.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(tolower(c));
            }
        );

        return path;
    }

    string sanitize_name(const string& text)
    {
        string sanitized;
        sanitized.reserve(text.size());

        bool last_was_underscore = false;
        for (unsigned char c : text)
        {
            if (isalnum(c))
            {
                sanitized += static_cast<char>(tolower(c));
                last_was_underscore = false;
            }
            else if (!last_was_underscore)
            {
                sanitized += '_';
                last_was_underscore = true;
            }
        }

        while (!sanitized.empty() && sanitized.front() == '_')
        {
            sanitized.erase(sanitized.begin());
        }

        while (!sanitized.empty() && sanitized.back() == '_')
        {
            sanitized.pop_back();
        }

        return sanitized.empty() ? "world" : sanitized;
    }

    uint64_t compute_hash_fnv1a(const string& text)
    {
        uint64_t hash = 14695981039346656037ull;
        for (unsigned char c : text)
        {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ull;
        }

        return hash;
    }

    string to_hex_string(uint64_t value)
    {
        stringstream stream;
        stream << hex << setw(16) << setfill('0') << value;
        return stream.str();
    }

    string get_preview_directory()
    {
        return string(spartan::ResourceCache::GetProjectDirectory()) + "/previews";
    }

    string get_authored_preview_path(const string& world_file_path)
    {
        // Curated previews ship with the project assets; worlds contain only world XML.
        return get_preview_directory() + "/" +
            spartan::FileSystem::GetFileNameWithoutExtensionFromFilePath(world_file_path) + ".png";
    }

    bool ensure_preview_directory_exists()
    {
        const string directory = get_preview_directory();
        return spartan::FileSystem::Exists(directory) || spartan::FileSystem::CreateDirectory_(directory);
    }

    bool is_current_request_world_loaded()
    {
        switch (preview_request.type)
        {
            case preview_request_type::world_file:
                return normalize_path(spartan::World::GetFilePath()) == normalize_path(preview_request.world_file_path);

            case preview_request_type::none:
            default:
                return false;
        }
    }

    string get_thumbnail_cache_path(const string& preview_path)
    {
        error_code ec_size;
        error_code ec_time;
        const uint64_t file_size = filesystem::file_size(preview_path, ec_size);
        const auto write_time    = filesystem::last_write_time(preview_path, ec_time);
        if (ec_size || ec_time || file_size == 0)
        {
            return {};
        }

        const int64_t ticks = write_time.time_since_epoch().count();
        const string key    = normalize_path(preview_path) + "|" + to_string(file_size) + "|" + to_string(ticks) + "|" + to_string(preview_max_dimension);
        return string(preview_cache_directory) + "/thumb_" + to_hex_string(compute_hash_fnv1a(key)) + ".png";
    }

    void save_thumbnail_cache(const string& cache_path, spartan::RHI_Texture* texture)
    {
        if (cache_path.empty() || !texture || texture->GetChannelCount() != 4 || texture->GetBitsPerChannel() != 8)
        {
            return;
        }

        spartan::RHI_Texture_Mip* mip = texture->GetMip(0, 0);
        if (!mip || mip->bytes.empty())
        {
            return;
        }

        error_code ec;
        filesystem::create_directories(preview_cache_directory, ec);
        if (ec)
        {
            return;
        }

        // write to a unique temp file then rename so concurrent instances never read a partial png
        const string temp_path = cache_path + "." + to_hex_string(compute_hash_fnv1a(cache_path) ^ static_cast<uint64_t>(chrono::steady_clock::now().time_since_epoch().count())) + ".tmp";
        spartan::ImageImporter::SaveSdrRgba8(temp_path, texture->GetWidth(), texture->GetHeight(), mip->bytes.data());
        if (spartan::FileSystem::Exists(temp_path))
        {
            filesystem::rename(temp_path, cache_path, ec);
            if (ec)
            {
                filesystem::remove(temp_path, ec);
            }
        }
    }

    void reap_finished_loads()
    {
        active_loads.erase(
            remove_if(active_loads.begin(), active_loads.end(), [](const future<void>& task)
            {
                return !task.valid() || task.wait_for(chrono::seconds(0)) == future_status::ready;
            }),
            active_loads.end()
        );
    }

    void dispatch_pending_loads()
    {
        if (shutting_down.load())
        {
            return;
        }

        reap_finished_loads();

        while (!pending_loads.empty() && active_loads.size() < max_concurrent_loads)
        {
            queued_load request = move(pending_loads.front());
            pending_loads.pop_front();

            active_loads.emplace_back(spartan::ThreadPool::AddTask([request = move(request)]()
            {
                if (shutting_down.load())
                {
                    return;
                }

                decoded_preview result;
                result.preview_path = request.preview_path;
                result.allow_delete = request.allow_delete;

                if (!spartan::FileSystem::Exists(request.preview_path))
                {
                    lock_guard<mutex> lock(decoded_mutex);
                    decoded_previews.push_back(move(result));
                    return;
                }

                const string cache_path = get_thumbnail_cache_path(request.preview_path);
                shared_ptr<spartan::RHI_Texture> texture = make_shared<spartan::RHI_Texture>();
                texture->SetFlag(spartan::RHI_Texture_DeferUpload);

                bool loaded_from_cache = false;
                if (!cache_path.empty() && spartan::FileSystem::Exists(cache_path))
                {
                    texture->LoadFromFile(cache_path);
                    loaded_from_cache = texture->GetWidth() > 0 && texture->GetHeight() > 0;
                    if (!loaded_from_cache)
                    {
                        error_code ec;
                        filesystem::remove(cache_path, ec);
                    }
                }

                if (!loaded_from_cache)
                {
                    texture = make_shared<spartan::RHI_Texture>();
                    texture->SetFlag(spartan::RHI_Texture_DeferUpload);
                    texture->LoadFromFile(request.preview_path);
                    if (texture->GetWidth() == 0 || texture->GetHeight() == 0 || is_blank_preview(texture.get()))
                    {
                        result.invalid = true;
                        lock_guard<mutex> lock(decoded_mutex);
                        decoded_previews.push_back(move(result));
                        return;
                    }

                    downscale_preview(texture.get());
                    save_thumbnail_cache(cache_path, texture.get());
                }

                result.texture = move(texture);
                lock_guard<mutex> lock(decoded_mutex);
                decoded_previews.push_back(move(result));
            }));
        }
    }

    void process_decoded_previews()
    {
        uint32_t uploaded = 0;
        while (uploaded < max_gpu_uploads_per_frame)
        {
            decoded_preview result;
            {
                lock_guard<mutex> lock(decoded_mutex);
                if (decoded_previews.empty())
                {
                    break;
                }
                result = move(decoded_previews.front());
                decoded_previews.pop_front();
            }

            auto it = preview_entries.find(result.preview_path);
            if (it == preview_entries.end())
            {
                continue;
            }

            if (result.invalid || !result.texture)
            {
                if (result.invalid && result.allow_delete)
                {
                    spartan::FileSystem::Delete(result.preview_path);
                    preview_entries.erase(it);
                }
                else
                {
                    it->second.state = preview_entry_state::failed;
                    it->second.texture.reset();
                }
                continue;
            }

            result.texture->PrepareForGpu();
            if (result.texture->GetResourceState() == spartan::ResourceState::PreparedForGpu && result.texture->GetRhiResource() != nullptr)
            {
                it->second.texture = move(result.texture);
                it->second.state   = preview_entry_state::ready;
                uploaded++;
            }
            else
            {
                it->second.state = preview_entry_state::failed;
                it->second.texture.reset();
            }
        }
    }

    string resolve_preview_path_cached(const string& world_file_path)
    {
        auto it = resolved_preview_paths.find(world_file_path);
        if (it != resolved_preview_paths.end())
        {
            return it->second;
        }

        string resolved = WorldPreviews::GetPreviewPath(world_file_path);
        resolved_preview_paths[world_file_path] = resolved;
        return resolved;
    }

    void request_async_load(const string& preview_path, bool allow_delete)
    {
        auto it = preview_entries.find(preview_path);
        if (it != preview_entries.end())
        {
            return;
        }

        preview_entry entry;
        entry.state        = preview_entry_state::loading;
        entry.allow_delete = allow_delete;
        preview_entries[preview_path] = move(entry);

        pending_loads.push_back({ preview_path, allow_delete });
        dispatch_pending_loads();
    }

    spartan::RHI_Texture* get_texture(const string& preview_path, bool allow_delete)
    {
        auto it = preview_entries.find(preview_path);
        if (it == preview_entries.end())
        {
            request_async_load(preview_path, allow_delete);
            return nullptr;
        }

        if (it->second.state != preview_entry_state::ready || !it->second.texture)
        {
            return nullptr;
        }

        shared_ptr<spartan::RHI_Texture>& texture = it->second.texture;
        if (texture->GetWidth() == 0 || texture->GetHeight() == 0)
        {
            if (allow_delete)
            {
                preview_entries.erase(it);
                spartan::FileSystem::Delete(preview_path);
            }
            else
            {
                it->second.state = preview_entry_state::failed;
                it->second.texture.reset();
            }
            return nullptr;
        }

        if (texture->GetResourceState() == spartan::ResourceState::Max || texture->GetRhiResource() == nullptr)
        {
            texture->PrepareForGpu();
        }

        if (texture->GetResourceState() != spartan::ResourceState::PreparedForGpu || texture->GetRhiResource() == nullptr)
        {
            return nullptr;
        }

        return texture.get();
    }

    void begin_request(preview_request_type type, const string& preview_path, const string& world_file_path = "")
    {
        clear_request();

        if (spartan::FileSystem::Exists(preview_path))
        {
            return;
        }

        if (!ensure_preview_directory_exists())
        {
            return;
        }

        preview_request.type                  = type;
        preview_request.preview_path          = preview_path;
        preview_request.world_file_path       = world_file_path;
        preview_request.waiting_for_load_start  = !spartan::ProgressTracker::IsLoading();
        preview_request.waiting_for_load_finish = !preview_request.waiting_for_load_start;
        preview_request.capture_delay_remaining_sec = 0.0;
    }
}

void WorldPreviews::Tick()
{
    process_decoded_previews();
    dispatch_pending_loads();

    if (preview_request.type == preview_request_type::none)
    {
        return;
    }

    if (spartan::FileSystem::Exists(preview_request.preview_path))
    {
        clear_request();
        return;
    }

    const bool is_loading = spartan::ProgressTracker::IsLoading();

    if (preview_request.waiting_for_load_start)
    {
        if (is_loading)
        {
            preview_request.waiting_for_load_start  = false;
            preview_request.waiting_for_load_finish = true;
            return;
        }

        // very fast loads can complete between ticks, so fall back to the requested world identity
        if (is_current_request_world_loaded())
        {
            preview_request.waiting_for_load_start = false;
            preview_request.capture_delay_remaining_sec = preview_capture_delay_sec;
        }
        else
        {
            return;
        }
    }

    if (preview_request.waiting_for_load_finish)
    {
        if (is_loading)
        {
            return;
        }

        preview_request.waiting_for_load_finish = false;
        if (!is_current_request_world_loaded())
        {
            clear_request();
            return;
        }

        preview_request.capture_delay_remaining_sec = preview_capture_delay_sec;
    }

    if (preview_request.capture_delay_remaining_sec <= 0.0)
    {
        return;
    }

    if (is_loading)
    {
        preview_request.capture_delay_remaining_sec = 0.0;
        preview_request.waiting_for_load_finish = true;
        return;
    }

    preview_request.capture_delay_remaining_sec -= spartan::Timer::GetDeltaTimeRealSec();
    if (preview_request.capture_delay_remaining_sec > 0.0)
    {
        return;
    }

    if (!is_current_request_world_loaded())
    {
        clear_request();
        return;
    }

    if (ensure_preview_directory_exists())
    {
        spartan::Renderer::Screenshot(preview_request.preview_path);
        preview_entries.erase(preview_request.preview_path);
        resolved_preview_paths.erase(preview_request.world_file_path);
    }

    clear_request();
}

void WorldPreviews::Shutdown()
{
    shutting_down.store(true);
    clear_request();
    pending_loads.clear();

    for (future<void>& task : active_loads)
    {
        if (task.valid())
        {
            task.wait();
        }
    }
    active_loads.clear();

    {
        lock_guard<mutex> lock(decoded_mutex);
        decoded_previews.clear();
    }

    spartan::RHI_Device::QueueWaitAll();
    for (auto& [path, entry] : preview_entries)
    {
        if (entry.texture)
        {
            entry.texture->ReleaseGpuResources();
        }
    }

    preview_entries.clear();
    resolved_preview_paths.clear();
    shutting_down.store(false);
}

void WorldPreviews::Prefetch(const string& world_file_path)
{
    const string preview_path = resolve_preview_path_cached(world_file_path);
    request_async_load(preview_path, preview_path != get_authored_preview_path(world_file_path));
}

void WorldPreviews::Invalidate(const string& world_file_path)
{
    auto it = resolved_preview_paths.find(world_file_path);
    if (it != resolved_preview_paths.end())
    {
        preview_entries.erase(it->second);
        resolved_preview_paths.erase(it);
    }
    preview_entries.erase(GetPreviewPath(world_file_path));
}

void WorldPreviews::InvalidateAll()
{
    resolved_preview_paths.clear();
    for (auto it = preview_entries.begin(); it != preview_entries.end(); )
    {
        if (it->second.state == preview_entry_state::failed)
        {
            it = preview_entries.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void WorldPreviews::RequestGeneration(const string& world_file_path)
{
    Invalidate(world_file_path);
    begin_request(preview_request_type::world_file, GetPreviewPath(world_file_path), normalize_path(world_file_path));
}

string WorldPreviews::GetPreviewPath(const string& world_file_path)
{
    const string authored_path = get_authored_preview_path(world_file_path);
    if (spartan::FileSystem::Exists(authored_path))
    {
        return authored_path;
    }

    const string normalized_path = normalize_path(world_file_path);
    const string world_name      = sanitize_name(spartan::FileSystem::GetFileNameWithoutExtensionFromFilePath(world_file_path));
    const string world_hash      = to_hex_string(compute_hash_fnv1a(normalized_path));

    return get_preview_directory() + "/world_" + world_name + "_" + world_hash + ".png";
}

spartan::RHI_Texture* WorldPreviews::GetTexture(const string& world_file_path)
{
    const string preview_path = resolve_preview_path_cached(world_file_path);
    // A corrupt generated cache entry can be rebuilt; never delete authored assets.
    return get_texture(preview_path, preview_path != get_authored_preview_path(world_file_path));
}
