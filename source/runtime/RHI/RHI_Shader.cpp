/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include "RHI_Device.h"
#include "../Core/ThreadPool.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    string to_lower(const string& input)
    {
        string result = input;
        transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return tolower(c); });
        return result;
    }
}

namespace spartan
{
    RHI_Shader::RHI_Shader() : SpartanObject()
    {

    }

    RHI_Shader::~RHI_Shader()
    {
        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Shader, m_rhi_resource);
            m_rhi_resource = nullptr;
        }
    }

    void RHI_Shader::Compile(const RHI_Shader_Type shader_type, const string& file_path, bool async, const RHI_Vertex_Type vertex_type)
    {
        if (!FileSystem::IsFile(file_path))
        {
            SP_LOG_ERROR("\"%s\" doesn't exist.", file_path.c_str());
            return;
        }

        // clear
        m_input_layout = nullptr;
        m_descriptors.clear();

        m_shader_type = shader_type;
        m_vertex_type = vertex_type;
        if (m_shader_type == RHI_Shader_Type::Vertex)
        {
            m_input_layout = make_shared<RHI_InputLayout>();
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
                void* resource      = RHI_Compile();
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Shader, m_rhi_resource);
                m_rhi_resource      = resource;
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

    void RHI_Shader::PreprocessIncludeDirectives(const string& file_path, set<string>& processed_files)
    {
        const string include_prefix = "#include \"";
    
        // nrmalize file path to lowercase for case-insensitive tracking
        string file_path_lower = to_lower(file_path);
    
        // skip if already processed (prevents circular includes)
        if (processed_files.count(file_path_lower) > 0)
        {
            //SP_LOG_WARNING("Circular or duplicate include detected: %s", file_path.c_str());
            return;
        }
        processed_files.insert(file_path_lower);
    
        // try opening the file with the exact path
        ifstream file_stream(file_path);
        string resolved_path = file_path;
    
        // if exact path fails, attempt case-insensitive lookup
        if (!file_stream.is_open())
        {
            string directory         = FileSystem::GetDirectoryFromFilePath(file_path);
            string filename          = FileSystem::GetFileNameFromFilePath(file_path);
            string filename_lower    = to_lower(filename);
            vector<string> dir_files = FileSystem::GetFilesInDirectory(directory);

            for (const string& candidate : dir_files)
            {
                if (to_lower(candidate) == filename_lower)
                {
                    resolved_path = directory + candidate;
                    file_stream.open(resolved_path);
                    if (file_stream.is_open())
                        break;
                }
            }
    
            if (!file_stream.is_open())
            {
                SP_LOG_ERROR("Failed to locate include file: %s", file_path.c_str());
                return;
            }
        }
    
        // read the entire file into a string
        stringstream file_content;
        file_content << file_stream.rdbuf();
        string source = file_content.str();
        file_stream.close();
    
        // process the file line by line
        istringstream line_stream(source);
        string line;
        while (getline(line_stream, line))
        {
            // check for #include directive
            size_t include_start = line.find(include_prefix);
            if (include_start != string::npos)
            {
                size_t quote_start = include_start + include_prefix.length();
                size_t quote_end = line.find("\"", quote_start);
                if (quote_end != string::npos)
                {
                    string include_name = line.substr(quote_start, quote_end - quote_start);
                    string include_path = FileSystem::GetDirectoryFromFilePath(resolved_path) + include_name;
    
                    // recursively process the included file
                    PreprocessIncludeDirectives(include_path, processed_files);
                }
                else
                {
                    SP_LOG_ERROR("Malformed #include in %s: %s", resolved_path.c_str(), line.c_str());
                }
            }
            else
            {
                // append non-include lines to the preprocessed source
                m_preprocessed_source += line + "\n";
            }
        }
    
        // store metadata for debugging or inspection
        m_names.push_back(FileSystem::GetFileNameFromFilePath(resolved_path));
        m_file_paths.push_back(resolved_path);
        m_sources.push_back(source);
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
        set<string> processed_files;
        PreprocessIncludeDirectives(file_path, processed_files);

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
            case RHI_Shader_Type::Vertex:     return "main_vs";
            case RHI_Shader_Type::Hull:       return "main_hs";
            case RHI_Shader_Type::Domain:     return "main_ds";
            case RHI_Shader_Type::Pixel:      return "main_ps";
            case RHI_Shader_Type::Compute:    return "main_cs";
            case RHI_Shader_Type::RayTracing: return nullptr;
            default:                          return nullptr;
        }
    }

    const char* RHI_Shader::GetTargetProfile() const
    {
        switch (m_shader_type)
        {
            case RHI_Shader_Type::Vertex:     return "vs_6_8";
            case RHI_Shader_Type::Hull:       return "hs_6_8";
            case RHI_Shader_Type::Domain:     return "ds_6_8";
            case RHI_Shader_Type::Pixel:      return "ps_6_8";
            case RHI_Shader_Type::Compute:    return "cs_6_8";
            case RHI_Shader_Type::RayTracing: return "lib_6_8";
            default:                          return nullptr;
        }
    }

}
