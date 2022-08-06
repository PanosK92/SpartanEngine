/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "pch.h"
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
        m_rhi_device   = context->GetSubsystem<Renderer>()->GetRhiDevice();
        m_input_layout = make_shared<RHI_InputLayout>(m_rhi_device);
        m_vertex_type  = vertex_type;
    }

    // Actual API specific compilation
    static void CompileShader(
        atomic<Shader_Compilation_State>& compilation_state,
        RHI_Shader_Type shader_type,
        const unordered_map<string, string>& defines,
        string& object_name,
        void*& resource,
        function<void*()> compile2
    )
    {
        const Stopwatch timer;

        // Compile
        compilation_state = Shader_Compilation_State::Compiling;
        resource          = compile2();
        compilation_state = resource ? Shader_Compilation_State::Succeeded : Shader_Compilation_State::Failed;

        // Log compilation result
        {
            string type_str = "unknown";
            type_str = shader_type == RHI_Shader_Vertex  ? "vertex"  : type_str;
            type_str = shader_type == RHI_Shader_Pixel   ? "pixel"   : type_str;
            type_str = shader_type == RHI_Shader_Compute ? "compute" : type_str;

            string defines_str;
            for (const auto& define : defines)
            {
                if (!defines_str.empty())
                    defines_str += ", ";

                defines_str += define.first + " = " + define.second;
            }

            if (compilation_state == Shader_Compilation_State::Succeeded)
            {
                if (defines_str.empty())
                {
                    LOG_INFO("Successfully compiled %s shader \"%s\" in %.2f ms.", type_str.c_str(), object_name.c_str(), timer.GetElapsedTimeMs());
                }
                else
                {
                    LOG_INFO("Successfully compiled %s shader \"%s\" with definitions \"%s\" in %.2f ms.", type_str.c_str(), object_name.c_str(), defines_str.c_str(), timer.GetElapsedTimeMs());
                }
            }
            else if (compilation_state == Shader_Compilation_State::Failed)
            {
                if (defines_str.empty())
                {
                    LOG_ERROR("Failed to compile %s shader \"%s\".", type_str.c_str(), object_name.c_str());
                }
                else
                {
                    LOG_ERROR("Failed to compile %s shader \"%s\" with definitions \"%s\".", type_str.c_str(), object_name.c_str(), defines_str.c_str());
                }
            }
        }

    };
    void RHI_Shader::Compile(const RHI_Shader_Type type, const string& file_path, bool async)
    {
        m_shader_type = type;

        if (!FileSystem::IsFile(file_path))
        {
            LOG_ERROR("\"%s\" doesn't exist.", file_path.c_str());
            return;
        }

        // Load
        LoadSource(file_path);

        // Compile
        {
            m_compilation_state = Shader_Compilation_State::Idle;

            if (!async)
            {
                CompileShader(m_compilation_state, m_shader_type, m_defines, m_object_name, m_rhi_resource, bind(&RHI_Shader::Compile2, this));
            }
            else
            {
                m_context->GetSubsystem<Threading>()->AddTask([this]() { CompileShader(m_compilation_state, m_shader_type, m_defines, m_object_name, m_rhi_resource, std::bind(&RHI_Shader::Compile2, this)); });
            }
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

        string file_source    = buffer.str();
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

    void RHI_Shader::LoadSource(const string& file_path)
    {
        // Get name and file path
        m_object_name = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        m_file_path   = file_path;

        // Parse source
        m_source.clear();
        m_names.clear();
        m_file_paths.clear();
        m_sources.clear();
        m_file_paths_multiple.clear();
        ParseSource(file_path);

        // Reverse the vectors so they have the main shader before the subsequent include directives.
        // This also helps with the editor's shader editor where you are interested more in the first source.
        reverse(m_names.begin(), m_names.end());
        reverse(m_file_paths.begin(), m_file_paths.end());
        reverse(m_sources.begin(), m_sources.end());
    }

    void RHI_Shader::SetSource(const uint32_t index, const string& source)
    {
        if (index >= m_sources.size())
        {
            LOG_ERROR("No source with index %d exists.", index);
            return;
        }

        m_sources[index] = source;
    }

    const uint32_t RHI_Shader::GetVertexSize() const
    {
        return m_input_layout->GetVertexSize();
    }

    const char* RHI_Shader::GetEntryPoint() const
    {
        if (m_shader_type == RHI_Shader_Vertex)  return "mainVS";
        if (m_shader_type == RHI_Shader_Pixel)   return "mainPS";
        if (m_shader_type == RHI_Shader_Compute) return "mainCS";

        return nullptr;
    }
}
