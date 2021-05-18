/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ======================
#include "Spartan.h"
#include "RHI_Shader.h"
#include "RHI_InputLayout.h"
#include "../Threading/Threading.h"
#include "../Rendering/Renderer.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_Shader::RHI_Shader(Context* context, const RHI_Vertex_Type vertex_type) : SpartanObject(context)
    {
        m_rhi_device    = context->GetSubsystem<Renderer>()->GetRhiDevice();
        m_input_layout  = make_shared<RHI_InputLayout>(m_rhi_device);
        m_vertex_type   = vertex_type;
    }

    void RHI_Shader::Compile2()
    {
        // Compile
        m_compilation_state = Shader_Compilation_State::Compiling;
        m_resource          = Compile3();
        m_compilation_state = m_resource ? Shader_Compilation_State::Succeeded : Shader_Compilation_State::Failed;

        // Log compilation result
        {
            string type_str = "unknown";
            type_str        = m_shader_type == RHI_Shader_Vertex     ? "vertex"   : type_str;
            type_str        = m_shader_type == RHI_Shader_Pixel      ? "pixel"    : type_str;
            type_str        = m_shader_type == RHI_Shader_Compute    ? "compute"  : type_str;

            string defines;
            for (const auto& define : m_defines)
            {
                if (!defines.empty())
                    defines += ", ";

                defines += define.first + " = " + define.second;
            }

            if (m_compilation_state == Shader_Compilation_State::Succeeded)
            {
                if (defines.empty())
                {
                    LOG_INFO("Successfully compiled %s shader \"%s\".", type_str.c_str(), m_name.c_str());
                }
                else
                {
                    LOG_INFO("Successfully compiled %s shader \"%s\" with definitions \"%s\".", type_str.c_str(), m_name.c_str(), defines.c_str());
                }
            }
            else if (m_compilation_state == Shader_Compilation_State::Failed)
            {
                if (defines.empty())
                {
                    LOG_ERROR("Failed to compile %s shader \"%s\".", type_str.c_str(), m_name.c_str());
                }
                else
                {
                    LOG_ERROR("Failed to compile %s shader \"%s\" with definitions \"%s\".", type_str.c_str(), m_name.c_str(), defines.c_str());
                }
            }
        }
    }

    void RHI_Shader::Compile(const RHI_Shader_Type type, const std::string& shader, bool async)
    {
        m_shader_type = type;

        // Source
        if (!FileSystem::IsFile(shader))
        {
            m_name      = "N/A";
            m_file_path = "N/A";
            m_source    = shader;
        }
        else // File
        {
            LoadSource(shader);
        }

        // Compile
        m_compilation_state = Shader_Compilation_State::Idle;
        if (!async)
        {
            Compile2();
        }
        else
        {
            m_context->GetSubsystem<Threading>()->AddTask([this]()
            {
                Compile2();
            });
        }
    }

    void RHI_Shader::ParseSource(const string& file_path)
    {
        static string include_directive_prefix = "#include \"";

        // Read the file
        ifstream in(file_path);
        stringstream buffer;
        buffer << in.rdbuf();

        // Skip already parsed include directives (avoid recursive include directives)
        if (find(m_file_paths_multiple.begin(), m_file_paths_multiple.end(), file_path) == m_file_paths_multiple.end())
        {
            m_file_paths_multiple.emplace_back(file_path);
        }
        else
        {
            return;
        }

        string file_source = buffer.str();
        string file_directory = FileSystem::GetDirectoryFromFilePath(file_path);
        
        
        // Build combined source (go through every line)
        istringstream stream(file_source);
        string source_line;
        while (getline(stream, source_line))
        {
            bool is_include_directive = source_line.find(include_directive_prefix) != string::npos;

            if (!is_include_directive)
            {
                m_source += source_line + "\n";
            }
            else
            {
                string file_name = FileSystem::GetStringBetweenExpressions(source_line, include_directive_prefix, "\"");
                string include_file_path = file_directory + file_name;
                ParseSource(include_file_path);
            }
        }

        // Get name
        m_names.emplace_back(FileSystem::GetFileNameFromFilePath(file_path));

        // Get file path
        m_file_paths.emplace_back(file_path);

        // Get source
        m_sources.emplace_back(file_source);
    }

    void RHI_Shader::WaitForCompilation()
    {
        // Wait
        while (m_compilation_state == Shader_Compilation_State::Compiling)
        {
            LOG_INFO("Waiting for shader \"%s\" to compile...", m_name.c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        
        // Log error in case of failure
        if (m_compilation_state != Shader_Compilation_State::Succeeded)
        {
            LOG_ERROR("Shader \"%s\" failed compile", m_name.c_str());
        }
    }

    void RHI_Shader::LoadSource(const std::string& file_path)
    {
        // Get name and file path
        m_name = FileSystem::GetFileNameFromFilePath(file_path);
        m_file_path = file_path;

        // Parse source
        m_source.clear();
        m_names.clear();
        m_file_paths.clear();
        m_sources.clear();
        m_file_paths_multiple.clear();
        ParseSource(file_path);

        // Reverse the vectors so they have the main shader before the subsequent include directives.
        // This also helps with the editor's shader editor where you are interested more in the first source.
        std::reverse(m_names.begin(), m_names.end());
        std::reverse(m_file_paths.begin(), m_file_paths.end());
        std::reverse(m_sources.begin(), m_sources.end());
    }

    void RHI_Shader::SetSource(const uint32_t index, const std::string& source)
    {
        if (index >= m_sources.size())
        {
            LOG_ERROR("No source with index %d exists.", index);
            return;
        }

        m_sources[index] = source;
    }

    const char* RHI_Shader::GetEntryPoint() const
    {
        static const char* entry_point_empty = nullptr;

        static const char* entry_point_vs = "mainVS";
        static const char* entry_point_ps = "mainPS";
        static const char* entry_point_cs = "mainCS";

        if (m_shader_type == RHI_Shader_Vertex)     return entry_point_vs;
        if (m_shader_type == RHI_Shader_Pixel)      return entry_point_ps;
        if (m_shader_type == RHI_Shader_Compute)    return entry_point_cs;

        return entry_point_empty;
    }

    const char* RHI_Shader::GetTargetProfile() const
    {
        static const char* target_profile_empty = nullptr;

        #if defined(API_GRAPHICS_D3D11)
        static const char* target_profile_vs = "vs_5_0";
        static const char* target_profile_ps = "ps_5_0";
        static const char* target_profile_cs = "cs_5_0";
        #elif defined(API_GRAPHICS_D3D12)
        static const char* target_profile_vs = "vs_6_6";
        static const char* target_profile_ps = "ps_6_6";
        static const char* target_profile_cs = "cs_6_6";
        #elif defined(API_GRAPHICS_VULKAN)
        static const char* target_profile_vs = "vs_6_6";
        static const char* target_profile_ps = "ps_6_6";
        static const char* target_profile_cs = "cs_6_6";
        #endif

        if (m_shader_type == RHI_Shader_Vertex)     return target_profile_vs;
        if (m_shader_type == RHI_Shader_Pixel)      return target_profile_ps;
        if (m_shader_type == RHI_Shader_Compute)    return target_profile_cs;

        return target_profile_empty;
    }

    const char* RHI_Shader::GetShaderModel() const
    {
        #if defined(API_GRAPHICS_D3D11)
        static const char* shader_model = "5_0";
        #elif defined(API_GRAPHICS_D3D12)
        static const char* shader_model = "6_0";
        #elif defined(API_GRAPHICS_VULKAN)
        static const char* shader_model = "6_0";
        #endif

        return shader_model;
    }
}
