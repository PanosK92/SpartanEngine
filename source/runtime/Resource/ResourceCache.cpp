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

//= INCLUDES ==================
#include "pch.h"
#include "ResourceCache.h"
#include "IconAtlas.h"
#include "../RHI/RHI_Texture.h"
#include "../Rendering/Renderer.h"
#include "../Core/Window.h"
#include "../FileSystem/FileSystem.h"
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstddef>
#include <cstring>
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//=============================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan
{
    namespace
    {
        array<string, 6> m_standard_resource_directories;
        char m_project_directory[256] = {};
        vector<shared_ptr<IResource>> m_resources;
        recursive_mutex m_mutex;
        bool use_root_shader_directory = false;
        unordered_map<string, unique_ptr<mutex>> m_in_flight_mutexes;
        mutex m_in_flight_map_mutex;

        // walks up from the working directory to the repository root, identified by a .git entry, so the
        // root shader directory resolves to the git tracked source no matter where the engine is launched from
        string resolve_root_shader_directory(const string& shaders_relative)
        {
            string current = FileSystem::GetWorkingDirectory();
            for (uint32_t level = 0; level < 16; level++)
            {
                const string candidate = current + "/" + shaders_relative;
                if (FileSystem::Exists(current + "/.git") && FileSystem::IsDirectory(candidate))
                {
                    return candidate;
                }

                const string parent = FileSystem::GetParentDirectory(current);
                if (parent == current)
                {
                    break;
                }
                current = parent;
            }

            return "";
        }
    }

    void ResourceCache::Initialize()
    {
        // create project directory
        SetProjectDirectory("project/");

        // add engine standard resource directories
        const string data_dir = string(GetDataDirectory()) + "/";
        AddResourceDirectory(ResourceDirectory::Environment, string(m_project_directory) + "environment");
        AddResourceDirectory(ResourceDirectory::Fonts, data_dir + "fonts");
        AddResourceDirectory(ResourceDirectory::Icons, data_dir + "icons");
        AddResourceDirectory(ResourceDirectory::ShaderCompiler, data_dir + "shader_compiler");
        AddResourceDirectory(ResourceDirectory::Shaders, data_dir + "shaders");
        AddResourceDirectory(ResourceDirectory::Textures, data_dir + "textures");
    }

    void ResourceCache::Shutdown()
    {
        // clear texture references from materials owned by renderer before destroying cached textures
        // this prevents dangling pointers since those materials outlive ResourceCache resources
        Renderer::ClearMaterialTextureReferences();

        uint32_t resource_count = static_cast<uint32_t>(m_resources.size());
        m_resources.clear();
        if (resource_count != 0)
        {
            SP_LOG_INFO("%d resources have been cleared", resource_count);
        }
    }

    void ResourceCache::LoadDefaultResources()
    {
        Window::PumpEvents();
        IconAtlas::Build();
        Window::PumpEvents();
    }

    void ResourceCache::UnloadDefaultResources()
    {
        IconAtlas::Shutdown();
    }

    shared_ptr<IResource>& ResourceCache::GetByName(const string& name, const ResourceType type)
    {
        lock_guard<recursive_mutex> guard(m_mutex);
        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (name == resource->GetObjectName() && (type == ResourceType::Max || resource->GetResourceType() == type))
                return resource;
        }
        static shared_ptr<IResource> empty;
        return empty;
    }

    vector<shared_ptr<IResource>> ResourceCache::GetByType(const ResourceType type /*= ResourceType::Unknown*/)
    {
        lock_guard<recursive_mutex> guard(m_mutex);
        vector<shared_ptr<IResource>> resources;
        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource->GetResourceType() == type || type == ResourceType::Max)
            {
                resources.emplace_back(resource);
            }
        }
        return resources;
    }

    uint64_t ResourceCache::GetMemoryUsage(ResourceType type /*= Resource_Unknown*/)
    {
        lock_guard<recursive_mutex> guard(m_mutex);
        uint64_t size = 0;
        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource->GetResourceType() == type || type == ResourceType::Max)
            {
                if (SpartanObject* object = dynamic_cast<SpartanObject*>(resource.get()))
                {
                    size += object->GetObjectSize();
                }
            }
        }
        return size;
    }

    uint32_t ResourceCache::GetResourceCount(const ResourceType type)
    {
        return static_cast<uint32_t>(GetByType(type).size());
    }

    void ResourceCache::AddResourceDirectory(const ResourceDirectory type, const string& directory)
    {
        m_standard_resource_directories[static_cast<uint32_t>(type)] = directory;
    }

    string ResourceCache::GetResourceDirectory(const ResourceDirectory resource_directory_type)
    {
        string directory = m_standard_resource_directories[static_cast<uint32_t>(resource_directory_type)];
        if (use_root_shader_directory && resource_directory_type == ResourceDirectory::Shaders)
        {
            // resolve against the repository root so it works from any working directory, fall back to the local copy
            const string root_directory = resolve_root_shader_directory(directory);
            if (!root_directory.empty())
            {
                return root_directory;
            }
        }
        return directory;
    }

    void ResourceCache::SetProjectDirectory(const char* directory)
    {
        if (!FileSystem::Exists(directory))
        {
            FileSystem::CreateDirectory_(directory);
        }

        strcpy_s(m_project_directory, sizeof(m_project_directory), directory);
        m_project_directory[sizeof(m_project_directory) - 1] = '\0'; // ensure null-termination
    }

    string ResourceCache::GetProjectDirectoryAbsolute()
    {
        return FileSystem::GetWorkingDirectory() + "/" + m_project_directory;
    }

    const char* ResourceCache::GetProjectDirectory()
    {
        return m_project_directory;
    }

    const char* ResourceCache::GetDataDirectory()
    {
        #ifdef _WIN32
        return "Data";
        #else
        return "data";
        #endif
    }

    vector<shared_ptr<IResource>>& ResourceCache::GetResources()
    {
        return m_resources;
    }

    recursive_mutex& ResourceCache::GetMutex()
    {
        return m_mutex;
    }

    mutex& ResourceCache::GetInFlightMutex(const string& path)
    {
        lock_guard<mutex> lock(m_in_flight_map_mutex);
        auto it = m_in_flight_mutexes.find(path);
        if (it == m_in_flight_mutexes.end())
        {
            it = m_in_flight_mutexes.emplace(path, make_unique<mutex>()).first;
        }
        return *it->second;
    }

    bool ResourceCache::GetUseRootShaderDirectory()
    {
        return use_root_shader_directory;
    }

    void ResourceCache::SetUseRootShaderDirectory(const bool _use_root_shader_directory)
    {
        use_root_shader_directory = _use_root_shader_directory;
    }

    const Icon& ResourceCache::GetIcon(IconType type)
    {
        return IconAtlas::Get(type);
    }

    // gui icon atlas, defined here so it lives in an existing translation unit
    namespace
    {
        shared_ptr<RHI_Texture>       atlas_texture;
        unordered_map<IconType, Icon> atlas_icons;
        Icon                          atlas_fallback;

        constexpr uint32_t atlas_width = 1024; // fixed width, height grows to fit
        constexpr uint32_t atlas_pad   = 2;    // gutter between icons to avoid bilinear bleeding

        // a decoded source icon waiting to be packed
        struct source_icon
        {
            IconType        type   = IconType::Undefined;
            uint32_t        width  = 0;
            uint32_t        height = 0;
            vector<uint8_t> rgba; // tightly packed rgba8
            uint32_t        x      = 0;
            uint32_t        y      = 0;
        };

        // decodes an image file to rgba8 cpu pixels without uploading it to the gpu
        bool load_icon_pixels(const string& path, source_icon& out)
        {
            if (!FileSystem::Exists(path))
            {
                SP_LOG_WARNING("icon atlas: \"%s\" doesn't exist", path.c_str());
                return false;
            }

            RHI_Texture temp;
            temp.SetFlags(RHI_Texture_Srv | RHI_Texture_DeferUpload); // defer skips the auto gpu upload, we only want cpu bytes
            temp.LoadFromFile(path);

            const uint32_t width    = temp.GetWidth();
            const uint32_t height   = temp.GetHeight();
            const uint32_t channels = temp.GetChannelCount();
            if (width == 0 || height == 0 || !temp.HasData() || temp.GetBytesPerChannel() != 1)
            {
                SP_LOG_WARNING("icon atlas: \"%s\" has an unsupported pixel layout", path.c_str());
                return false;
            }

            RHI_Texture_Mip* mip = temp.GetMip(0, 0);
            if (!mip)
            {
                return false;
            }

            const uint8_t* src = reinterpret_cast<const uint8_t*>(mip->bytes.data());
            out.width          = width;
            out.height         = height;
            out.rgba.resize(static_cast<size_t>(width) * height * 4);

            const uint32_t pixel_count = width * height;
            for (uint32_t i = 0; i < pixel_count; i++)
            {
                uint8_t r = 0, g = 0, b = 0, a = 255;
                if (channels >= 4)
                {
                    r = src[i * channels + 0];
                    g = src[i * channels + 1];
                    b = src[i * channels + 2];
                    a = src[i * channels + 3];
                }
                else if (channels == 3)
                {
                    r = src[i * channels + 0];
                    g = src[i * channels + 1];
                    b = src[i * channels + 2];
                }
                else // grayscale
                {
                    r = g = b = src[i * channels + 0];
                }

                out.rgba[i * 4 + 0] = r;
                out.rgba[i * 4 + 1] = g;
                out.rgba[i * 4 + 2] = b;
                out.rgba[i * 4 + 3] = a;
            }

            return true;
        }
    }

    void IconAtlas::Build()
    {
        Shutdown();

        const vector<string> data_dirs =
        {
            string(ResourceCache::GetDataDirectory()) + "/",
            "data/",
            "Data/",
            "../data/",
            "../Data/"
        };

        // single source of truth, icon type to source file
        const vector<pair<IconType, string>> table =
        {
            { IconType::Console,       "icons/console.png"          },
            { IconType::File,          "icons/file.png"             },
            { IconType::Folder,        "icons/folder.png"           },
            { IconType::Model,         "icons/model.png"            },
            { IconType::World,         "icons/world.png"            },
            { IconType::Material,      "icons/material.png"         },
            { IconType::Shader,        "icons/code.png"             },
            { IconType::Xml,           "icons/xml.png"              },
            { IconType::Dll,           "icons/dll.png"              },
            { IconType::Txt,           "icons/txt.png"              },
            { IconType::Ini,           "icons/ini.png"              },
            { IconType::Exe,           "icons/exe.png"              },
            { IconType::Font,          "icons/font.png"             },
            { IconType::Screenshot,    "icons/screenshot.png"       },
            { IconType::Gear,          "icons/gear.png"             },
            { IconType::Play,          "icons/play.png"             },
            { IconType::Profiler,      "icons/timer.png"            },
            { IconType::ResourceCache, "icons/resource_viewer.png"  },
            { IconType::RenderDoc,     "icons/renderdoc.png"        },
            { IconType::Texture,       "icons/texture.png"          },
            { IconType::Minimize,      "icons/window_minimise.png"  },
            { IconType::Maximize,      "icons/window_maximise.png"  },
            { IconType::X,             "icons/window_close.png"     },
            { IconType::Entity,        "icons/entity.png"           },
            { IconType::Hybrid,        "icons/hybrid.png"           },
            { IconType::Audio,         "icons/audio.png"            },
            { IconType::Terrain,       "icons/terrain.png"          },
            { IconType::Light,         "icons/light.png"            },
            { IconType::Camera,        "icons/camera.png"           },
            { IconType::Particle,      "icons/particle.png"         },
            { IconType::Physics,       "icons/physics.png"          },
            { IconType::Compressed,    "icons/compressed.png"       },
            { IconType::ArrowLeft,     "icons/arrow_left.png"       },
            { IconType::ArrowRight,    "icons/arrow_right.png"      },
            { IconType::ArrowUp,       "icons/arrow_up.png"         },
            { IconType::Refresh,       "icons/refresh.png"          },
            { IconType::Logo,          "logo.ico"                   },
            { IconType::Mcp,           "icons/mcp.png"              },
            { IconType::Snap,          "icons/snap.png"             }
        };

        // decode every source icon
        vector<source_icon> sources;
        sources.reserve(table.size());
        for (const auto& [type, file] : table)
        {
            source_icon icon;
            icon.type = type;

            string file_path;
            for (const string& data_dir : data_dirs)
            {
                const string candidate = data_dir + file;
                if (FileSystem::Exists(candidate))
                {
                    file_path = candidate;
                    break;
                }
            }

            if (!file_path.empty() && load_icon_pixels(file_path, icon))
            {
                sources.push_back(move(icon));
            }
        }

        if (sources.empty())
        {
            SP_LOG_ERROR("icon atlas: no icons were loaded");
            return;
        }

        // shelf pack, tallest first for tighter rows
        sort(sources.begin(), sources.end(), [](const source_icon& a, const source_icon& b)
        {
            return a.height > b.height;
        });

        uint32_t cursor_x     = atlas_pad;
        uint32_t cursor_y     = atlas_pad;
        uint32_t shelf_height = 0;
        for (source_icon& icon : sources)
        {
            if (cursor_x + icon.width + atlas_pad > atlas_width)
            {
                cursor_x     = atlas_pad;
                cursor_y    += shelf_height + atlas_pad;
                shelf_height = 0;
            }

            icon.x       = cursor_x;
            icon.y       = cursor_y;
            cursor_x    += icon.width + atlas_pad;
            shelf_height = max(shelf_height, icon.height);
        }
        const uint32_t atlas_height = cursor_y + shelf_height + atlas_pad;

        // blit into a transparent rgba8 buffer
        vector<byte> pixels(static_cast<size_t>(atlas_width) * atlas_height * 4, byte{0});
        for (const source_icon& icon : sources)
        {
            for (uint32_t row = 0; row < icon.height; row++)
            {
                byte* dst        = pixels.data() + (static_cast<size_t>(icon.y + row) * atlas_width + icon.x) * 4;
                const uint8_t* s = icon.rgba.data() + static_cast<size_t>(row) * icon.width * 4;
                memcpy(dst, s, static_cast<size_t>(icon.width) * 4);
            }
        }

        // upload the packed atlas as a single gpu texture
        vector<RHI_Texture_Slice> data;
        data.emplace_back().mips.emplace_back().bytes = move(pixels);
        atlas_texture = make_shared<RHI_Texture>(
            RHI_Texture_Type::Type2D, atlas_width, atlas_height, 1, 1,
            RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv, "gui_icon_atlas", data
        );

        // resolve uv rects
        const float inv_w = 1.0f / static_cast<float>(atlas_width);
        const float inv_h = 1.0f / static_cast<float>(atlas_height);
        for (const source_icon& icon : sources)
        {
            Icon entry;
            entry.texture    = atlas_texture.get();
            entry.uv_min     = math::Vector2(icon.x * inv_w, icon.y * inv_h);
            entry.uv_max     = math::Vector2((icon.x + icon.width) * inv_w, (icon.y + icon.height) * inv_h);
            atlas_icons[icon.type] = entry;
        }

        // fallback used when a requested icon is missing
        auto it_file   = atlas_icons.find(IconType::File);
        atlas_fallback = it_file != atlas_icons.end() ? it_file->second : atlas_icons.begin()->second;
    }

    void IconAtlas::Shutdown()
    {
        atlas_icons.clear();
        atlas_fallback = Icon();
        atlas_texture.reset();
    }

    const Icon& IconAtlas::Get(IconType type)
    {
        auto it = atlas_icons.find(type);
        if (it != atlas_icons.end())
        {
            return it->second;
        }

        return atlas_fallback;
    }

    RHI_Texture* IconAtlas::GetTexture()
    {
        return atlas_texture.get();
    }
}
