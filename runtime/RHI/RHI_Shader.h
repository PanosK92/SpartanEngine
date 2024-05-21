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

#pragma once

//= INCLUDES =====================
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../Core/SpartanObject.h"
#include "RHI_Vertex.h"
#include "RHI_Descriptor.h"
//================================

namespace Spartan
{
    enum class RHI_ShaderCompilationState
    {
        Idle,
        Compiling,
        Succeeded,
        Failed
    };

    class SP_CLASS RHI_Shader : public SpartanObject
    {
    public:
        RHI_Shader();
        ~RHI_Shader();

        // compilation
        void Compile(const RHI_Shader_Type type, const std::string& file_path, bool async, const RHI_Vertex_Type vertex_type = RHI_Vertex_Type::Max);
        RHI_ShaderCompilationState GetCompilationState() const { return m_compilation_state; }
        bool IsCompiled() const                                { return m_compilation_state == RHI_ShaderCompilationState::Succeeded; }

        // source
        void LoadFromDrive(const std::string& file_path);
        const std::vector<std::string>& GetNames()     const { return m_names; }
        const std::vector<std::string>& GetFilePaths() const { return m_file_paths; }
        const std::vector<std::string>& GetSources()   const { return m_sources; }
        void SetSource(const uint32_t index, const std::string& source);

        // defines
        void AddDefine(const std::string& define, const std::string& value = "1") { m_defines[define] = value; }
        auto& GetDefines() const                                                  { return m_defines; }

        // misc
        uint32_t GetVertexSize() const;
        const std::vector<RHI_Descriptor>& GetDescriptors()      const { return m_descriptors; }
        const std::shared_ptr<RHI_InputLayout>& GetInputLayout() const { return m_input_layout; } // only valid for a vertex shader
        const auto& GetFilePath()                                const { return m_file_path; }
        RHI_Shader_Type GetShaderStage()                         const { return m_shader_type; }
        uint64_t GetHash()                                       const { return m_hash; }
        const char* GetEntryPoint()                              const;
        const char* GetTargetProfile()                           const;
        void* GetRhiResource()                                   const { return m_rhi_resource; }

    private:
        void PreprocessIncludeDirectives(const std::string& file_path);
        void* RHI_Compile();
        void Reflect(const RHI_Shader_Type shader_type, const uint32_t* ptr, uint32_t size);

        std::string m_file_path;
        std::string m_preprocessed_source;
        std::vector<std::string> m_names;               // The names of the files from the include directives in the shader
        std::vector<std::string> m_file_paths;          // The file paths of the files from the include directives in the shader
        std::vector<std::string> m_sources;             // The source of the files from the include directives in the shader
        std::vector<std::string> m_file_paths_multiple; // The file paths of include directives which are defined multiple times in the shader
        std::unordered_map<std::string, std::string> m_defines;
        std::vector<RHI_Descriptor> m_descriptors;
        std::shared_ptr<RHI_InputLayout> m_input_layout;
        std::atomic<RHI_ShaderCompilationState> m_compilation_state = RHI_ShaderCompilationState::Idle;
        RHI_Shader_Type m_shader_type                              = RHI_Shader_Type::Max;
        RHI_Vertex_Type m_vertex_type                               = RHI_Vertex_Type::Max;
        uint64_t m_hash                                             = 0;

        void* m_rhi_resource = nullptr;
    };
}
