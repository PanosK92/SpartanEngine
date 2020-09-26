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
    RHI_Shader::RHI_Shader(Context* context) : Spartan_Object(context)
    {
        m_rhi_device    = context->GetSubsystem<Renderer>()->GetRhiDevice();
        m_input_layout    = make_shared<RHI_InputLayout>(m_rhi_device);
    }

    template <typename T>
    void RHI_Shader::Compile(const RHI_Shader_Type type, const string& shader)
    {
        m_shader_type = type;
        m_vertex_type = RHI_Vertex_Type_To_Enum<T>();

        // Can also be the source
        const bool is_file = FileSystem::IsFile(shader);

        // Deduce name and file path
        if (is_file)
        {
            m_name      = FileSystem::GetFileNameFromFilePath(shader);
            m_file_path = shader;
        }
        else
        {
            m_name.clear();
            m_file_path.clear();
        }

        // Compile
        m_compilation_state = Shader_Compilation_Compiling;
        m_resource          = _Compile(shader);
        m_compilation_state = m_resource ? Shader_Compilation_Succeeded : Shader_Compilation_Failed;

        // Log compilation result
        {
            string type_str = "unknown";
            type_str        = type == RHI_Shader_Vertex     ? "vertex"   : type_str;
            type_str        = type == RHI_Shader_Pixel      ? "pixel"    : type_str;
            type_str        = type == RHI_Shader_Compute    ? "compute"  : type_str;

            string defines;
            for (const auto& define : m_defines)
            {
                if (!defines.empty())
                    defines += ", ";

                defines += define.first + " = " + define.second;
            }

            if (m_compilation_state == Shader_Compilation_Succeeded)
            {
                if (defines.empty())
                {
                    LOG_INFO("Successfully compiled %s shader from \"%s\"", type_str.c_str(), shader.c_str());
                }
                else
                {
                    LOG_INFO("Successfully compiled %s shader from \"%s\" with definitions \"%s\"", type_str.c_str(), shader.c_str(), defines.c_str());
                }
            }
            else if (m_compilation_state == Shader_Compilation_Failed)
            {
                if (defines.empty())
                {
                    LOG_ERROR("Failed to compile %s shader from \"%s\"", type_str.c_str(), shader.c_str());
                }
                else
                {
                    LOG_ERROR("Failed to compile %s shader from \"%s\" with definitions \"%s\"", type_str.c_str(), shader.c_str(), defines.c_str());
                }
            }
        }
    }

    template <typename T>
    void RHI_Shader::CompileAsync(const RHI_Shader_Type type, const string& shader)
    {
        m_context->GetSubsystem<Threading>()->AddTask([this, type, shader]()
        {
            Compile<T>(type, shader);
        });
    }

    void RHI_Shader::WaitForCompilation()
    {
        // Wait
        while (m_compilation_state == Shader_Compilation_Compiling)
        {
            LOG_INFO("Waiting for shader \"%s\" to compile...", m_name.c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        
        // Log error in case of failure
        if (m_compilation_state != Shader_Compilation_Succeeded)
        {
            LOG_ERROR("Shader \"%s\" failed compile", m_name.c_str());
        }
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

    //= Explicit template instantiation =======================================================================
    template void RHI_Shader::CompileAsync<RHI_Vertex_Undefined>(const RHI_Shader_Type, const std::string&);
    template void RHI_Shader::CompileAsync<RHI_Vertex_Pos>(const RHI_Shader_Type, const std::string&);
    template void RHI_Shader::CompileAsync<RHI_Vertex_PosTex>(const RHI_Shader_Type, const std::string&);
    template void RHI_Shader::CompileAsync<RHI_Vertex_PosCol>(const RHI_Shader_Type, const std::string&);
    template void RHI_Shader::CompileAsync<RHI_Vertex_Pos2dTexCol8>(const RHI_Shader_Type, const std::string&);
    template void RHI_Shader::CompileAsync<RHI_Vertex_PosTexNorTan>(const RHI_Shader_Type, const std::string&);
    //=========================================================================================================
}
