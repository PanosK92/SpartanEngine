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

//= INCLUDES ========================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Shader.h"
#include "../RHI_InputLayout.h"
SP_WARNINGS_OFF
#include <spirv_cross/spirv_hlsl.hpp>
#include <atlbase.h>
#include <dxcapi.h>
SP_WARNINGS_ON
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_Shader::~RHI_Shader()
    {
        const auto rhi_context = m_rhi_device->GetContextRhi();

        if (HasResource())
        {
            vkDestroyShaderModule(rhi_context->device, static_cast<VkShaderModule>(m_resource), nullptr);
            m_resource = nullptr;
        }
    }

    namespace DxcHelper
    {
        /*
        Version: dxcompiler.dll: 1.6 - 1.5.0.2860 (f1f60648)

        USAGE: dxc.exe [options] <inputs>
        
        Common Options:
          -help              Display available options
          -nologo            Suppress copyright message
          -Qunused-arguments Don't emit warning for unused driver arguments
        
        Compilation Options:
          -all-resources-bound    Enables agressive flattening
          -auto-binding-space <value>
                                  Set auto binding space - enables auto resource binding in libraries
          -Cc                     Output color coded assembly listings
          -default-linkage <value>
                                  Set default linkage for non-shader functions when compiling or linking to a library target (internal, external)
          -denorm <value>         select denormal value options (any, preserve, ftz). any is the default.
          -D <value>              Define macro
          -enable-16bit-types     Enable 16bit types and disable min precision types. Available in HLSL 2018 and shader model 6.2
          -encoding <value>       Set default encoding for text outputs (utf8|utf16) default=utf8
          -export-shaders-only    Only export shaders when compiling a library
          -exports <value>        Specify exports when compiling a library: export1[[,export1_clone,...]=internal_name][;...]
          -E <value>              Entry point name
          -Fc <file>              Output assembly code listing file
          -fdiagnostics-show-option
                                  Print option name with mappable diagnostics
          -Fd <file>              Write debug information to the given file, or automatically named file in directory when ending in '\'
          -Fe <file>              Output warnings and errors to the given file
          -Fh <file>              Output header file containing object code
          -flegacy-macro-expansion
                                  Expand the operands before performing token-pasting operation (fxc behavior)
          -flegacy-resource-reservation
                                  Reserve unused explicit register assignments for compatibility with shader model 5.0 and below
          -fno-diagnostics-show-option
                                  Do not print option name with mappable diagnostics
          -force-rootsig-ver <profile>
                                  force root signature version (rootsig_1_1 if omitted)
          -Fo <file>              Output object file
          -Fre <file>             Output reflection to the given file
          -Frs <file>             Output root signature to the given file
          -Fsh <file>             Output shader hash to the given file
          -Gec                    Enable backward compatibility mode
          -Ges                    Enable strict mode
          -Gfa                    Avoid flow control constructs
          -Gfp                    Prefer flow control constructs
          -Gis                    Force IEEE strictness
          -HV <value>             HLSL version (2016, 2017, 2018). Default is 2018
          -H                      Show header includes and nesting depth
          -ignore-line-directives Ignore line directives
          -I <value>              Add directory to include search path
          -Lx                     Output hexadecimal literals
          -Ni                     Output instruction numbers in assembly listings
          -no-legacy-cbuf-layout  Do not use legacy cbuffer load
          -no-warnings            Suppress warnings
          -No                     Output instruction byte offsets in assembly listings
          -Odump                  Print the optimizer commands.
          -Od                     Disable optimizations
          -pack-optimized         Optimize signature packing assuming identical signature provided for each connecting stage
          -pack-prefix-stable     (default) Pack signatures preserving prefix-stable property - appended elements will not disturb placement of prior elements
          -recompile              recompile from DXIL container with Debug Info or Debug Info bitcode file
          -res-may-alias          Assume that UAVs/SRVs may alias
          -rootsig-define <value> Read root signature from a #define
          -T <profile>            Set target profile.
                <profile>: ps_6_0, ps_6_1, ps_6_2, ps_6_3, ps_6_4, ps_6_5, ps_6_6,
                         vs_6_0, vs_6_1, vs_6_2, vs_6_3, vs_6_4, vs_6_5, vs_6_6,
                         gs_6_0, gs_6_1, gs_6_2, gs_6_3, gs_6_4, gs_6_5, gs_6_6,
                         hs_6_0, hs_6_1, hs_6_2, hs_6_3, hs_6_4, hs_6_5, hs_6_6,
                         ds_6_0, ds_6_1, ds_6_2, ds_6_3, ds_6_4, ds_6_5, ds_6_6,
                         cs_6_0, cs_6_1, cs_6_2, cs_6_3, cs_6_4, cs_6_5, cs_6_6,
                         lib_6_1, lib_6_2, lib_6_3, lib_6_4, lib_6_5, lib_6_6,
                         ms_6_5, ms_6_6,
                         as_6_5, as_6_6,
        
          -Vd                     Disable validation
          -Vi                     Display details about the include process.
          -Vn <name>              Use <name> as variable name in header file
          -WX                     Treat warnings as errors
          -Zi                     Enable debug information
          -Zpc                    Pack matrices in column-major order
          -Zpr                    Pack matrices in row-major order
          -Zsb                    Compute Shader Hash considering only output binary
          -Zss                    Compute Shader Hash considering source information
        
        Optimization Options:
          -O0 Optimization Level 0
          -O1 Optimization Level 1
          -O2 Optimization Level 2
          -O3 Optimization Level 3 (Default)
        
        Rewriter Options:
          -extract-entry-uniforms Move uniform parameters from entry point to global scope
          -global-extern-by-default
                                  Set extern on non-static globals
          -keep-user-macro        Write out user defines after rewritten HLSL
          -line-directive         Add line directive
          -remove-unused-functions
                                  Remove unused functions and types
          -remove-unused-globals  Remove unused static globals and functions
          -skip-fn-body           Translate function definitions to declarations
          -skip-static            Remove static functions and globals when used with -skip-fn-body
          -unchanged              Rewrite HLSL, without changes.
        
        SPIR-V CodeGen Options:
          -fspv-debug=<value>     Specify whitelist of debug info category (file -> source -> line, tool)
          -fspv-extension=<value> Specify SPIR-V extension permitted to use
          -fspv-flatten-resource-arrays
                                  Flatten arrays of resources so each array element takes one binding number
          -fspv-reflect           Emit additional SPIR-V instructions to aid reflection
          -fspv-target-env=<value>
                                  Specify the target environment: vulkan1.0 (default) or vulkan1.1
          -fvk-auto-shift-bindings
                                  Apply fvk-*-shift to resources without an explicit register assignment.
          -fvk-b-shift <shift> <space>
                                  Specify Vulkan binding number shift for b-type register
          -fvk-bind-globals <binding> <set>
                                  Specify Vulkan binding number and set number for the $Globals cbuffer
          -fvk-bind-register <type-number> <space> <binding> <set>
                                  Specify Vulkan descriptor set and binding for a specific register
          -fvk-invert-y           Negate SV_Position.y before writing to stage output in VS/DS/GS to accommodate Vulkan's coordinate system
          -fvk-s-shift <shift> <space>
                                  Specify Vulkan binding number shift for s-type register
          -fvk-t-shift <shift> <space>
                                  Specify Vulkan binding number shift for t-type register
          -fvk-u-shift <shift> <space>
                                  Specify Vulkan binding number shift for u-type register
          -fvk-use-dx-layout      Use DirectX memory layout for Vulkan resources
          -fvk-use-dx-position-w  Reciprocate SV_Position.w after reading from stage input in PS to accommodate the difference between Vulkan and DirectX
          -fvk-use-gl-layout      Use strict OpenGL std140/std430 memory layout for Vulkan resources
          -fvk-use-scalar-layout  Use scalar memory layout for Vulkan resources
          -Oconfig=<value>        Specify a comma-separated list of SPIRV-Tools passes to customize optimization configuration (see http://khr.io/hlsl2spirv#optimization)
          -spirv                  Generate SPIR-V code
        
        Utility Options:
          -dumpbin              Load a binary file rather than compiling
          -extractrootsignature Extract root signature from shader bytecode (must be used with /Fo <file>)
          -getprivate <file>    Save private data from shader blob
          -P <value>            Preprocess to file (must be used alone)
          -Qembed_debug         Embed PDB in shader container (must be used with /Zi)
          -Qstrip_debug         Strip debug information from 4_0+ shader bytecode  (must be used with /Fo <file>)
          -Qstrip_priv          Strip private data from shader bytecode  (must be used with /Fo <file>)
          -Qstrip_reflect       Strip reflection data from shader bytecode  (must be used with /Fo <file>)
          -Qstrip_rootsignature Strip root signature data from shader bytecode  (must be used with /Fo <file>)
          -setprivate <file>    Private data to add to compiled shader blob
          -setrootsignature <file>
                                Attach root signature to shader bytecode
          -verifyrootsignature <file>
                                Verify shader bytecode with root signature
        
        Warning Options:
          -W[no-]<warning> Enable/Disable the specified warning
        */

        inline bool error_check(IDxcResult* dxc_result)
        {
            // Get error buffer
            IDxcBlobEncoding* error_buffer = nullptr;
            HRESULT result = dxc_result->GetErrorBuffer(&error_buffer);
            if (SUCCEEDED(result))
            {
                // Log info, warnings and errors
                stringstream ss(string(static_cast<char*>(error_buffer->GetBufferPointer()), error_buffer->GetBufferSize()));
                string line;
                while (getline(ss, line, '\n'))
                {
                    if (line.find("error") != string::npos)
                    {
                        LOG_ERROR(line);
                    }
                    else if (line.find("warning") != string::npos)
                    {
                        LOG_WARNING(line);
                    }
                    else if (!FileSystem::IsEmptyOrWhitespace(line))
                    {
                        LOG_INFO(line);
                    }
                }
            }
            else
            {
                LOG_ERROR("Failed to get error buffer");
            }

            // Release error buffer
            if (error_buffer)
            {
                error_buffer->Release();
                error_buffer = nullptr;
            }

            // Return status
            dxc_result->GetStatus(&result);
            return result == S_OK;
        }

        class Compiler
        {
        public:
            Compiler()
            {
                DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils));
                DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler));;
            }

            CComPtr<IDxcBlob> Compile(const string& shader, vector<string>& arguments)
            {
                // Get shader source
                DxcBuffer dxc_buffer;
                CComPtr<IDxcBlobEncoding> blob_encoding = nullptr;
                {
                    HRESULT result;
                    if (FileSystem::IsSupportedShaderFile(shader))
                    {
                        result = m_utils->LoadFile(FileSystem::StringToWstring(shader).c_str(), nullptr, &blob_encoding);
                    }
                    else // Source
                    {
                        result = m_utils->CreateBlobFromPinned(shader.c_str(), static_cast<uint32_t>(shader.size()), CP_UTF8, &blob_encoding);
                    }

                    if (FAILED(result))
                    {
                        LOG_ERROR("Failed to load shader source.");
                        return nullptr;
                    }

                    dxc_buffer.Ptr      = blob_encoding->GetBufferPointer();
                    dxc_buffer.Size     = blob_encoding->GetBufferSize();
                    dxc_buffer.Encoding = DXC_CP_ACP; // Assume BOM says UTF8 or UTF16 or this is ANSI text.
                }

                // Convert arguments to wstring
                vector<wstring> arguments_wstring;
                arguments_wstring.reserve(arguments.size());
                for (const string& str : arguments)
                {
                    arguments_wstring.emplace_back(FileSystem::StringToWstring(str));
                }
                // Convert arguments to LPCWSTR
                vector<LPCWSTR> arguments_lpcwstr;
                arguments_lpcwstr.reserve(arguments.size());
                for (const wstring& wstr : arguments_wstring)
                {
                    arguments_lpcwstr.emplace_back(wstr.c_str());
                }

                // Create and include handler
                CComPtr<IDxcIncludeHandler> include_handler;
                m_utils->CreateDefaultIncludeHandler(&include_handler);

                // Compile
                CComPtr<IDxcResult> dxc_result = nullptr;
                {
                    m_compiler->Compile
                    (
                        &dxc_buffer,                                        // Source text to compile
                        arguments_lpcwstr.data(),                           // Array of pointers to arguments
                        static_cast<uint32_t>(arguments_lpcwstr.size()),    // Number of arguments
                        include_handler,                                    // user-provided interface to handle #include directives (optional)
                        IID_PPV_ARGS(&dxc_result)                           // IDxcResult: status, buffer, and errors
                    );

                    if (!error_check(dxc_result))
                    {
                        LOG_ERROR("Failed to compile");
                        return nullptr;
                    }
                }

                // Get compiled shader buffer
                CComPtr<IDxcBlob> blob_compiled = nullptr;
                if (FAILED(dxc_result->GetResult(&blob_compiled)))
                {
                    LOG_ERROR("Failed to get compiled shader buffer");
                    return nullptr;
                }

                return blob_compiled;
            }
            
            CComPtr<IDxcUtils> m_utils          = nullptr;
            CComPtr<IDxcCompiler3> m_compiler   = nullptr;
        };

        static Compiler& Instance()
        {
            static Compiler instance;
            return instance;
        }
    }
    
    void* RHI_Shader::_Compile(const string& shader)
    {
        // Arguments (and defines)
        vector<string> arguments;

        // Arguments
        {
            arguments.emplace_back("-E"); arguments.emplace_back(GetEntryPoint());
            arguments.emplace_back("-T"); arguments.emplace_back(GetTargetProfile());
            arguments.emplace_back("-spirv");                                                                                                               // Generate SPIR-V code
            arguments.emplace_back("-fspv-reflect");                                                                                                        // Emit additional SPIR-V instructions to aid reflection
            arguments.emplace_back("-fspv-target-env=vulkan1.1");                                                                                           // Specify the target environment: vulkan1.0 (default) or vulkan1.1
            arguments.emplace_back("-fvk-b-shift"); arguments.emplace_back(to_string(rhi_shader_shift_buffer));             arguments.emplace_back("all");  // Specify Vulkan binding number shift for b-type (buffer) register
            arguments.emplace_back("-fvk-t-shift"); arguments.emplace_back(to_string(rhi_shader_shift_texture));            arguments.emplace_back("all");  // Specify Vulkan binding number shift for t-type (texture) register
            arguments.emplace_back("-fvk-s-shift"); arguments.emplace_back(to_string(rhi_shader_shift_sampler));            arguments.emplace_back("all");  // Specify Vulkan binding number shift for s-type (sampler) register
            arguments.emplace_back("-fvk-u-shift"); arguments.emplace_back(to_string(rhi_shader_shift_storage_texture));    arguments.emplace_back("all");  // Specify Vulkan binding number shift for u-type (read/write buffer) register
            arguments.emplace_back("-fvk-use-dx-position-w");                                                                                               // Reciprocate SV_Position.w after reading from stage input in PS to accommodate the difference between Vulkan and DirectX
            arguments.emplace_back("-fvk-use-dx-layout");                                                                                                   // Use DirectX memory layout for Vulkan resources
            arguments.emplace_back("-flegacy-macro-expansion");                                                                                             // Expand the operands before performing token-pasting operation (fxc behavior)
            #ifdef DEBUG
            arguments.emplace_back("-Od");                                                                                                                  // Disable optimizations
            arguments.emplace_back("-Zi");                                                                                                                  // Enable debug information
            #endif

            // Negate SV_Position.y before writing to stage output in VS/DS/GS to accommodate Vulkan's coordinate system
            if (m_shader_type == RHI_Shader_Vertex)
            {
                arguments.emplace_back("-fvk-invert-y");
            }

            // Add include directory
            if (FileSystem::IsFile(shader))
            {
                arguments.emplace_back("-I"); arguments.emplace_back(FileSystem::GetDirectoryFromFilePath(shader));
            }
        }

        // Defines
        {
            // Add standard defines
            arguments.emplace_back("-D"); arguments.emplace_back("VS="+ to_string(static_cast<uint8_t>(m_shader_type == RHI_Shader_Vertex)));
            arguments.emplace_back("-D"); arguments.emplace_back("PS="+ to_string(static_cast<uint8_t>(m_shader_type == RHI_Shader_Pixel)));
            arguments.emplace_back("-D"); arguments.emplace_back("CS="+ to_string(static_cast<uint8_t>(m_shader_type == RHI_Shader_Compute)));

            // Add the rest of the defines
            for (const auto& define : m_defines)
            {
                arguments.emplace_back("-D"); arguments.emplace_back(define.first + "=" + define.second);
            }
        }

        // Compile
        if (CComPtr<IDxcBlob> shader_buffer = DxcHelper::Instance().Compile(shader, arguments))
        {
            // Create shader module
            VkShaderModule shader_module            = nullptr;
            VkShaderModuleCreateInfo create_info    = {};
            create_info.sType                       = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            create_info.codeSize                    = static_cast<size_t>(shader_buffer->GetBufferSize());
            create_info.pCode                       = reinterpret_cast<const uint32_t*>(shader_buffer->GetBufferPointer());

            if (!vulkan_utility::error::check(vkCreateShaderModule(m_rhi_device->GetContextRhi()->device, &create_info, nullptr, &shader_module)))
            {
                LOG_ERROR("Failed to create shader module.");
                return nullptr;
            }

            // Reflect shader resources (so that descriptor sets can be created later)
            _Reflect
            (
                m_shader_type,
                reinterpret_cast<uint32_t*>(shader_buffer->GetBufferPointer()),
                static_cast<uint32_t>(shader_buffer->GetBufferSize() / 4)
            );
            
            // Create input layout
            if (m_vertex_type != RHI_Vertex_Type_Unknown)
            {
                if (!m_input_layout->Create(m_vertex_type, nullptr))
                {
                    LOG_ERROR("Failed to create input layout for %s", FileSystem::GetFileNameFromFilePath(shader).c_str());
                    return nullptr;
                }
            }
            
            return static_cast<void*>(shader_module);
        }

        LOG_ERROR("Failed to compile %s", shader.c_str());
        return nullptr;
    }

    void RHI_Shader::_Reflect(const RHI_Shader_Type shader_type, const uint32_t* ptr, const uint32_t size)
    {
        // Initialize compiler with SPIR-V data
        const auto compiler = spirv_cross::CompilerHLSL(ptr, size);

        // The SPIR-V is now parsed, and we can perform reflection on it
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        // Get storage images
        for (const auto& resource : resources.storage_images)
        {
            m_descriptors.emplace_back
            (
                RHI_Descriptor_Type::RHI_Descriptor_Texture,                    // type
                compiler.get_decoration(resource.id, spv::DecorationBinding),   // slot
                shader_type,                                                    // stage
                true,                                                           // is_storage
                false                                                           // is_dynamic_constant_buffer
            );
        }

        // Get constant buffers
        for (const auto& resource : resources.uniform_buffers)
        {
            m_descriptors.emplace_back
            (
                RHI_Descriptor_Type::RHI_Descriptor_ConstantBuffer,             // type
                compiler.get_decoration(resource.id, spv::DecorationBinding),   // slot
                shader_type,                                                    // stage
                false,                                                          // is_storage
                false                                                           // is_dynamic_constant_buffer
            );
        }

        // Get textures
        for (const auto& resource : resources.separate_images)
        {
            m_descriptors.emplace_back
            (
                RHI_Descriptor_Type::RHI_Descriptor_Texture,                    // type
                compiler.get_decoration(resource.id, spv::DecorationBinding),   // slot
                shader_type,                                                    // stage
                false,                                                          // is_storage
                false                                                           // is_dynamic_constant_buffer
            );
        }

        // Get samplers
        for (const auto& resource : resources.separate_samplers)
        {
            m_descriptors.emplace_back
            (
                RHI_Descriptor_Type::RHI_Descriptor_Sampler,                    // type
                compiler.get_decoration(resource.id, spv::DecorationBinding),   // slot
                shader_type,                                                    // stage
                false,                                                          // is_storage
                false                                                           // is_dynamic_constant_buffer
            );
        }
    }
}
