/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "RHI_Shader.h"
#include "RHI_InputLayout.h"
#include "../Core/ThreadPool.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_Shader::RHI_Shader() : SpartanObject()
    {

    }

    void RHI_Shader::Compile(const RHI_Shader_Type shader_type, const string& file_path, bool async, const RHI_Vertex_Type vertex_type)
    {
        // clear in case of re-compilation
        m_descriptors.clear();
        m_input_layout = nullptr;

        m_shader_type = shader_type;
        m_vertex_type = vertex_type;
        if (m_shader_type == RHI_Shader_Type::Vertex)
        {
            m_input_layout = make_shared<RHI_InputLayout>();
        }

        if (!FileSystem::IsFile(file_path))
        {
            SP_LOG_ERROR("\"%s\" doesn't exist.", file_path.c_str());
            return;
        }

        // load
        LoadFromDrive(file_path);

        // compile
        {
            m_compilation_state = RHI_ShaderCompilationState::Idle;

            auto compile = [this, shader_type, async]()
            {
                // time compilation
                const Stopwatch timer;

                // compile
                m_compilation_state = RHI_ShaderCompilationState::Compiling;
                m_rhi_resource      = RHI_Compile();
                m_compilation_state = m_rhi_resource ? RHI_ShaderCompilationState::Succeeded : RHI_ShaderCompilationState::Failed;

                // log failure
                if (m_compilation_state != RHI_ShaderCompilationState::Succeeded)
                {
                    string defines_str;
                    for (const auto& define : m_defines)
                    {
                        if (!defines_str.empty())
                            defines_str += ", ";

                        defines_str += define.first + " = " + define.second;
                    }
              
                    if (defines_str.empty())
                    {
                        SP_LOG_ERROR("Failed to compile shader \"%s\".", m_object_name.c_str());
                    }
                    else
                    {
                        SP_LOG_ERROR("Failed to compile shader \"%s\" with definitions \"%s\".", m_object_name.c_str(), defines_str.c_str());
                    }
                }
            };

            if (async)
            {
                ThreadPool::AddTask(compile);
            }
            else
            {
                compile();
            }
        }
    }

    void RHI_Shader::PreprocessIncludeDirectives(const string& file_path)
    {
        static string include_directive_prefix = "#include \"";

        // Skip already parsed include directives (avoid recursive include directives)
        if (find(m_file_paths_multiple.begin(), m_file_paths_multiple.end(), file_path) == m_file_paths_multiple.end())
        {
            m_file_paths_multiple.emplace_back(file_path);
        }
        else
        {
            return;
        }

        // load source
        ifstream in(file_path);
        stringstream buffer;
        buffer << in.rdbuf();
        string source = buffer.str();
        
        // go through every line
        istringstream stream(source);
        string source_line;
        while (getline(stream, source_line))
        {
            // add the line to the preprocessed source
            bool is_include_directive = source_line.find(include_directive_prefix) != string::npos;
            if (!is_include_directive)
            {
                m_preprocessed_source += source_line + "\n";
            }
            // if the line is an include directive, process it recursively
            else
            {
                // construct include file
                string file_name         = FileSystem::GetStringBetweenExpressions(source_line, include_directive_prefix, "\"");
                string include_file_path = FileSystem::GetDirectoryFromFilePath(file_path) + file_name;

                // process
                PreprocessIncludeDirectives(include_file_path);
            }
        }

        // save name
        m_names.emplace_back(FileSystem::GetFileNameFromFilePath(file_path));

        // save file path
        m_file_paths.emplace_back(file_path);

        // save source
        m_sources.emplace_back(source);
    }

    void RHI_Shader::LoadFromDrive(const string& file_path)
    {
        // initialize a couple of things
        m_object_name = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        m_file_path   = file_path;
        m_preprocessed_source.clear();
        m_names.clear();
        m_file_paths.clear();
        m_sources.clear();
        m_file_paths_multiple.clear();

        // construct the source by recursively processing all include directives, starting from the actual file path.
        PreprocessIncludeDirectives(file_path);

        // update hash
        {
            hash<string> hasher;
            m_hash = 0;
            m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(hasher(m_preprocessed_source)));
            for (const auto& it : m_defines)
            {
                m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(hasher(it.first)));
                m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(hasher(it.second)));
            }
        }

        // reverse the vectors so they have the main shader before the subsequent include directives.
        // this also helps with the editor's shader editor where you are interested more in the first source.
        reverse(m_names.begin(), m_names.end());
        reverse(m_file_paths.begin(), m_file_paths.end());
        reverse(m_sources.begin(), m_sources.end());
    }

    void RHI_Shader::SetSource(const uint32_t index, const string& source)
    {
        if (index >= m_sources.size())
        {
            SP_LOG_ERROR("No source with index %d exists.", index);
            return;
        }

        m_sources[index] = source;
    }

    uint32_t RHI_Shader::GetVertexSize() const
    {
        return m_input_layout->GetVertexSize();
    }

    const char* RHI_Shader::GetEntryPoint() const
    {
        switch (m_shader_type)
        {
            case RHI_Shader_Type::Vertex:  return "main_vs";
            case RHI_Shader_Type::Hull:    return "main_hs";
            case RHI_Shader_Type::Domain:  return "main_ds";
            case RHI_Shader_Type::Pixel:   return "main_ps";
            case RHI_Shader_Type::Compute: return "main_cs";
            default:                       return nullptr;
        }
    }

    const char* RHI_Shader::GetTargetProfile() const
    {
        switch (m_shader_type)
        {
            case RHI_Shader_Type::Vertex:  return "vs_6_8";
            case RHI_Shader_Type::Hull:    return "hs_6_8";
            case RHI_Shader_Type::Domain:  return "ds_6_8";
            case RHI_Shader_Type::Pixel:   return "ps_6_8";
            case RHI_Shader_Type::Compute: return "cs_6_8";
            default:                       return nullptr;
        }
    }

}
