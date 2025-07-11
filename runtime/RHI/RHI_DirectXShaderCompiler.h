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

#pragma once

//= INCLUDES ==========
SP_WARNINGS_OFF
#include <dxc/dxcapi.h>
SP_WARNINGS_ON
//=====================

namespace spartan
{
/*
OVERVIEW: HLSL Compiler for Windows

Version: dxcompiler.dll: 1.9 - 1.8.2505.28 (9efbb6c32); dxil.dll: 1.9(1.8.2505.28)

USAGE: dxc.exe [options] <inputs>

Common Options:
  -help              Display available options
  -Qunused-arguments Don't emit warning for unused driver arguments
  --version          Display compiler version information

Compilation Options:
  -all-resources-bound    Enables agressive flattening
  -auto-binding-space <value>
                          Set auto binding space - enables auto resource binding in libraries
  -Cc                     Output color coded assembly listings
  -default-linkage <value>
                          Set default linkage for non-shader functions when compiling or linking to a library target (internal, external)
  -denorm <value>         select denormal value options (any, preserve, ftz). any is the default.
  -disable-payload-qualifiers
                          Disables support for payload access qualifiers for raytracing payloads in SM 6.7.
  -D <value>              Define macro
  -enable-16bit-types     Enable 16bit types and disable min precision types. Available in HLSL 2018 and shader model 6.2
  -enable-lifetime-markers
                          Enable generation of lifetime markers
  -enable-payload-qualifiers
                          Enables support for payload access qualifiers for raytracing payloads in SM 6.6.
  -encoding <value>       Set default encoding for source inputs and text outputs (utf8|utf16(win)|utf32(*nix)|wide) default=utf8
  -export-shaders-only    Only export shaders when compiling a library
  -exports <value>        Specify exports when compiling a library: export1[[,export1_clone,...]=internal_name][;...]
  -E <value>              Entry point name
  -Fc <file>              Output assembly code listing file
  -fdiagnostics-format=<value>
                          Select diagnostic message format. Supported values: clang, msvc, mdvc-fallback, vi
  -fdiagnostics-show-option
                          Print option name with mappable diagnostics
  -fdisable-loc-tracking  Disable source location tracking in IR. This will break diagnostic generation for late validation. (Ignored if /Zi is passed)
  -Fd <file>              Write debug information to the given file, or automatically named file in directory when ending in '\'
  -Fe <file>              Output warnings and errors to the given file
  -Fh <file>              Output header file containing object code
  -Fi <file>              Set preprocess output file name (with /P)
  -flegacy-macro-expansion
                          Expand the operands before performing token-pasting operation (fxc behavior)
  -flegacy-resource-reservation
                          Reserve unused explicit register assignments for compatibility with shader model 5.0 and below
  -fnew-inlining-behavior Experimental option to use heuristics-driven late inlining and disable alwaysinline annotation for library shaders
  -fno-diagnostics-show-option
                          Do not print option name with mappable diagnostics
  -force-rootsig-ver <profile>
                          force root signature version (rootsig_1_1 if omitted)
  -Fo <file>              Output object file
  -Fre <file>             Output reflection to the given file
  -Frs <file>             Output root signature to the given file
  -Fsh <file>             Output shader hash to the given file
  -ftime-report           Print time report
  -ftime-trace-granularity=<value>
                          Minimum time granularity (in microseconds) traced by time profiler
  -ftime-trace=<value>    Print hierchial time tracing to file
  -ftime-trace            Print hierchial time tracing to stdout
  -Gec                    Enable backward compatibility mode
  -Ges                    Enable strict mode
  -Gfa                    Avoid flow control constructs
  -Gfp                    Prefer flow control constructs
  -Gis                    Force IEEE strictness
  -HV <value>             HLSL version (2016, 2017, 2018, 2021). Default is 2021
  -H                      Show header includes and nesting depth
  -ignore-line-directives Ignore line directives
  -I <value>              Add directory to include search path
  -Lx                     Output hexadecimal literals
  -Ni                     Output instruction numbers in assembly listings
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
        <profile>: ps_6_0, ps_6_1, ps_6_2, ps_6_3, ps_6_4, ps_6_5, ps_6_6, ps_6_7, ps_6_8, ps_6_9,
                 vs_6_0, vs_6_1, vs_6_2, vs_6_3, vs_6_4, vs_6_5, vs_6_6, vs_6_7, vs_6_8, vs_6_9,
                 gs_6_0, gs_6_1, gs_6_2, gs_6_3, gs_6_4, gs_6_5, gs_6_6, gs_6_7, gs_6_8, gs_6_9,
                 hs_6_0, hs_6_1, hs_6_2, hs_6_3, hs_6_4, hs_6_5, hs_6_6, hs_6_7, hs_6_8, hs_6_9,
                 ds_6_0, ds_6_1, ds_6_2, ds_6_3, ds_6_4, ds_6_5, ds_6_6, ds_6_7, ds_6_8, ds_6_9,
                 cs_6_0, cs_6_1, cs_6_2, cs_6_3, cs_6_4, cs_6_5, cs_6_6, cs_6_7, cs_6_8, cs_6_9,
                 lib_6_1, lib_6_2, lib_6_3, lib_6_4, lib_6_5, lib_6_6, lib_6_7, lib_6_8, lib_6_9,
                 ms_6_5, ms_6_6, ms_6_7, ms_6_8, ms_6_9,
                 as_6_5, as_6_6, as_6_7, as_6_8, as_6_9,

  -Vd                     Disable validation
  -verify<value>          Verify diagnostic output using comment directives
  -Vi                     Display details about the include process.
  -Vn <name>              Use <name> as variable name in header file
  -WX                     Treat warnings as errors
  -Zi                     Enable debug information. Cannot be used together with -Zs
  -Zpc                    Pack matrices in column-major order
  -Zpr                    Pack matrices in row-major order
  -Zsb                    Compute Shader Hash considering only output binary
  -Zss                    Compute Shader Hash considering source information
  -Zs                     Generate small PDB with just sources and compile options. Cannot be used together with -Zi

OPTIONS:
  -MD        Write a file with .d extension that will contain the list of the compilation target dependencies.
  -MF <file> Write the specfied file that will contain the list of the compilation target dependencies.
  -M         Dumps the list of the compilation target dependencies.

Optimization Options:
  -ffinite-math-only    Allow optimizations for floating-point arithmetic that assume that arguments and results are not NaNs or +-Infs.
  -fno-finite-math-only Disallow optimizations for floating-point arithmetic that assume that arguments and results are not NaNs or +-Infs.
  -O0                   Optimization Level 0
  -O1                   Optimization Level 1
  -O2                   Optimization Level 2
  -O3                   Optimization Level 3 (Default)

Rewriter Options:
  -decl-global-cb         Collect all global constants outside cbuffer declarations into cbuffer GlobalCB { ... }. Still experimental, not all dependency scenarios handled.
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
  -fspv-debug=<value>     Specify whitelist of debug info category (file -> source -> line, tool, vulkan-with-source)
  -fspv-enable-maximal-reconvergence
                          Enables the MaximallyReconvergesKHR execution mode for this module.
  -fspv-entrypoint-name=<value>
                          Specify the SPIR-V entry point name. Defaults to the HLSL entry point name.
  -fspv-extension=<value> Specify SPIR-V extension permitted to use
  -fspv-flatten-resource-arrays
                          Flatten arrays of resources so each array element takes one binding number
  -fspv-max-id <shift> <space>
                          Set the maximum value for an id in the SPIR-V binary. Default is 0x3FFFFF, which is the largest value all drivers must support.
  -fspv-preserve-bindings Preserves all bindings declared within the module, even when those bindings are unused
  -fspv-preserve-interface
                          Preserves all interface variables in the entry point, even when those variables are unused
  -fspv-print-all         Print the SPIR-V module before each pass and after the last one. Useful for debugging SPIR-V legalization and optimization passes.
  -fspv-reduce-load-size  Replaces loads of composite objects to reduce memory pressure for the loads
  -fspv-reflect           Emit additional SPIR-V instructions to aid reflection
  -fspv-target-env=<value>
                          Specify the target environment: vulkan1.0 (default), vulkan1.1, vulkan1.1spirv1.4, vulkan1.2, vulkan1.3, or universal1.5
  -fspv-use-legacy-buffer-matrix-order
                          Assume the legacy matrix order (row major) when accessing raw buffers (e.g., ByteAdddressBuffer)
  -fspv-use-vulkan-memory-model
                          Generates SPIR-V modules that use the Vulkan memory model instead of GLSL450.
  -fvk-auto-shift-bindings
                          Apply fvk-*-shift to resources without an explicit register assignment.
  -fvk-b-shift <shift> <space>
                          Specify Vulkan binding number shift for b-type register
  -fvk-bind-counter-heap <binding> <set>
                          Specify Vulkan binding number and set number for the counter heap.
  -fvk-bind-globals <binding> <set>
                          Specify Vulkan binding number and set number for the $Globals cbuffer
  -fvk-bind-register <type-number> <space> <binding> <set>
                          Specify Vulkan descriptor set and binding for a specific register
  -fvk-bind-resource-heap <binding> <set>
                          Specify Vulkan binding number and set number for the resource heap.
  -fvk-bind-sampler-heap <binding> <set>
                          Specify Vulkan binding number and set number for the sampler heap.
  -fvk-invert-y           Negate SV_Position.y before writing to stage output in VS/DS/GS/MS/Lib to accommodate Vulkan's coordinate system
  -fvk-s-shift <shift> <space>
                          Specify Vulkan binding number shift for s-type register
  -fvk-support-nonzero-base-instance
                          Follow Vulkan spec to use gl_BaseInstance as the first vertex instance, which makes SV_InstanceID = gl_InstanceIndex - gl_BaseInstance (without this option, SV_InstanceID = gl_InstanceIndex)
  -fvk-support-nonzero-base-vertex
                          Follow Vulkan spec to use gl_BaseVertex as the first vertex, which makes SV_VertexID = gl_VertexIndex - gl_BaseVertex (without this option, SV_VertexID = gl_VertexIndex)
  -fvk-t-shift <shift> <space>
                          Specify Vulkan binding number shift for t-type register
  -fvk-u-shift <shift> <space>
                          Specify Vulkan binding number shift for u-type register
  -fvk-use-dx-layout      Use DirectX memory layout for Vulkan resources
  -fvk-use-dx-position-w  Reciprocate SV_Position.w after reading from stage input in PS to accommodate the difference between Vulkan and DirectX
  -fvk-use-gl-layout      Use strict OpenGL std140/std430 memory layout for Vulkan resources
  -fvk-use-scalar-layout  Use scalar memory layout for Vulkan resources
  -metal                  Generate Metal code
  -Oconfig=<value>        Specify a comma-separated list of SPIRV-Tools passes to customize optimization configuration (see http://khr.io/hlsl2spirv#optimization)
  -spirv                  Generate SPIR-V code

Utility Options:
  -dumpbin              Load a binary file rather than compiling
  -extractrootsignature Extract root signature from shader bytecode (must be used with /Fo <file>)
  -getprivate <file>    Save private data from shader blob
  -link                 Link list of libraries provided in <inputs> argument separated by ';'
  -P                    Preprocess to file
  -Qembed_debug         Embed PDB in shader container (must be used with /Zi)
  -Qsource_in_debug_module
                        Embed source code in PDB
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

    static bool error_check(IDxcResult* dxc_result)
    {
        // get error buffer
        IDxcBlobEncoding* error_buffer = nullptr;
        HRESULT result = dxc_result->GetErrorBuffer(&error_buffer);
        if (SUCCEEDED(result))
        {
            // log info, warnings and errors
            std::stringstream ss(std::string(static_cast<char*>(error_buffer->GetBufferPointer()), error_buffer->GetBufferSize()));
            std::string line;
            while (getline(ss, line, '\n'))
            {
                if (line.find("error") != std::string::npos)
                {
                    SP_LOG_ERROR(line);
                }
                else if (line.find("warning") != std::string::npos)
                {
                    SP_LOG_WARNING(line);
                }
                else if (!FileSystem::IsEmptyOrWhitespace(line))
                {
                    SP_LOG_INFO(line);
                }
            }
        }
        else
        {
            SP_LOG_ERROR("Failed to get error buffer");
        }

        // release error buffer
        if (error_buffer)
        {
            error_buffer->Release();
            error_buffer = nullptr;
        }

        // return status
        dxc_result->GetStatus(&result);
        return result == S_OK;
    }

    class DirectXShaderCompiler
    {
    public:
        static IDxcResult* Compile(const std::string& source, std::vector<std::string>& arguments)
        {
            // static dxc interfaces
            static IDxcUtils* m_utils = nullptr;
            static IDxcCompiler3* m_compiler = nullptr;

            // initialize compiler and utils
            if (!m_compiler || !m_utils)
            {
                if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler))) ||
                    FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils))))
                {
                    SP_LOG_ERROR("Failed to create DirectXShaderCompiler interfaces");
                    return nullptr;
                }

                // log version info
                IDxcVersionInfo* version_info = nullptr;
                if (SUCCEEDED(m_compiler->QueryInterface(&version_info)))
                {
                    UINT32 major = 0, minor = 0;
                    version_info->GetVersion(&major, &minor);

                    std::ostringstream stream;
                    stream << major << "." << minor;
                    Settings::RegisterThirdPartyLib("DirectXShaderCompiler", stream.str(), "https://github.com/microsoft/DirectXShaderCompiler");

                    version_info->Release();
                }
                else
                {
                    SP_LOG_ERROR("Failed to get DirectXShaderCompiler version info");
                }
            }

            // create blob from source
            IDxcBlobEncoding* blob_encoding = nullptr;
            if (FAILED(m_utils->CreateBlobFromPinned(source.c_str(), static_cast<uint32_t>(source.size()), CP_UTF8, &blob_encoding)))
            {
                SP_LOG_ERROR("Failed to create shader blob from source.");
                return nullptr;
            }

            DxcBuffer dxc_buffer = {};
            dxc_buffer.Ptr       = blob_encoding->GetBufferPointer();
            dxc_buffer.Size      = blob_encoding->GetBufferSize();
            dxc_buffer.Encoding  = DXC_CP_UTF8;

            // convert args to wide strings
            std::vector<std::wstring> arguments_wstring(arguments.size());
            std::vector<LPCWSTR> arguments_lpcwstr(arguments.size());
            for (size_t i = 0; i < arguments.size(); ++i)
            {
                arguments_wstring[i] = FileSystem::StringToWstring(arguments[i]);
                arguments_lpcwstr[i] = arguments_wstring[i].c_str();
            }

            // compile shader
            IDxcResult* dxc_result = nullptr;
            HRESULT hr = m_compiler->Compile(
                &dxc_buffer,
                arguments_lpcwstr.data(),
                static_cast<uint32_t>(arguments_lpcwstr.size()),
                nullptr, // optionally pass a real include handler
                IID_PPV_ARGS(&dxc_result)
            );

            // release the source blob
            blob_encoding->Release();

            if (FAILED(hr) || !error_check(dxc_result))
            {
                SP_LOG_ERROR("Shader compilation failed.");
                if (dxc_result)
                {
                    dxc_result->Release();
                    dxc_result = nullptr;
                }
            }

            return dxc_result;
        }
    };
}
