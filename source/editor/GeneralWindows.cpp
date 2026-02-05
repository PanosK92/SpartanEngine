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
#include "Windows/WorldSelector.h"
#include "Windows/Contributors.h"
#include "ImGui/Source/imgui.h"
#include "ImGui/ImGui_Extension.h"
#include "FileSystem/FileSystem.h"
#include "Widgets/Viewport.h"
#include "Input/Input.h"
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
            if (!visible) return;

            // position below the world selection window so they don't overlap
            ImVec2 viewport_center = editor->GetWidget<Viewport>()->GetCenter();
            ImGui::SetNextWindowPos(
                ImVec2(viewport_center.x, viewport_center.y + 200.0f * spartan::Window::GetDpiScale()),
                ImGuiCond_Appearing,
                ImVec2(0.5f, 0.0f) // anchor at top-center so it extends downward
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
                spartan::FileSystem::OpenUrl("https://panoskarabelas.com/");

            ImGui::SameLine();
            if (ImGuiSp::button("GitHub"))
                spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine");

            ImGui::SameLine();
            if (ImGuiSp::button("X"))
                spartan::FileSystem::OpenUrl("https://twitter.com/panoskarabelas");

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
            const char* name;
            const char* version;
            const char* url;
        };

        // third-party libraries used by the engine (alphabetically sorted)
        static const third_party_lib libs[] =
        {
            { "AMD Compressonator",          "4.2",        "https://github.com/GPUOpen-Tools/compressonator"                  },
            { "AMD FidelityFX",              "1.1.4",      "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK"       },
            { "AMD Vulkan Memory Allocator", "3.3.0",      "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator"},
            { "Assimp",                      "6.0.2",      "https://github.com/assimp/assimp"                                 },
            { "DirectX",                     "12.0",       "https://en.wikipedia.org/wiki/DirectX"                            },
            { "DirectXShaderCompiler",       "May 2025",   "https://github.com/microsoft/DirectXShaderCompiler"               },
            { "FreeImage",                   "3.18.0",     "https://freeimage.sourceforge.io/"                                },
            { "FreeType",                    "2.13.2",     "https://freetype.org/"                                            },
            { "ImGui",                       "1.91.9 WIP", "https://github.com/ocornut/imgui"                                 },
            { "Intel XeSS",                  "2.1.0",      "https://github.com/intel/xess"                                    },
            { "Lua",                         "5.5.0",      "https://www.lua.org/"                                             },
            { "meshoptimizer",               "0.25",       "https://github.com/zeux/meshoptimizer"                            },
            { "NVIDIA NRD",                  "4.16.1",     "https://github.com/NVIDIAGameWorks/RayTracingDenoiser"            },
            { "OpenXR",                      "1.1.54",     "https://www.khronos.org/openxr/"                                  },
            { "PhysX",                       "5.6.0",      "https://github.com/NVIDIA-Omniverse/PhysX"                        },
            { "pugixml",                     "1.13",       "https://github.com/zeux/pugixml"                                  },
            { "RenderDoc",                   "1.40",       "https://renderdoc.org/"                                           },
            { "SDL",                         "3.2.24",     "https://www.libsdl.org/"                                          },
            { "Sol2",                        "3.3.0",      "https://github.com/ThePhD/sol2"                                   },
            { "SPIRV-Cross",                 "2023.09",    "https://github.com/KhronosGroup/SPIRV-Cross"                      },
            { "Vulkan",                      "1.4.321",    "https://vulkan.lunarg.com/"                                       },
        };

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

                for (const third_party_lib& lib : libs)
                {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(lib.name);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(lib.version);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushID(lib.url);
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
            if (!visible) return;

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
            { "-",               "Right Stick",      "Camera Orbit (Chase View)" }
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
                return;

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
