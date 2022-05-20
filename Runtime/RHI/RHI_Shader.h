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
    // Forward declarations
    class Context;

    class SPARTAN_CLASS RHI_Shader : public SpartanObject
    {
    public:
        RHI_Shader() = default;
        RHI_Shader(Context* context, const RHI_Vertex_Type vertex_type = RHI_Vertex_Type::Undefined);
        ~RHI_Shader();

        // Compilation
        void Compile(const RHI_Shader_Type type, const std::string& file_path, bool async);
        Shader_Compilation_State GetCompilationState() const { return m_compilation_state; }
        bool IsCompiled()                              const { return m_compilation_state == Shader_Compilation_State::Succeeded; }
        void WaitForCompilation();

        // Resource
        void* GetResource() const;
        bool HasResource()  const { return m_resource != nullptr; }

        // Source
        void LoadSource(const std::string& file_path);
        const std::vector<std::string>& GetNames()     const { return m_names; }
        const std::vector<std::string>& GetFilePaths() const { return m_file_paths; }
        const std::vector<std::string>& GetSources()   const { return m_sources; }
        void SetSource(const uint32_t index, const std::string& source);

        // Defines
        void AddDefine(const std::string& define, const std::string& value = "1") { m_defines[define] = value; }
        auto& GetDefines() const                                                  { return m_defines; }

        // Misc
        const uint32_t GetVertexSize() const;
        const std::vector<RHI_Descriptor>& GetDescriptors()      const { return m_descriptors; }
        const std::shared_ptr<RHI_InputLayout>& GetInputLayout() const { return m_input_layout; } // only valid for vertex shader
        const auto& GetFilePath()                                const { return m_file_path; }
        RHI_Shader_Type GetShaderStage()                         const { return m_shader_type; }
        const char* GetEntryPoint()                              const;
        const char* GetTargetProfile()                           const;
        const char* GetShaderModel()                             const;

    protected:
        std::shared_ptr<RHI_Device> m_rhi_device;

    private:
        void ParseSource(const std::string& file_path);
        void* Compile2();
        void Reflect(const RHI_Shader_Type shader_type, const uint32_t* ptr, uint32_t size);

        std::string m_file_path;
        std::string m_source;
        std::vector<std::string> m_names;               // The names of the files from the include directives in the shader
        std::vector<std::string> m_file_paths;          // The file paths of the files from the include directives in the shader
        std::vector<std::string> m_sources;             // The source of the files from the include directives in the shader
        std::vector<std::string> m_file_paths_multiple; // The file paths of include directives which are defined multiple times in the shader
        std::unordered_map<std::string, std::string> m_defines;
        std::vector<RHI_Descriptor> m_descriptors;
        std::shared_ptr<RHI_InputLayout> m_input_layout;
        std::atomic<Shader_Compilation_State> m_compilation_state = Shader_Compilation_State::Idle;
        RHI_Shader_Type m_shader_type                             = RHI_Shader_Unknown;
        RHI_Vertex_Type m_vertex_type                             = RHI_Vertex_Type::Undefined;
        void* m_resource                                          = nullptr;
    };
}
