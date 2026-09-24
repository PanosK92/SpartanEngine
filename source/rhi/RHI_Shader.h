/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/SpartanObject.h"
#include "RHI_Vertex.h"
#include "RHI_Descriptor.h"
#include <set>
//================================

namespace spartan
{
    enum class RHI_ShaderCompilationState
    {
        Idle,
        Compiling,
        Succeeded,
        Failed
    };

    class RHI_Shader : public SpartanObject
    {
    public:
        RHI_Shader();
        ~RHI_Shader();

        // compilation
        void Compile(const RHI_Shader_Type type, const std::string& file_path, bool async, const RHI_Vertex_Type vertex_type = RHI_Vertex_Type::Max);
        void CompileFromSpirv(const RHI_Shader_Type type, const void* spirv_bytecode, uint64_t spirv_size, const std::string& name);
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
        void PreprocessIncludeDirectives(const std::string& file_path, std::set<std::string>& processed_files);
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
