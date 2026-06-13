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

//= INCLUDES =====================
#include "pch.h"
#include "GeneralWindows.h"
#include "WorldPreviews.h"
#include "Windows/WorldSelector.h"
#include "Windows/Contributors.h"
#include "ImGui/Source/imgui.h"
#include "ImGui/ImGui_Extension.h"
#include "FileSystem/FileSystem.h"
#include "Widgets/Viewport.h"
#include "Input/Input.h"
// third party version queries
SP_WARNINGS_OFF
#include <assimp/version.h>
#include <FreeImage/FreeImage.h>
#include <freetype/freetype.h>
#include <SDL3/SDL_version.h>
#include <lua/lua.h>
#include <meshoptimizer/meshoptimizer.h>
#include <openxr/openxr.h>
#include <physx/foundation/PxPhysicsVersion.h>
#include <sol/sol.hpp>
#include "IO/pugixml.hpp"
#if defined(_WIN32)
#include <xess/xess.h>
#endif
#if defined(API_GRAPHICS_VULKAN)
#include <vulkan/vulkan_core.h>
#include <spirv_cross/spirv_cross_c.h>
#endif
SP_WARNINGS_ON
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    Editor* editor = nullptr;

    static void center_next_window(Editor* editor)
    {
        ImGui::SetNextWindowPos(
            editor->GetWidget<Viewport>()->GetCenter(),
            ImGuiCond_Appearing, // use appearing so the user can move it if they want
            ImVec2(0.5f, 0.5f)
        );
    }

    // merged welcome window (combines introduction and sponsor into one)
    namespace welcome
    {
        bool visible = true;

        void window()
        {
            if (!visible)
            {
                return;
            }

            ImGui::SetNextWindowPos(
                editor->GetWidget<Viewport>()->GetCenter(),
                ImGuiCond_Appearing,
                ImVec2(0.5f, 0.5f)
            );

            if (ImGui::Begin("Welcome", &visible, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
            {
                float content_width = 500.0f * spartan::Window::GetDpiScale();

                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + content_width);

                // introduction section
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[ Note ]");
                ImGui::SameLine();
                ImGui::Text("This isn't an engine for the average user.");
                ImGui::Spacing();
                ImGui::Text("It is designed for advanced research, ideal for game engine and rendering engineers.");

                ImGui::PopTextWrapPos();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // sponsor section
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + content_width);
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ Support ]");
                ImGui::SameLine();
                ImGui::Text("I cover the costs for hosting and bandwidth of engine assets.");
                ImGui::Spacing();
                ImGui::Text("If you enjoy the simplicity of running a single script, build, run and have everything just work, please consider sponsoring to help keep everything running smoothly!");
                ImGui::PopTextWrapPos();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // buttons
                float button_width = 140.0f;
                float total_width  = button_width * 2 + ImGui::GetStyle().ItemSpacing.x;
                float offset_x     = (ImGui::GetContentRegionAvail().x - total_width) * 0.5f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);

                if (ImGui::Button("I Understand", ImVec2(button_width, 0)))
                {
                    visible = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Sponsor", ImVec2(button_width, 0)))
                {
                    spartan::FileSystem::OpenUrl("https://github.com/sponsors/PanosK92");
                }
            }
            ImGui::End();
        }
    }

    namespace about
    {
        bool visible = false;

        const char* license_text =
            "MIT License"
            "\n\n"
            "Copyright(c) 2015-2026 Panos Karabelas"
             "\n\n"
            "Permission is hereby granted, free of charge, to any person obtaining a copy"
            "of this software and associated documentation files (the \"Software\"), to deal"
            "in the Software without restriction, including without limitation the rights"
            "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell"
            "copies of the Software, and to permit persons to whom the Software is"
            "furnished to do so, subject to the following conditions:"
            "\n\n"
            "The above copyright notice and this permission notice shall be included in all"
            "copies or substantial portions of the Software."
            "\n\n"
            "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR"
            "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
            "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE"
            "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER"
            "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
            "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.";

        void personal_details()
        {
            ImGui::BeginGroup();
            {
                // shift text that the buttons and the text align
                static const float y_shift = 6.0f;

                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                ImGui::Text("Creator");

                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - y_shift);
                if (ImGuiSp::button("Panos Karabelas"))
                {
                    spartan::FileSystem::OpenUrl("https://panoskarabelas.com/");
                }

                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - y_shift);
                if (ImGuiSp::button("GitHub"))
                {
                    spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine");
                }

                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - y_shift);
                if (ImGuiSp::button("X"))
                {
                    spartan::FileSystem::OpenUrl("https://twitter.com/panoskarabelas");
                }
            }
            ImGui::EndGroup();
        }

       void tab_general()
        {
            // --- Top Section: Creator & Links (Fixed Height) ---
            ImGui::Text("Creator");
            ImGui::SameLine();
            ImGui::Spacing();

            ImGui::AlignTextToFramePadding();

            if (ImGuiSp::button("Panos Karabelas"))
            {
                spartan::FileSystem::OpenUrl("https://panoskarabelas.com/");
            }

            ImGui::SameLine();
            if (ImGuiSp::button("GitHub"))
            {
                spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine");
            }

            ImGui::SameLine();
            if (ImGuiSp::button("X"))
            {
                spartan::FileSystem::OpenUrl("https://twitter.com/panoskarabelas");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // --- Bottom Section: License (Dynamic Height) ---
            ImGui::Text("License");

            // Push a darker background color for the text area
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);

            // ImVec2(0.0f, -FLT_MIN) tells ImGui:
            // X = 0.0f      -> "Use all available width"
            // Y = -FLT_MIN  -> "Use all remaining vertical space"
            if (ImGui::BeginChild("license_scroll", ImVec2(0.0f, -FLT_MIN), true))
            {
                // Use GetContentRegionAvail().x to ensure text wraps *before* hitting the scrollbar
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
                ImGui::TextUnformatted(license_text);
                ImGui::PopTextWrapPos();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        void tab_contributors()
        {
            Contributors::RenderTable();
        }

        struct third_party_lib
        {
            std::string name;
            std::string version;
            std::string url;
        };

        // stringification helpers for numeric version macros
        #define SP_VERSION_STR(x) #x
        #define SP_VERSION_XSTR(x) SP_VERSION_STR(x)

        static const std::vector<third_party_lib>& get_libs()
        {
            static const std::vector<third_party_lib> libs = []
            {
                std::vector<third_party_lib> v;

                // single header drop, version is not exposed as a macro
                v.push_back({ "AMD FidelityFX CAS/SPD", "1.1.4", "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK" });

                // single header drop, version is not exposed as a macro
                v.push_back({ "AMD Vulkan Memory Allocator", "3.3.0", "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator" });

                // runtime query, tracks the linked binary
                {
                    std::string ver = std::to_string(aiGetVersionMajor()) + "." +
                                      std::to_string(aiGetVersionMinor()) + "." +
                                      std::to_string(aiGetVersionPatch());
                    v.push_back({ "Assimp", ver, "https://github.com/assimp/assimp" });
                }

                // not header versioned, label only
                v.push_back({ "DirectX", "12.0", "https://en.wikipedia.org/wiki/DirectX" });

                // no compile time macro, runtime query requires loading the dll
                v.push_back({ "DirectXShaderCompiler", "May 2025", "https://github.com/microsoft/DirectXShaderCompiler" });

                // runtime query
                v.push_back({ "FreeImage", FreeImage_GetVersion(), "https://freeimage.sourceforge.io/" });

                {
                    std::string ver = SP_VERSION_XSTR(FREETYPE_MAJOR) "." SP_VERSION_XSTR(FREETYPE_MINOR) "." SP_VERSION_XSTR(FREETYPE_PATCH);
                    v.push_back({ "FreeType", ver, "https://freetype.org/" });
                }

                v.push_back({ "ImGui", IMGUI_VERSION, "https://github.com/ocornut/imgui" });

#if defined(_WIN32)
                // runtime query
                {
                    xess_version_t xv = {};
                    std::string ver  = "n/a";
                    if (xessGetVersion(&xv) == XESS_RESULT_SUCCESS)
                    {
                        ver = std::to_string(xv.major) + "." +
                              std::to_string(xv.minor) + "." +
                              std::to_string(xv.patch);
                    }
                    v.push_back({ "Intel XeSS", ver, "https://github.com/intel/xess" });
                }
#endif

                {
                    std::string ver = LUA_VERSION_MAJOR "." LUA_VERSION_MINOR "." LUA_VERSION_RELEASE;
                    v.push_back({ "Lua", ver, "https://www.lua.org/" });
                }

                // packed as major times 1000 plus minor times 10 plus patch
                {
                    constexpr int raw   = MESHOPTIMIZER_VERSION;
                    constexpr int major = raw / 1000;
                    constexpr int minor = (raw % 1000) / 10;
                    constexpr int patch = raw % 10;
                    std::string ver = std::to_string(major) + "." + std::to_string(minor);
                    if (patch > 0)
                    {
                        ver += "." + std::to_string(patch);
                    }
                    v.push_back({ "meshoptimizer", ver, "https://github.com/zeux/meshoptimizer" });
                }

                {
                    std::string ver = std::to_string(XR_VERSION_MAJOR(XR_CURRENT_API_VERSION)) + "." +
                                      std::to_string(XR_VERSION_MINOR(XR_CURRENT_API_VERSION)) + "." +
                                      std::to_string(XR_VERSION_PATCH(XR_CURRENT_API_VERSION));
                    v.push_back({ "OpenXR", ver, "https://www.khronos.org/openxr/" });
                }

                {
                    std::string ver = SP_VERSION_XSTR(PX_PHYSICS_VERSION_MAJOR) "."
                                      SP_VERSION_XSTR(PX_PHYSICS_VERSION_MINOR) "."
                                      SP_VERSION_XSTR(PX_PHYSICS_VERSION_BUGFIX);
                    v.push_back({ "PhysX", ver, "https://github.com/NVIDIA-Omniverse/PhysX" });
                }

                // packed as major times 1000 plus minor times 10 plus patch since 1.10
                {
                    constexpr int raw   = PUGIXML_VERSION;
                    constexpr int major = raw / 1000;
                    constexpr int minor = (raw / 10) % 100;
                    constexpr int patch = raw % 10;
                    std::string ver = std::to_string(major) + "." + std::to_string(minor);
                    if (patch > 0)
                    {
                        ver += "." + std::to_string(patch);
                    }
                    v.push_back({ "pugixml", ver, "https://github.com/zeux/pugixml" });
                }

                // only the in process api version is queryable
                v.push_back({ "RenderDoc", "1.40", "https://renderdoc.org/" });

                {
                    std::string ver = SP_VERSION_XSTR(SDL_MAJOR_VERSION) "."
                                      SP_VERSION_XSTR(SDL_MINOR_VERSION) "."
                                      SP_VERSION_XSTR(SDL_MICRO_VERSION);
                    v.push_back({ "SDL", ver, "https://www.libsdl.org/" });
                }

                v.push_back({ "Sol2", SOL_VERSION_STRING, "https://github.com/ThePhD/sol2" });

#if defined(API_GRAPHICS_VULKAN)
                // c api version, the release tag is not exposed
                {
                    std::string ver = std::to_string(SPVC_C_API_VERSION_MAJOR) + "." +
                                      std::to_string(SPVC_C_API_VERSION_MINOR) + "." +
                                      std::to_string(SPVC_C_API_VERSION_PATCH);
                    v.push_back({ "SPIRV-Cross", ver, "https://github.com/KhronosGroup/SPIRV-Cross" });
                }
#endif

#if defined(API_GRAPHICS_VULKAN)
                {
                    std::string ver = std::to_string(VK_API_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE)) + "." +
                                      std::to_string(VK_API_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE)) + "." +
                                      std::to_string(VK_HEADER_VERSION);
                    v.push_back({ "Vulkan", ver, "https://vulkan.lunarg.com/" });
                }
#endif

                return v;
            }();

            return libs;
        }

        void tab_libraries()
        {
            ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

            if (ImGui::BeginTable("##third_party_libs_table", 3, flags, ImVec2(0.0f, -FLT_MIN)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Link", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableHeadersRow();

                for (const third_party_lib& lib : get_libs())
                {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(lib.name.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(lib.version.c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushID(lib.url.c_str());
                    if (ImGuiSp::button("URL"))
                    {
                        spartan::FileSystem::OpenUrl(lib.url);
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }

        void window()
        {
            if (!visible)
            {
                return;
            }

            center_next_window(editor);

            // fixed size for the About window to prevent it jumping around when switching tabs
            ImGui::SetNextWindowSize(ImVec2(800.0f * spartan::Window::GetDpiScale(), 500.0f * spartan::Window::GetDpiScale()), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("About Spartan Engine", &visible, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
            {
                if (ImGui::BeginTabBar("##about_tabs"))
                {
                    if (ImGui::BeginTabItem("General"))
                    {
                        ImGui::Spacing();
                        tab_general();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Contributors"))
                    {
                        tab_contributors();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Third Party"))
                    {
                        tab_libraries();
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
            }
            ImGui::End();
        }
    }

    namespace controls
    {
        bool visible = false;

        struct Shortcut
        {
            const char* keys;        // stored as "Ctrl+S"
            const char* description;
        };

        static const Shortcut editor_shortcuts[] =
        {
            { "Ctrl+P",       "Toggle this window"       },
            { "Ctrl+S",       "Save world"               },
            { "Ctrl+L",       "Load world"               },
            { "Ctrl+Z",       "Undo"                     },
            { "Ctrl+Shift+Z", "Redo"                     },
            { "Alt+Enter",    "Toggle fullscreen"        },
            { "F",            "Focus on entity"          }
        };

        struct ControlBinding
        {
            const char* keyboard;
            const char* gamepad;
            const char* description;
        };

        static const Shortcut camera_controls[] =
        {
            { "Hold R-Click", "Enable First Person"      },
            { "W, A, S, D",   "Movement"                 },
            { "Q, E",         "Elevation (Up/Down)"      },
            { "Ctrl",         "Crouch"                   },
            { "Shift",        "Sprint / Fast Move"       },
            { "F",            "Toggle Flashlight"        },
            { "L-Click",      "Shoot physics cube"       }
        };

        static const ControlBinding camera_controls_full[] =
        {
            { "Hold R-Click",    "Always On",        "Enable First Person"       },
            { "W, A, S, D",      "Left Stick",       "Movement"                  },
            { "Mouse",           "Right Stick",      "Look Around"               },
            { "Q, E",            "L2, R2",           "Elevation (Down/Up)"       },
            { "Ctrl",            "O / Circle",       "Crouch"                    },
            { "Shift",           "L1",               "Sprint / Fast Move"        },
            { "Space",           "X / Cross",        "Jump"                      },
            { "F",               "Triangle / Y",     "Toggle Flashlight"         },
            { "L-Click + R-Click", "-",              "Shoot Physics Cube"        }
        };

        static const ControlBinding car_controls[] =
        {
            { "E",               "Square / X",       "Enter / Exit Vehicle"      },
            { "Arrow Up",        "R2",               "Throttle (Gas)"            },
            { "Arrow Down",      "L2",               "Brake"                     },
            { "Arrow Left/Right","Left Stick X",    "Steering"                  },
            { "Space",           "O / Circle",       "Handbrake"                 },
            { "R",               "X / Cross",        "Reset to Spawn"            },
            { "V",               "Triangle / Y",     "Cycle Camera View"         },
            { "-",               "L1",               "Shift Down (Manual)"       },
            { "-",               "R1",               "Shift Up (Manual)"         },
            { "-",               "Right Stick",      "Camera Orbit (Chase View)" },
            { "L",               "D-Pad Up",         "Headlights (Off / Low / High)" },
            { "Automatic",       "Automatic",        "Brake Lights (On Brake)"   }
        };

        // helper to render "Ctrl" + "S" as distinct visual styling elements
        void render_key_combo(const char* key_string)
        {
            std::string s = key_string;
            std::string delimiter = "+";
            size_t pos = 0;
            std::string token;

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));

            // We use a local index to differentiate keys within the same combo (just in case)
            int key_part_index = 0;
            bool first = true;

            while ((pos = s.find(delimiter)) != std::string::npos)
            {
                token = s.substr(0, pos);
                if (!first) { ImGui::SameLine(); ImGui::TextDisabled("+"); ImGui::SameLine(); }

                // PushID ensures that this "Ctrl" button is unique from others
                ImGui::PushID(key_part_index++);
                ImGui::SmallButton(token.c_str());
                ImGui::PopID();

                s.erase(0, pos + delimiter.length());
                first = false;
            }

            if (!first) { ImGui::SameLine(); ImGui::TextDisabled("+"); ImGui::SameLine(); }

            ImGui::PushID(key_part_index++);
            ImGui::SmallButton(s.c_str());
            ImGui::PopID();

            ImGui::PopStyleVar();
        }

        void show_shortcut_table(const char* str_id, const Shortcut* shortcuts, size_t count)
        {
            ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;

            if (ImGui::BeginTable(str_id, 2, flags, ImVec2(0.0f, -FLT_MIN)))
            {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Key Combination", ImGuiTableColumnFlags_WidthFixed, 180.0f * spartan::Window::GetDpiScale());

                for (size_t i = 0; i < count; i++)
                {
                    ImGui::TableNextRow();

                    // Push the row index as an ID
                    // This makes "Ctrl" in Row 0 distinct from "Ctrl" in Row 1
                    ImGui::PushID((int)i);

                    // Column 1: Description
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(shortcuts[i].description);

                    // Column 2: Keys
                    ImGui::TableSetColumnIndex(1);
                    render_key_combo(shortcuts[i].keys);

                    ImGui::PopID(); // Don't forget to Pop!
                }

                ImGui::EndTable();
            }
        }

        void show_control_binding_table(const char* str_id, const ControlBinding* bindings, size_t count)
        {
            ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;

            if (ImGui::BeginTable(str_id, 3, flags, ImVec2(0.0f, -FLT_MIN)))
            {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthFixed, 140.0f * spartan::Window::GetDpiScale());
                ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthFixed, 140.0f * spartan::Window::GetDpiScale());
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < count; i++)
                {
                    ImGui::TableNextRow();
                    ImGui::PushID((int)i);

                    // column 0: description
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(bindings[i].description);

                    // column 1: keyboard
                    ImGui::TableSetColumnIndex(1);
                    render_key_combo(bindings[i].keyboard);

                    // column 2: gamepad
                    ImGui::TableSetColumnIndex(2);
                    render_key_combo(bindings[i].gamepad);

                    ImGui::PopID();
                }

                ImGui::EndTable();
            }
        }
        void window()
        {
            if (!visible)
            {
                return;
            }

            // center the window on first use, but let user move it freely afterwards
            ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

            // set a reasonable default size (wider for the three-column layout)
            ImGui::SetNextWindowSize(ImVec2(600.0f * spartan::Window::GetDpiScale(), 400.0f * spartan::Window::GetDpiScale()), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("Controls & Shortcuts", &visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
            {
                if (ImGui::BeginTabBar("##controls_tabs"))
                {
                    if (ImGui::BeginTabItem("Editor Shortcuts"))
                    {
                        ImGui::Spacing();
                        show_shortcut_table("##editor_shortcuts_table", editor_shortcuts, std::size(editor_shortcuts));
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Camera"))
                    {
                        ImGui::Spacing();
                        show_control_binding_table("##camera_controls_table", camera_controls_full, std::size(camera_controls_full));
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Car"))
                    {
                        ImGui::Spacing();
                        show_control_binding_table("##car_controls_table", car_controls, std::size(car_controls));
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
            }
            ImGui::End();
        }
    }

}

void GeneralWindows::Initialize(Editor* editor_in)
{
    editor = editor_in;

    // the welcome window only shows up if the editor.ini file doesn't exist, which means that this is the first ever run
    welcome::visible = !spartan::FileSystem::Exists(ImGui::GetIO().IniFilename);

    // initialize world selector (handles asset download and world file scanning)
    WorldSelector::Initialize(editor_in);
}

void GeneralWindows::Tick()
{
    WorldPreviews::Tick();

    // show the world selector after the welcome window has been dismissed
    static bool welcome_was_visible = welcome::visible;
    if (welcome_was_visible && !welcome::visible)
    {
        welcome_was_visible = false;
        if (!WorldSelector::GetVisible())
        {
            WorldSelector::SetVisible(true);
        }
    }

    // if there was no welcome window at all, show the world selector on the first tick
    static bool first_tick = true;
    if (first_tick)
    {
        first_tick = false;
        if (!welcome::visible && !WorldSelector::GetVisible())
        {
            WorldSelector::SetVisible(true);
        }
    }

    // windows
    {
        WorldSelector::Tick();
        welcome::window();
        about::window();
        controls::window();
    }

    // shortcuts
    if (spartan::Input::GetKey(spartan::KeyCode::Ctrl_Left) && spartan::Input::GetKeyDown(spartan::KeyCode::P))
    {
        controls::visible = !controls::visible;
    }
}

bool GeneralWindows::GetVisibilityWorlds()
{
    return WorldSelector::GetVisible();
}

void GeneralWindows::SetVisibilityWorlds(const bool visibility)
{
    WorldSelector::SetVisible(visibility);
}

bool* GeneralWindows::GetVisiblityWindowAbout()
{
    return &about::visible;
}

bool* GeneralWindows::GetVisiblityWindowControls()
{
    return &controls::visible;
}
