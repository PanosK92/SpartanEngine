/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========================
#include "pch.h"
#include "Window.h"
#include "../rendering/Renderer.h"
#include "../resource/ResourceCache.h"
#include "../input/Input.h"
#include "../xr/Xr.h"
SP_WARNINGS_OFF
#include "../io/pugixml.hpp"
SP_WARNINGS_ON
//====================================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan
{
    namespace
    {
        // device, instance and query pool creation depend on these, so they are frozen after the pre-init load
        unordered_map<const CVarVariant*, CVarVariant> startup_values;

        void startup_only(const CVarVariant& value)
        {
            auto it = startup_values.find(&value);
            if (it != startup_values.end() && value != it->second)
            {
                const_cast<CVarVariant&>(value) = it->second;
                SP_LOG_WARNING("This debug setting is read at startup, edit spartan.xml and restart");
            }
        }
    }

    TConsoleVar<bool> cvar_debug_validation_layer("debug.validation_layer", false, "api validation layer for error detection and debug messages", startup_only);
    TConsoleVar<bool> cvar_debug_gpu_assisted_validation("debug.gpu_assisted_validation", false, "gpu-based validation, extremely slow and breaks on first error", startup_only);
    TConsoleVar<bool> cvar_debug_log_to_file("debug.log_to_file", false, "write diagnostic and validation messages to a persistent log file");
    TConsoleVar<bool> cvar_debug_breadcrumbs("debug.breadcrumbs", false, "record gpu execution markers to find the cause of gpu crashes", startup_only);
    TConsoleVar<bool> cvar_debug_renderdoc("debug.renderdoc", false, "renderdoc integration for frame capture, disables ray tracing", startup_only);
    TConsoleVar<bool> cvar_debug_gpu_marking("debug.gpu_marking", false, "gpu debug markers, enable only while capturing with an external gpu debugger", startup_only);
    TConsoleVar<bool> cvar_debug_gpu_timing("debug.gpu_timing", true, "gpu timestamp queries for profiling", startup_only);
    TConsoleVar<bool> cvar_debug_shader_optimization("debug.shader_optimization", true, "shader compiler optimizations, applies to shaders compiled afterwards");
    TConsoleVar<bool> cvar_debug_steam("debug.steam", false, "initialize steamworks", startup_only);
    TConsoleVar<bool> cvar_debug_d3d12_enhanced_barriers("debug.d3d12_enhanced_barriers", false, "d3d12 only, submit barriers through ID3D12GraphicsCommandList7, see D3D12_Barriers.cpp for the interop prerequisites", startup_only);

    namespace
    {
        bool m_has_loaded_user_settings = false;
        string file_path                = "spartan.xml";

        void resolve_file_path()
        {
            // already resolved to an absolute path from a previous call
            if (file_path.size() > 2 &&
                (file_path[1] == ':' || file_path[0] == '/' || file_path[0] == '\\'))
            {
                if (FileSystem::Exists(file_path))
                {
                    return;
                }
            }

            // prefer the xml next to the exe, cwd differs between vs and a double click
            const string exe_dir = FileSystem::GetExecutableDirectory();
            const string exe_xml = exe_dir + "/spartan.xml";
            if (FileSystem::Exists(exe_xml))
            {
                file_path = exe_xml;
                return;
            }

            const string cwd_xml = FileSystem::GetWorkingDirectory() + "/spartan.xml";
            if (FileSystem::Exists(cwd_xml))
            {
                file_path = cwd_xml;
                return;
            }

            // walk up from the exe looking for binaries/spartan.xml
            string current = exe_dir;
            for (uint32_t level = 0; level < 16; level++)
            {
                const string candidate = current + "/binaries/spartan.xml";
                if (FileSystem::Exists(candidate))
                {
                    file_path = candidate;
                    return;
                }

                const string parent = FileSystem::GetParentDirectory(current);
                if (parent == current)
                {
                    break;
                }
                current = parent;
            }

            // last resort, keep a writable default next to the exe
            file_path = exe_xml;
        }

        // helper to convert cvar name to xml-safe name (e.g., "r.bloom" -> "r_bloom")
        string cvar_name_to_xml(const char* name)
        {
            string result(name);
            for (char& c : result)
            {
                if (c == '.')
                {
                    c = '_';
                }
            }
            return result;
        }

        bool is_render_cvar(string_view name)
        {
            return name.size() >= 2 && name[0] == 'r' && name[1] == '.';
        }

        bool is_debug_cvar(string_view name)
        {
            return name.starts_with("debug.");
        }

        void load_debug_cvars(const pugi::xml_node& root)
        {
            for (const auto& [name, cvar] : ConsoleRegistry::Get().GetAll())
            {
                if (is_debug_cvar(name))
                {
                    if (pugi::xml_node child = root.child(cvar_name_to_xml(string(name).c_str()).c_str()))
                    {
                        ConsoleRegistry::Get().SetValueFromString(name, child.text().as_string());
                    }
                }
            }
        }

        void lock_startup_cvars()
        {
            for (const auto& [name, cvar] : ConsoleRegistry::Get().GetAll())
            {
                if (is_debug_cvar(name) && cvar.m_on_change == startup_only)
                {
                    startup_values[cvar.m_value_ptr] = *cvar.m_value_ptr;
                }
            }
        }

        void save()
        {
            pugi::xml_document doc;

            // write settings
            pugi::xml_node root = doc.append_child("Settings");
            {
                root.append_child("FullScreen").text().set(Window::IsFullScreen());
                root.append_child("IsMouseVisible").text().set(Input::GetMouseCursorVisible());

                // never persist hmd eye resolution, that poisons the next non-vr launch
                uint32_t output_w = static_cast<uint32_t>(Renderer::GetResolutionOutput().x);
                uint32_t output_h = static_cast<uint32_t>(Renderer::GetResolutionOutput().y);
                uint32_t render_w = static_cast<uint32_t>(Renderer::GetResolutionRender().x);
                uint32_t render_h = static_cast<uint32_t>(Renderer::GetResolutionRender().y);
                float viewport_w  = 0.0f;
                float viewport_h  = 0.0f;
                if (!Xr::TryGetPersistedDesktopResolution(output_w, output_h, render_w, render_h, viewport_w, viewport_h))
                {
                    if (Xr::GetStereoMode())
                    {
                        output_w = Window::GetWidthInPixels();
                        output_h = Window::GetHeightInPixels();
                        render_w = 1920;
                        render_h = 1080;
                    }
                }

                root.append_child("ResolutionOutputWidth").text().set(output_w);
                root.append_child("ResolutionOutputHeight").text().set(output_h);
                root.append_child("ResolutionRenderWidth").text().set(render_w);
                root.append_child("ResolutionRenderHeight").text().set(render_h);
                root.append_child("FPSLimit").text().set(Timer::GetFpsLimit());
                for (const auto& [name, cvar] : ConsoleRegistry::Get().GetAll())
                {
                    // Diagnostic transport views are temporary, never a startup setting.
                    if (name == "r.fog.debug")
                    {
                        continue;
                    }
                    if (is_render_cvar(name) || is_debug_cvar(name))
                    {
                        pugi::xml_text text = root.append_child(cvar_name_to_xml(string(name).c_str()).c_str()).text();
                        if (name == "r.resolution_scale" && cvar_dynamic_resolution.GetValueAs<bool>())
                        {
                            text.set(1.0f);
                        }
                        else
                        {
                            // render cvars can hold any of the supported console variable types
                            visit([&text](const auto& value)
                            {
                                if constexpr (is_same_v<decay_t<decltype(value)>, string>)
                                {
                                    text.set(value.c_str());
                                }
                                else
                                {
                                    text.set(value);
                                }
                            }, *cvar.m_value_ptr);
                        }
                    }
                }

                root.append_child("UseRootShaderDirectory").text().set(ResourceCache::GetUseRootShaderDirectory());
            }

            if (!doc.save_file(file_path.c_str()))
            {
                SP_LOG_ERROR("Failed to save settings file: %s", file_path.c_str());
            }
        }

        void load()
        {
            // attempt to load file
            pugi::xml_document doc;
            if (!doc.load_file(file_path.c_str()))
            {
                SP_LOG_ERROR("Failed to load XML file");
                return;
            }

            pugi::xml_node root = doc.child("Settings");

            // load settings
            {
                if ((root.child("FullScreen").text().as_bool()))
                {
                    Window::FullScreen();
                }

                Input::SetMouseCursorVisible(root.child("IsMouseVisible").text().as_bool());
                Timer::SetFpsLimit(root.child("FPSLimit").text().as_float());

                int render_w = root.child("ResolutionRenderWidth").text().as_int();
                int render_h = root.child("ResolutionRenderHeight").text().as_int();
                int output_w = root.child("ResolutionOutputWidth").text().as_int();
                int output_h = root.child("ResolutionOutputHeight").text().as_int();

                // reject leftover openxr eye sizes (near-square, huge) so a bad save cannot stick
                if (output_w > 0 && output_h > 0)
                {
                    const float aspect = static_cast<float>(output_w) / static_cast<float>(output_h);
                    if (aspect > 0.85f && aspect < 1.15f && output_w >= 3000)
                    {
                        SP_LOG_WARNING(
                            "settings: discarding poisoned hmd resolution %dx%d, using window size",
                            output_w,
                            output_h
                        );
                        output_w = static_cast<int>(Window::GetWidthInPixels());
                        output_h = static_cast<int>(Window::GetHeightInPixels());
                        render_w = 1920;
                        render_h = 1080;
                    }
                }

                Renderer::SetResolutionRender(render_w, render_h);
                Renderer::SetResolutionOutput(output_w, output_h);
                bool dynamic_resolution = root.child("r_dynamic_resolution").text().as_bool();

                // Older builds persisted these short-lived grass defaults. Let the renderer's
                // new defaults replace that exact pair; preserve deliberately customized values.
                const bool legacy_grass_tracks =
                    root.child("r_grass_track_radius").text().as_float() == 8.0f &&
                    root.child("r_grass_track_recovery").text().as_float() == 1.5f;

                // load render options from xml
                for (const auto& [name, cvar] : ConsoleRegistry::Get().GetAll())
                {
                    if (legacy_grass_tracks &&
                        (name == "r.grass_track_radius" || name == "r.grass_track_recovery"))
                    {
                        continue;
                    }
                    // Ignore diagnostic views left in settings by older builds.
                    if (name == "r.fog.debug")
                    {
                        cvar_fog_debug.SetValue(0.0f);
                        continue;
                    }
                    if (is_render_cvar(name))
                    {
                        pugi::xml_node child = root.child(cvar_name_to_xml(string(name).c_str()).c_str());
                        if (child)
                        {
                            if (name == "r.resolution_scale" && dynamic_resolution)
                            {
                                ConsoleRegistry::Get().SetValueFromString(string(name).c_str(), "1.0");
                            }
                            else
                            {
                                ConsoleRegistry::Get().SetValueFromString(string(name).c_str(), child.text().as_string());
                            }
                        }
                    }
                }

                // this setting can be mapped directly to the resource cache (no need to wait for it to initialize)
                if (pugi::xml_node use_root_shader_directory = root.child("UseRootShaderDirectory"))
                {
                    ResourceCache::SetUseRootShaderDirectory(use_root_shader_directory.text().as_bool());
                }
            }

            m_has_loaded_user_settings = true;
        }
    }

    void Settings::LoadPreInitSettings()
    {
        resolve_file_path();

        pugi::xml_document doc;
        if (FileSystem::Exists(file_path) && doc.load_file(file_path.c_str()))
        {
            pugi::xml_node root = doc.child("Settings");
            if (pugi::xml_node use_root_shader_directory = root.child("UseRootShaderDirectory"))
            {
                ResourceCache::SetUseRootShaderDirectory(use_root_shader_directory.text().as_bool());
            }

            load_debug_cvars(root);
        }

        lock_startup_cvars();
    }

    void Settings::Initialize()
    {
        resolve_file_path();
        if (FileSystem::Exists(file_path))
        {
            load();
        }
    }
    
    void Settings::Shutdown()
    {
        save();
    }

    bool Settings::HasLoadedUserSettingsFromFile()
    {
        return m_has_loaded_user_settings;
    }
}
