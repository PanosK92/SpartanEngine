/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ======================
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../Core/Spartan_Object.h"
#include "RHI_Vertex.h"
#include "RHI_Desctiptor.h"
//=================================

namespace Spartan
{
    // Forward declarations
    class Context;

    class SPARTAN_CLASS RHI_Shader : public Spartan_Object
    {
    public:
        RHI_Shader() = default;
        RHI_Shader(Context* context);
        ~RHI_Shader();

        // Compilation
        template<typename T> void Compile(const RHI_Shader_Type type, const std::string& shader);
        void Compile(const RHI_Shader_Type type, const std::string& shader) { Compile<RHI_Vertex_Undefined>(type, shader); }
        template<typename T> void CompileAsync(const RHI_Shader_Type type, const std::string& shader);
        void CompileAsync(const RHI_Shader_Type type, const std::string& shader) { CompileAsync<RHI_Vertex_Undefined>(type, shader); }
        auto GetCompilationState()  const { return m_compilation_state; }
        bool IsCompiled()           const { return m_compilation_state == Shader_Compilation_Succeeded; }
        void WaitForCompilation();

        // Resource
        void* GetResource() const { return m_resource; }
        bool HasResource()  const { return m_resource != nullptr; }

        // Name
        const std::string& GetName() const      { return m_name; }
        void SetName(const std::string& name)   { m_name = name; }

        // Defines
        void AddDefine(const std::string& define, const std::string& value = "1")    { m_defines[define] = value; }
        auto& GetDefines() const                                                    { return m_defines; }

        // Misc
        const std::vector<RHI_Descriptor>& GetDescriptors() const { return m_descriptors; }
        const auto& GetInputLayout()                        const { return m_input_layout; } // only valid for vertex shader
        const auto& GetFilePath()                           const { return m_file_path; }
        RHI_Shader_Type GetShaderStage()                    const { return m_shader_type; }
        const char* GetEntryPoint()                         const;
        const char* GetTargetProfile()                      const;
        const char* GetShaderModel()                        const;

    protected:
        std::shared_ptr<RHI_Device> m_rhi_device;

    private:
        // All compile functions resolve to this, and this is what the underlying API implements
        void* _Compile(const std::string& shader);
        void _Reflect(const RHI_Shader_Type shader_type, const uint32_t* ptr, uint32_t size);

        std::string m_name;
        std::string m_file_path;
        std::unordered_map<std::string, std::string> m_defines;
        std::vector<RHI_Descriptor> m_descriptors;
        std::shared_ptr<RHI_InputLayout> m_input_layout;
        Shader_Compilation_State m_compilation_state    = Shader_Compilation_Unknown;
        RHI_Shader_Type m_shader_type                   = RHI_Shader_Unknown;
        RHI_Vertex_Type m_vertex_type                   = RHI_Vertex_Type_Unknown;

        // API 
        void* m_resource = nullptr;
    };
}
