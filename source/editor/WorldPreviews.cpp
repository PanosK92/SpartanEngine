#include "pch.h"
#include "WorldPreviews.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <memory>
#include <sstream>
#include <unordered_map>
#include "Core/ProgressTracker.h"
#include "Core/Timer.h"
#include "FileSystem/FileSystem.h"
#include "Rendering/Renderer.h"
#include "Resource/ResourceCache.h"
#include "RHI/RHI_Device.h"
#include "RHI/RHI_Texture.h"
#include "World/World.h"

using namespace std;

namespace
{
    enum class preview_request_type
    {
        none,
        default_world,
        world_file
    };

    struct preview_request_state
    {
        preview_request_type type = preview_request_type::none;
        spartan::DefaultWorld default_world = spartan::DefaultWorld::Max;
        string world_file_path;
        string preview_path;
        bool waiting_for_load_start = false;
        bool waiting_for_load_finish = false;
        double capture_delay_remaining_sec = 0.0;
    };

    preview_request_state preview_request;
    unordered_map<string, shared_ptr<spartan::RHI_Texture>> preview_textures;
    constexpr double preview_capture_delay_sec = 1.0;

    void clear_request()
    {
        preview_request.type                  = preview_request_type::none;
        preview_request.default_world         = spartan::DefaultWorld::Max;
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

    bool ensure_preview_directory_exists()
    {
        const string directory = get_preview_directory();
        return spartan::FileSystem::Exists(directory) || spartan::FileSystem::CreateDirectory_(directory);
    }

    const char* get_default_world_name(spartan::DefaultWorld default_world)
    {
        switch (default_world)
        {
            case spartan::DefaultWorld::Forest:   return "forest";
            case spartan::DefaultWorld::Sponza:   return "sponza";
            case spartan::DefaultWorld::Test:     return "test";
            case spartan::DefaultWorld::Empty:    return "empty";
            case spartan::DefaultWorld::Max:      return "unknown";
        }

        return "unknown";
    }

    bool is_current_request_world_loaded()
    {
        switch (preview_request.type)
        {
            case preview_request_type::default_world:
                return spartan::Game::GetLoadedWorld() == preview_request.default_world;

            case preview_request_type::world_file:
                return normalize_path(spartan::World::GetFilePath()) == normalize_path(preview_request.world_file_path);

            case preview_request_type::none:
            default:
                return false;
        }
    }

    spartan::RHI_Texture* get_texture(const string& preview_path)
    {
        if (!spartan::FileSystem::Exists(preview_path))
        {
            return nullptr;
        }

        auto invalidate_preview = [&](const string& path)
        {
            preview_textures.erase(path);

            if (spartan::FileSystem::Exists(path))
            {
                spartan::FileSystem::Delete(path);
            }
        };

        auto it = preview_textures.find(preview_path);
        if (it != preview_textures.end())
        {
            shared_ptr<spartan::RHI_Texture> texture = it->second;
            if (!texture)
            {
                preview_textures.erase(it);
                return nullptr;
            }

            if (texture->GetWidth() == 0 || texture->GetHeight() == 0)
            {
                invalidate_preview(preview_path);
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

        shared_ptr<spartan::RHI_Texture> texture = make_shared<spartan::RHI_Texture>(preview_path);
        if (!texture)
        {
            return nullptr;
        }

        if (texture->GetWidth() == 0 || texture->GetHeight() == 0)
        {
            invalidate_preview(preview_path);
            return nullptr;
        }

        preview_textures[preview_path] = texture;
        if (texture->GetResourceState() != spartan::ResourceState::PreparedForGpu || texture->GetRhiResource() == nullptr)
        {
            return nullptr;
        }

        return texture.get();
    }

    void begin_request(preview_request_type type, const string& preview_path, const string& world_file_path = "", spartan::DefaultWorld default_world = spartan::DefaultWorld::Max)
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
        preview_request.default_world         = default_world;
        preview_request.waiting_for_load_start  = !spartan::ProgressTracker::IsLoading();
        preview_request.waiting_for_load_finish = !preview_request.waiting_for_load_start;
        preview_request.capture_delay_remaining_sec = 0.0;
    }
}

void WorldPreviews::Tick()
{
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

    preview_request.capture_delay_remaining_sec -= spartan::Timer::GetDeltaTimeSec();
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
    }

    clear_request();
}

void WorldPreviews::Shutdown()
{
    clear_request();

    spartan::RHI_Device::QueueWaitAll();
    for (auto& [path, texture] : preview_textures)
    {
        if (texture)
        {
            texture->DestroyResourceImmediate();
        }
    }

    preview_textures.clear();
}

void WorldPreviews::RequestGeneration(spartan::DefaultWorld default_world)
{
    begin_request(preview_request_type::default_world, GetPreviewPath(default_world), "", default_world);
}

void WorldPreviews::RequestGeneration(const string& world_file_path)
{
    begin_request(preview_request_type::world_file, GetPreviewPath(world_file_path), normalize_path(world_file_path));
}

string WorldPreviews::GetPreviewPath(spartan::DefaultWorld default_world)
{
    return get_preview_directory() + "/default_" + get_default_world_name(default_world) + ".png";
}

string WorldPreviews::GetPreviewPath(const string& world_file_path)
{
    const string normalized_path = normalize_path(world_file_path);
    const string world_name      = sanitize_name(spartan::FileSystem::GetFileNameWithoutExtensionFromFilePath(world_file_path));
    const string world_hash      = to_hex_string(compute_hash_fnv1a(normalized_path));

    return get_preview_directory() + "/world_" + world_name + "_" + world_hash + ".png";
}

spartan::RHI_Texture* WorldPreviews::GetTexture(spartan::DefaultWorld default_world)
{
    return get_texture(GetPreviewPath(default_world));
}

spartan::RHI_Texture* WorldPreviews::GetTexture(const string& world_file_path)
{
    return get_texture(GetPreviewPath(world_file_path));
}
