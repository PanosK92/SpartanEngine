/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ============================
#include "pch.h"
#include "../RHI_Shader.h"
#include "../RHI_Device.h"
#include "../RHI_InputLayout.h"
#include "../../geometry/GeneratedCache.h"
#include "../../file_system/FileSystem.h"
#include "WebGPU_Internal.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#if !defined(__EMSCRIPTEN__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "../RHI_DirectXShaderCompiler.h"
SP_WARNINGS_OFF
#include <spirv_cross/spirv_hlsl.hpp>
SP_WARNINGS_ON
#endif
//=======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // hlsl becomes spir-v through dxc, spir-v becomes wgsl through tint, the browser only ever sees the cached wgsl
    namespace
    {
        const char* cache_version = "webgpu_shader_v4";

        // wgsl can't test for nan/inf, has no non-uniform indexing qualifier and browsers don't allow subgroups in every stage
        // so the wave intrinsics run as a single lane, every lane is its own wave which keeps the per-wave atomics correct
        const char* source_prelude =
            "#define isnan(x) ((asuint(x) & 0x7fffffffu) > 0x7f800000u)\n"
            "#define isinf(x) ((asuint(x) & 0x7fffffffu) == 0x7f800000u)\n"
            "#define isfinite(x) ((asuint(x) & 0x7f800000u) != 0x7f800000u)\n"
            "#define NonUniformResourceIndex(x) (x)\n"
            "#define WaveIsFirstLane() (true)\n"
            "#define WaveGetLaneIndex() (0u)\n"
            "#define WaveGetLaneCount() (1u)\n"
            "#define WaveReadLaneFirst(x) (x)\n"
            "#define WaveReadLaneAt(x, i) (x)\n"
            "#define WaveActiveSum(x) (x)\n"
            "#define WaveActiveMax(x) (x)\n"
            "#define WaveActiveMin(x) (x)\n"
            "#define WaveActiveBitOr(x) (x)\n"
            "#define WaveActiveBitAnd(x) (x)\n"
            "#define WaveActiveAnyTrue(x) (x)\n"
            "#define WaveActiveAllTrue(x) (x)\n"
            "#define WaveActiveCountBits(x) ((x) ? 1u : 0u)\n"
            "#define WavePrefixCountBits(x) (0u)\n"
            "#define WavePrefixSum(x) ((x) * 0)\n"
            "#define WaveActiveBallot(x) uint4((x) ? 1u : 0u, 0u, 0u, 0u)\n"
            "#line 1\n";

        struct CachedDescriptor
        {
            char name[64]                  = {};
            RHI_Descriptor_Type type       = RHI_Descriptor_Type::Max;
            RHI_Image_Layout layout        = RHI_Image_Layout::Max;
            uint32_t slot                  = 0;
            uint32_t stage                 = 0;
            uint32_t struct_size           = 0;
            uint32_t array_length          = 0;
            uint32_t space                 = 0;
            uint32_t as_array              = 0;
            uint32_t used                  = 0;
        };

        bool is_identifier(const char c)
        {
            return isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        size_t skip_spaces(const string& text, size_t i)
        {
            while (i < text.size() && isspace(static_cast<unsigned char>(text[i])))
            {
                i++;
            }
            return i;
        }

        // core webgpu has no arrays of textures, every material samples one white texture until binding arrays exist
        void rewrite_material_textures(string& source)
        {
            const string name        = "material_textures[";
            const string replacement = "material_textures_webgpu";
            size_t position          = 0;
            while ((position = source.find(name, position)) != string::npos)
            {
                if (position > 0 && is_identifier(source[position - 1]))
                {
                    position += name.size();
                    continue;
                }

                size_t end  = position + name.size();
                int depth   = 1;
                while (end < source.size() && depth > 0)
                {
                    depth += source[end] == '[' ? 1 : (source[end] == ']' ? -1 : 0);
                    end++;
                }
                source.replace(position, end - position, replacement);
                position += replacement.size();
            }
        }

        void replace_all(string& source, const string& from, const string& to)
        {
            size_t position = 0;
            while ((position = source.find(from, position)) != string::npos)
            {
                source.replace(position, from.size(), to);
                position += to.size();
            }
        }

        // wgsl storage textures have a fixed texel format, unknown ones get the format most textures of their type use
        const char* storage_format(string type)
        {
            type.erase(remove_if(type.begin(), type.end(), [](char c) { return isspace(static_cast<unsigned char>(c)); }), type.end());
            if (type == "float" || type == "half" || type == "min16float")
                return "r32f";
            if (type == "float2" || type == "half2")
                return "rg16f";
            if (type == "uint")
                return "r32ui";
            if (type == "uint2")
                return "rg32ui";
            if (type == "uint4")
                return "rgba32ui";
            if (type == "int")
                return "r32i";
            return "rgba16f";
        }

        void rewrite_storage_textures(string& source)
        {
            const string name     = "RWTexture";
            const string attribute = "[[vk::image_format(\"";
            size_t position       = 0;
            while ((position = source.find(name, position)) != string::npos)
            {
                const size_t start = position;
                position          += name.size();
                if (start > 0 && is_identifier(source[start - 1]))
                    continue;

                // RWTexture2D<type> name[n] : register(...), anything else is a parameter, a member or a macro
                size_t i = position;
                while (i < source.size() && is_identifier(source[i]))
                {
                    i++;
                }
                i = skip_spaces(source, i);
                if (i >= source.size() || source[i] != '<')
                    continue;
                const size_t type_end = source.find('>', i);
                if (type_end == string::npos)
                    continue;
                const string type = source.substr(i + 1, type_end - i - 1);
                i = skip_spaces(source, type_end + 1);
                const size_t identifier = i;
                while (i < source.size() && is_identifier(source[i]))
                {
                    i++;
                }
                if (i == identifier)
                    continue;
                i = skip_spaces(source, i);
                if (i < source.size() && source[i] == '[')
                {
                    i = source.find(']', i);
                    if (i == string::npos)
                        continue;
                    i = skip_spaces(source, i + 1);
                }
                if (i >= source.size() || source[i] != ':')
                    continue;
                i = skip_spaces(source, i + 1);
                if (source.compare(i, 8, "register") != 0)
                    continue;

                // walk back over qualifiers to where an attribute would sit
                size_t declaration = start;
                size_t back        = start;
                while (back > 0 && isspace(static_cast<unsigned char>(source[back - 1])))
                {
                    back--;
                }
                const string qualifier = "globallycoherent";
                if (back >= qualifier.size() && source.compare(back - qualifier.size(), qualifier.size(), qualifier) == 0)
                {
                    declaration = back - qualifier.size();
                    back        = declaration;
                    while (back > 0 && isspace(static_cast<unsigned char>(source[back - 1])))
                    {
                        back--;
                    }
                }

                const char* format = storage_format(type);
                if (back >= 2 && source.compare(back - 2, 2, "]]") == 0)
                {
                    const size_t attribute_start = source.rfind(attribute, back);
                    if (attribute_start != string::npos && back - attribute_start < 64)
                    {
                        const size_t value_start = attribute_start + attribute.size();
                        const size_t value_end   = source.find('"', value_start);
                        if (value_end != string::npos && source.compare(value_start, value_end - value_start, "unknown") == 0)
                        {
                            source.replace(value_start, value_end - value_start, format);
                            position = value_start + strlen(format);
                        }
                        continue;
                    }
                }

                const string inserted = attribute + format + "\")]] ";
                source.insert(declaration, inserted);
                position += inserted.size();
            }
        }

        string rewrite_source(string source)
        {
            source.erase(remove(source.begin(), source.end(), '\r'), source.end());
            rewrite_material_textures(source);
            replace_all(source, "samplers_comparison[]", "samplers_comparison[1]");
            replace_all(source, "SamplerState samplers[]", "SamplerState samplers[8]");
            rewrite_storage_textures(source);
            return source_prelude + source;
        }

        uint64_t compute_key(const string& source, const unordered_map<string, string>& defines, const RHI_Shader_Type type, const char* entry_point, const char* profile)
        {
            generated_cache::Hash hash;
            hash.Add(string(cache_version));
            hash.Add(source);

            vector<pair<string, string>> sorted(defines.begin(), defines.end());
            sort(sorted.begin(), sorted.end());
            for (const auto& [define, value] : sorted)
            {
                hash.Add(define);
                hash.Add(value);
            }

            hash.Add(static_cast<uint32_t>(type));
            hash.Add(string(entry_point ? entry_point : ""));
            hash.Add(string(profile ? profile : ""));
            return hash.value;
        }

        filesystem::path cache_path(const uint64_t key)
        {
            return generated_cache::Path(FileSystem::GetExecutableDirectory() + "/cache", "webgpu_shaders", key);
        }

        uint64_t to_stage(const RHI_Shader_Type type)
        {
            switch (type)
            {
                case RHI_Shader_Type::Vertex:  return WGPUShaderStage_Vertex;
                case RHI_Shader_Type::Pixel:   return WGPUShaderStage_Fragment;
                case RHI_Shader_Type::Compute: return WGPUShaderStage_Compute;
                default:                       return WGPUShaderStage_None;
            }
        }

        const pair<const char*, WGPUTextureFormat> storage_formats[] =
        {
            { "rgba8unorm",    WGPUTextureFormat_RGBA8Unorm },
            { "rgba8snorm",    WGPUTextureFormat_RGBA8Snorm },
            { "rgba8uint",     WGPUTextureFormat_RGBA8Uint },
            { "rgba16float",   WGPUTextureFormat_RGBA16Float },
            { "rgba16uint",    WGPUTextureFormat_RGBA16Uint },
            { "rgba32float",   WGPUTextureFormat_RGBA32Float },
            { "rgba32uint",    WGPUTextureFormat_RGBA32Uint },
            { "r32float",      WGPUTextureFormat_R32Float },
            { "r32uint",       WGPUTextureFormat_R32Uint },
            { "r32sint",       WGPUTextureFormat_R32Sint },
            { "rg32float",     WGPUTextureFormat_RG32Float },
            { "rg32uint",      WGPUTextureFormat_RG32Uint },
            { "r8unorm",       WGPUTextureFormat_R8Unorm },
            { "rg8unorm",      WGPUTextureFormat_RG8Unorm },
            { "r16float",      WGPUTextureFormat_R16Float },
            { "rg16float",     WGPUTextureFormat_RG16Float },
            { "r16uint",       WGPUTextureFormat_R16Uint },
            { "r8uint",        WGPUTextureFormat_R8Uint },
            { "rgb10a2unorm",  WGPUTextureFormat_RGB10A2Unorm },
            { "rg11b10ufloat", WGPUTextureFormat_RG11B10Ufloat },
            { "bgra8unorm",    WGPUTextureFormat_BGRA8Unorm },
            { "r16unorm",      WGPUTextureFormat_R16Unorm },
            { "rgba16unorm",   WGPUTextureFormat_RGBA16Unorm },
        };

        WGPUTextureFormat parse_storage_format(const string& format)
        {
            for (const auto& [name, value] : storage_formats)
            {
                if (format == name)
                    return value;
            }
            return WGPUTextureFormat_Undefined;
        }

        WGPUTextureViewDimension parse_dimension(const string& type)
        {
            // longest names first, texture_2d is a prefix of texture_2d_array
            if (type.find("cube_array") != string::npos) return WGPUTextureViewDimension_CubeArray;
            if (type.find("2d_array") != string::npos)   return WGPUTextureViewDimension_2DArray;
            if (type.find("cube") != string::npos)       return WGPUTextureViewDimension_Cube;
            if (type.find("3d") != string::npos)         return WGPUTextureViewDimension_3D;
            if (type.find("1d") != string::npos)         return WGPUTextureViewDimension_1D;
            return WGPUTextureViewDimension_2D;
        }

        // the wgsl declarations say exactly what each binding is, tint already resolved spir-v's image types
        vector<webgpu::Binding> parse_bindings(const string& wgsl, const uint64_t stage)
        {
            vector<webgpu::Binding> bindings;
            size_t position = 0;
            while ((position = wgsl.find("@group(", position)) != string::npos)
            {
                webgpu::Binding binding;
                binding.group  = static_cast<uint32_t>(strtoul(wgsl.c_str() + position + 7, nullptr, 10));
                const size_t b = wgsl.find("@binding(", position);
                const size_t e = wgsl.find(';', position);
                if (b == string::npos || e == string::npos)
                    break;

                binding.binding          = static_cast<uint32_t>(strtoul(wgsl.c_str() + b + 9, nullptr, 10));
                binding.visibility       = stage;
                const string declaration = wgsl.substr(position, e - position);
                position                 = e;

                const size_t colon = declaration.rfind(':');
                if (colon == string::npos)
                    continue;
                string type = declaration.substr(colon + 1);
                type.erase(remove_if(type.begin(), type.end(), [](char c) { return isspace(static_cast<unsigned char>(c)); }), type.end());

                if (declaration.find("var<uniform>") != string::npos)
                {
                    binding.kind = webgpu::Kind::Uniform;
                }
                else if (declaration.find("var<storage") != string::npos)
                {
                    binding.kind = declaration.find("read_write") != string::npos ? webgpu::Kind::Storage : webgpu::Kind::StorageRead;
                }
                else if (type == "sampler")
                {
                    binding.kind = webgpu::Kind::Sampler;
                }
                else if (type == "sampler_comparison")
                {
                    binding.kind = webgpu::Kind::SamplerComparison;
                }
                else if (type.rfind("texture_storage_", 0) == 0)
                {
                    binding.kind      = webgpu::Kind::StorageTexture;
                    binding.dimension = parse_dimension(type.substr(0, type.find('<')));
                    const size_t open  = type.find('<');
                    const size_t comma = type.find(',', open);
                    const size_t close = type.find('>', comma);
                    if (open != string::npos && comma != string::npos && close != string::npos)
                    {
                        binding.storage_format = parse_storage_format(type.substr(open + 1, comma - open - 1));
                        const string access    = type.substr(comma + 1, close - comma - 1);
                        binding.access         = access == "read" ? WGPUStorageTextureAccess_ReadOnly : (access == "write" ? WGPUStorageTextureAccess_WriteOnly : WGPUStorageTextureAccess_ReadWrite);
                    }
                }
                else if (type.rfind("texture_", 0) == 0)
                {
                    binding.kind         = webgpu::Kind::Texture;
                    binding.dimension    = parse_dimension(type.substr(0, type.find('<')));
                    binding.multisampled = type.find("multisampled") != string::npos ? 1 : 0;
                    if (type.find("depth") != string::npos)
                    {
                        binding.sample_type = WGPUTextureSampleType_Depth;
                    }
                    else if (type.find("<u32>") != string::npos)
                    {
                        binding.sample_type = WGPUTextureSampleType_Uint;
                    }
                    else if (type.find("<i32>") != string::npos)
                    {
                        binding.sample_type = WGPUTextureSampleType_Sint;
                    }
                    else
                    {
                        binding.sample_type = WGPUTextureSampleType_Float;
                    }
                }
                else
                {
                    continue;
                }

                bindings.push_back(binding);
            }
            return bindings;
        }

        // tint declares every storage texture read_write, which only a few formats allow, the ones nothing reads become write only
        void restrict_unread_storage_textures(string& wgsl)
        {
            auto count = [&wgsl](const string& text, const bool identifier)
            {
                uint32_t found  = 0;
                size_t position = 0;
                while ((position = wgsl.find(text, position)) != string::npos)
                {
                    const size_t after = position + text.size();
                    const bool whole   = !identifier || ((position == 0 || !is_identifier(wgsl[position - 1])) && (after >= wgsl.size() || !is_identifier(wgsl[after])));
                    found             += whole ? 1 : 0;
                    position           = after;
                }
                return found;
            };

            const string access = "read_write>";
            size_t position     = 0;
            while ((position = wgsl.find("@group(", position)) != string::npos)
            {
                const size_t end        = wgsl.find(';', position);
                const size_t var        = wgsl.find("var ", position);
                const size_t colon      = wgsl.find(':', position);
                const size_t storage    = wgsl.find("texture_storage_", position);
                const size_t access_pos = wgsl.find(access, position);
                const size_t start      = position;
                position                = end;
                if (end == string::npos || var > end || colon > end || storage > end || access_pos > end)
                    continue;

                string name = wgsl.substr(var + 4, colon - var - 4);
                name.erase(remove_if(name.begin(), name.end(), [](char c) { return isspace(static_cast<unsigned char>(c)); }), name.end());
                if (name.empty())
                    continue;

                // only the declaration, stores and size queries, anything else may read it or pass it on
                const uint32_t uses  = count(name, true);
                const uint32_t known = 1 + count("textureStore(" + name + ",", false) + count("textureDimensions(" + name + ")", false) + count("textureDimensions(" + name + ",", false) + count("textureNumLayers(" + name + ")", false);
                if (uses != known)
                    continue;

                wgsl.replace(access_pos, access.size(), "write>");
                position = wgsl.find(';', start);
            }
        }

        WGPUShaderModule create_module(const string& wgsl, const string& name)
        {
            WGPUShaderSourceWGSL source       = WGPU_SHADER_SOURCE_WGSL_INIT;
            source.code                       = { wgsl.data(), wgsl.size() };
            WGPUShaderModuleDescriptor module = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
            module.nextInChain                = &source.chain;
            module.label                      = webgpu::label(name.c_str());
            return wgpuDeviceCreateShaderModule(RHI_Context::device, &module);
        }

#if !defined(__EMSCRIPTEN__)
        // groups follow the plan in WebGPU_Internal.h, b1 in space 0 is the push constant uniform
        void remap_bindings(vector<uint32_t>& words)
        {
            const uint32_t op_decorate          = 71;
            const uint32_t decoration_binding   = 33;
            const uint32_t decoration_set       = 34;
            unordered_map<uint32_t, uint32_t*> sets;
            unordered_map<uint32_t, uint32_t*> bindings;

            for (size_t i = 5; i < words.size();)
            {
                const uint32_t count  = words[i] >> 16;
                const uint32_t opcode = words[i] & 0xffff;
                if (count == 0)
                    break;

                if (opcode == op_decorate && count >= 4)
                {
                    if (words[i + 2] == decoration_set)
                    {
                        sets[words[i + 1]] = &words[i + 3];
                    }
                    else if (words[i + 2] == decoration_binding)
                    {
                        bindings[words[i + 1]] = &words[i + 3];
                    }
                }
                i += count;
            }

            for (auto& [id, set] : sets)
            {
                auto it = bindings.find(id);
                if (it == bindings.end())
                    continue;

                uint32_t& binding = *it->second;
                if (*set == 0 && binding == rhi_shader_register_shift_b + 1)
                {
                    *set    = webgpu::group_push;
                    binding = 0;
                }
                else
                {
                    *set = *set == 0 ? webgpu::group_pass : webgpu::group_bindless;
                }
            }
        }

        // the vertex stage can't see writable storage, so every buffer a vertex shader declares is marked read only
        void make_buffers_read_only(vector<uint32_t>& words)
        {
            const uint32_t op_variable            = 59;
            const uint32_t op_decorate            = 71;
            const uint32_t decoration_nonwritable = 24;
            const uint32_t storage_uniform        = 2;
            const uint32_t storage_buffer         = 12;

            vector<uint32_t> variables;
            size_t insert_at = 0;
            for (size_t i = 5; i < words.size();)
            {
                const uint32_t count  = words[i] >> 16;
                const uint32_t opcode = words[i] & 0xffff;
                if (count == 0)
                    break;

                if (opcode == op_decorate && insert_at == 0)
                {
                    insert_at = i;
                }
                else if (opcode == op_variable && count >= 4 && (words[i + 3] == storage_buffer || words[i + 3] == storage_uniform))
                {
                    variables.push_back(words[i + 2]);
                }
                i += count;
            }

            if (insert_at == 0 || variables.empty())
                return;

            vector<uint32_t> decorations;
            for (const uint32_t id : variables)
            {
                decorations.push_back((3u << 16) | op_decorate);
                decorations.push_back(id);
                decorations.push_back(decoration_nonwritable);
            }
            words.insert(words.begin() + insert_at, decorations.begin(), decorations.end());
        }

        // tint ships as an executable next to the engine, it compiles one module per run
        string run_tint(const vector<uint32_t>& words, const string& name)
        {
            static atomic<uint32_t> counter = 0;
            const filesystem::path directory = filesystem::temp_directory_path() / "spartan_webgpu";
            error_code error;
            filesystem::create_directories(directory, error);

            const string stem             = to_string(GetCurrentProcessId()) + "_" + to_string(counter++);
            const filesystem::path input  = directory / (stem + ".spv");
            const filesystem::path output = directory / (stem + ".wgsl");
            const filesystem::path log    = directory / (stem + ".log");
            {
                ofstream file(input, ios::binary);
                file.write(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(uint32_t));
            }

            const string tint = FileSystem::GetExecutableDirectory() + "/tint.exe";
            wstring command   = L"\"" + filesystem::path(tint).wstring() + L"\" --allow-non-uniform-derivatives true -o \"" + output.wstring() + L"\" \"" + input.wstring() + L"\"";

            SECURITY_ATTRIBUTES security = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
            HANDLE log_handle            = CreateFileW(log.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            // shaders compile on many threads, only the log may be inherited or a child holds another thread's files open
            SIZE_T attribute_size = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
            vector<uint8_t> attribute_storage(attribute_size);
            LPPROC_THREAD_ATTRIBUTE_LIST attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
            InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size);
            UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &log_handle, sizeof(HANDLE), nullptr, nullptr);

            STARTUPINFOEXW startup         = {};
            startup.StartupInfo.cb         = sizeof(startup);
            startup.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdOutput = log_handle;
            startup.StartupInfo.hStdError  = log_handle;
            startup.lpAttributeList        = attributes;
            PROCESS_INFORMATION process    = {};

            string wgsl;
            const bool created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &process);
            DeleteProcThreadAttributeList(attributes);
            if (created)
            {
                WaitForSingleObject(process.hProcess, INFINITE);
                DWORD exit_code = 1;
                GetExitCodeProcess(process.hProcess, &exit_code);
                CloseHandle(process.hProcess);
                CloseHandle(process.hThread);
                CloseHandle(log_handle);
                log_handle = INVALID_HANDLE_VALUE;

                ifstream file(output, ios::binary);
                if (exit_code == 0 && file)
                {
                    wgsl.assign(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
                }
                else
                {
                    ifstream file_log(log, ios::binary);
                    string message((istreambuf_iterator<char>(file_log)), istreambuf_iterator<char>());
                    if (message.size() > 2048)
                    {
                        message.resize(2048);
                    }
                    SP_LOG_ERROR("tint failed on %s: %s", name.c_str(), message.c_str());
                }
            }
            else
            {
                SP_LOG_ERROR("Failed to run %s", tint.c_str());
            }

            if (log_handle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(log_handle);
            }
            // a module tint rejects stays behind so it can be disassembled
            if (wgsl.empty())
            {
                SP_LOG_INFO("tint input kept at %s", input.string().c_str());
            }
            else
            {
                filesystem::remove(input, error);
            }
            filesystem::remove(output, error);
            filesystem::remove(log, error);
            return wgsl;
        }

        WGPUVertexFormat to_vertex_format(const spirv_cross::SPIRType& type)
        {
            const uint32_t size = type.vecsize;
            if (type.basetype == spirv_cross::SPIRType::UInt)
                return size == 1 ? WGPUVertexFormat_Uint32 : size == 2 ? WGPUVertexFormat_Uint32x2 : size == 3 ? WGPUVertexFormat_Uint32x3 : WGPUVertexFormat_Uint32x4;
            if (type.basetype == spirv_cross::SPIRType::Int)
                return size == 1 ? WGPUVertexFormat_Sint32 : size == 2 ? WGPUVertexFormat_Sint32x2 : size == 3 ? WGPUVertexFormat_Sint32x3 : WGPUVertexFormat_Sint32x4;
            return size == 1 ? WGPUVertexFormat_Float32 : size == 2 ? WGPUVertexFormat_Float32x2 : size == 3 ? WGPUVertexFormat_Float32x3 : WGPUVertexFormat_Float32x4;
        }

        // what spir-v knows and wgsl doesn't say, which samplers each texture is used with and the vertex input names
        // which samplers each texture meets in an OpSampledImage, a linear scan since spirv-cross's combined sampler
        // analysis walks the call graph and takes minutes on the large pixel shaders
        void collect_sampler_masks(const vector<uint32_t>& words, vector<webgpu::Binding>& bindings)
        {
            const uint32_t op_decorate      = 71;
            const uint32_t op_load          = 61;
            const uint32_t op_access_chain  = 65;
            const uint32_t op_in_bounds     = 66;
            const uint32_t op_copy_object   = 83;
            const uint32_t op_sampled_image = 86;

            unordered_map<uint32_t, uint32_t> origin; // result id -> the variable it came from
            unordered_map<uint32_t, uint32_t> sets;
            unordered_map<uint32_t, uint32_t> slots;
            vector<pair<uint32_t, uint32_t>> pairs;   // image variable, sampler variable
            auto resolve = [&origin](const uint32_t id)
            {
                auto it = origin.find(id);
                return it != origin.end() ? it->second : id;
            };

            for (size_t i = 5; i < words.size();)
            {
                const uint32_t count  = words[i] >> 16;
                const uint32_t opcode = words[i] & 0xFFFF;
                if (count == 0 || i + count > words.size())
                    break;

                if (opcode == op_decorate && count >= 4)
                {
                    if (words[i + 2] == spv::DecorationDescriptorSet)
                    {
                        sets[words[i + 1]] = words[i + 3];
                    }
                    else if (words[i + 2] == spv::DecorationBinding)
                    {
                        slots[words[i + 1]] = words[i + 3];
                    }
                }
                else if ((opcode == op_load || opcode == op_access_chain || opcode == op_in_bounds || opcode == op_copy_object) && count >= 4)
                {
                    origin[words[i + 2]] = resolve(words[i + 3]);
                }
                else if (opcode == op_sampled_image && count >= 5)
                {
                    pairs.emplace_back(resolve(words[i + 3]), resolve(words[i + 4]));
                }
                i += count;
            }

            for (const auto& [image, sampler] : pairs)
            {
                if (!slots.count(image) || !slots.count(sampler))
                    continue;

                const uint32_t image_group     = sets[image];
                const uint32_t image_binding   = slots[image];
                const uint32_t sampler_binding = slots[sampler];
                if (sampler_binding < webgpu::binding_samplers || sampler_binding >= webgpu::binding_samplers + 64)
                    continue;

                for (webgpu::Binding& binding : bindings)
                {
                    if (binding.group == image_group && binding.binding == image_binding)
                    {
                        binding.sampler_mask |= 1ull << (sampler_binding - webgpu::binding_samplers);
                    }
                }
            }
        }

        void reflect_extra(const vector<uint32_t>& words, const char* entry_point, const RHI_Shader_Type type, vector<webgpu::Binding>& bindings, vector<webgpu::VertexInput>& vertex_inputs)
        {
            collect_sampler_masks(words, bindings);
            if (type != RHI_Shader_Type::Vertex)
                return;

            spirv_cross::CompilerHLSL compiler(words.data(), words.size());
            for (const spirv_cross::EntryPoint& candidate : compiler.get_entry_points_and_stages())
            {
                if (candidate.name == entry_point)
                {
                    compiler.set_entry_point(candidate.name, candidate.execution_model);
                    break;
                }
            }

            const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
            for (const spirv_cross::Resource& input : resources.stage_inputs)
            {
                const spirv_cross::SPIRType& input_type = compiler.get_type(input.type_id);
                string semantic                         = input.name;
                const size_t dot                        = semantic.rfind('.');
                if (dot != string::npos)
                {
                    semantic = semantic.substr(dot + 1);
                }

                // matrices take a location per column
                const uint32_t location = compiler.get_decoration(input.id, spv::DecorationLocation);
                for (uint32_t column = 0; column < max(1u, input_type.columns); column++)
                {
                    webgpu::VertexInput vertex_input;
                    snprintf(vertex_input.semantic, sizeof(vertex_input.semantic), "%s", semantic.c_str());
                    vertex_input.location = location + column;
                    vertex_input.format   = to_vertex_format(input_type);
                    vertex_inputs.push_back(vertex_input);
                }
            }
        }

        void spirv_resources_to_descriptors(
            const spirv_cross::CompilerHLSL& compiler,
            vector<RHI_Descriptor>& descriptors,
            const spirv_cross::SmallVector<spirv_cross::Resource>& resources,
            const RHI_Descriptor_Type descriptor_type,
            const RHI_Shader_Type shader_stage
        )
        {
            const RHI_Image_Layout layout = (descriptor_type == RHI_Descriptor_Type::TextureStorage || descriptor_type == RHI_Descriptor_Type::Image) ? RHI_Image_Layout::General : RHI_Image_Layout::Max;
            for (const spirv_cross::Resource& resource : resources)
            {
                const uint32_t slot              = compiler.get_decoration(resource.id, spv::DecorationBinding);
                const uint32_t space             = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                const spirv_cross::SPIRType type = compiler.get_type(resource.type_id);
                const bool is_array              = !type.array.empty();
                uint32_t array_length            = is_array ? type.array[0] : 0;
                uint32_t size                    = 0;
                if (descriptor_type == RHI_Descriptor_Type::ConstantBuffer)
                {
                    size = static_cast<uint32_t>(compiler.get_declared_struct_size(type));
                }
                if (is_array && array_length == 0)
                {
                    array_length = rhi_max_array_size;
                }
                descriptors.emplace_back(resource.name, descriptor_type, layout, slot, rhi_shader_type_to_mask(shader_stage), size, is_array, array_length, space, true);
            }
        }
#endif
    }

    namespace
    {
        // a color target the fragment stage doesn't write must have an empty write mask, so the pipeline needs to know
        uint32_t parse_fragment_outputs(const string& wgsl, const char* entry_point)
        {
            const size_t function = wgsl.find(string("fn ") + entry_point + "(");
            if (function == string::npos)
                return 0;

            const size_t arrow = wgsl.find("->", function);
            const size_t body  = wgsl.find('{', function);
            if (arrow == string::npos || arrow > body)
                return 0;

            auto collect = [&wgsl](size_t begin, const size_t end)
            {
                uint32_t mask = 0;
                while ((begin = wgsl.find("@location(", begin)) != string::npos && begin < end)
                {
                    mask  |= 1u << strtoul(wgsl.c_str() + begin + 10, nullptr, 10);
                    begin += 10;
                }
                return mask;
            };

            size_t type_begin = wgsl.find_first_not_of(" \t", arrow + 2);
            if (wgsl.compare(type_begin, 10, "@location(") == 0)
                return collect(type_begin, body);

            const size_t type_end   = wgsl.find_first_of(" \t{", type_begin);
            const string type_name  = wgsl.substr(type_begin, type_end - type_begin);
            const size_t definition = wgsl.find("struct " + type_name + " {");
            if (definition == string::npos)
                return 0;

            return collect(definition, wgsl.find('}', definition));
        }
    }

    const char* webgpu::storage_format_name(const WGPUTextureFormat format)
    {
        for (const auto& [name, value] : storage_formats)
        {
            if (value == format)
                return name;
        }
        return nullptr;
    }

    WGPUShaderModule webgpu::create_module_variant(const Shader* shader, const vector<BindingOverride>& overrides, const char* name)
    {
        string wgsl     = shader->wgsl;
        bool patched    = false;
        size_t position = 0;
        while ((position = wgsl.find("@group(", position)) != string::npos)
        {
            const uint32_t group = static_cast<uint32_t>(strtoul(wgsl.c_str() + position + 7, nullptr, 10));
            const size_t b       = wgsl.find("@binding(", position);
            const size_t end     = wgsl.find(';', position);
            if (b == string::npos || end == string::npos)
                break;

            const uint32_t binding = static_cast<uint32_t>(strtoul(wgsl.c_str() + b + 9, nullptr, 10));
            const size_t storage   = wgsl.find("texture_storage_", position);
            position               = end;
            if (group != group_pass || storage == string::npos || storage > end)
                continue;

            for (const BindingOverride& entry : overrides)
            {
                if (entry.binding != binding || entry.storage_format == WGPUTextureFormat_Undefined)
                    continue;

                const size_t open  = wgsl.find('<', storage);
                const size_t comma = wgsl.find(',', open);
                if (open != string::npos && comma != string::npos && comma < end)
                {
                    const char* format = storage_format_name(entry.storage_format);
                    wgsl.replace(open + 1, comma - open - 1, format);
                    position = wgsl.find(';', open);
                    patched  = true;
                }
            }
        }

        if (!patched)
        {
            wgpuShaderModuleAddRef(shader->module);
            return shader->module;
        }
        return create_module(wgsl, name);
    }

    void webgpu::destroy_shader(void* resource)
    {
        Shader* shader = static_cast<Shader*>(resource);
        if (shader->module)
        {
            wgpuShaderModuleRelease(shader->module);
        }
        delete shader;
    }

    void* RHI_Shader::RHI_Compile()
    {
        if (m_shader_type != RHI_Shader_Type::Vertex && m_shader_type != RHI_Shader_Type::Pixel && m_shader_type != RHI_Shader_Type::Compute)
        {
            SP_LOG_WARNING("%s is a stage webgpu doesn't have", m_object_name.c_str());
            return nullptr;
        }

        const uint64_t key = compute_key(m_preprocessed_source, m_defines, m_shader_type, GetEntryPoint(), GetTargetProfile());
        string wgsl;
        vector<char> wgsl_bytes;
        vector<webgpu::Binding> bindings;
        vector<webgpu::VertexInput> vertex_inputs;
        vector<CachedDescriptor> cached_descriptors;

        if (generated_cache::Load(cache_path(key), key, wgsl_bytes, bindings, vertex_inputs, cached_descriptors))
        {
            wgsl.assign(wgsl_bytes.begin(), wgsl_bytes.end());
        }
        else
        {
#if defined(__EMSCRIPTEN__)
            SP_LOG_ERROR("%s isn't in the shader cache, the web build can't compile hlsl, run the desktop webgpu build once to fill it", m_object_name.c_str());
            return nullptr;
#else
            const string source = rewrite_source(m_preprocessed_source);
            vector<string> arguments;
            {
                arguments.emplace_back("-E");
                arguments.emplace_back(GetEntryPoint());
                arguments.emplace_back("-T");
                arguments.emplace_back(GetTargetProfile());
                arguments.emplace_back("-spirv");
                arguments.emplace_back("-fspv-target-env=vulkan1.1"); // tint reads spir-v up to 1.3
                arguments.emplace_back("-fvk-u-shift"); arguments.emplace_back(to_string(rhi_shader_register_shift_u)); arguments.emplace_back("all");
                arguments.emplace_back("-fvk-b-shift"); arguments.emplace_back(to_string(rhi_shader_register_shift_b)); arguments.emplace_back("all");
                arguments.emplace_back("-fvk-t-shift"); arguments.emplace_back(to_string(rhi_shader_register_shift_t)); arguments.emplace_back("all");
                arguments.emplace_back("-fvk-s-shift"); arguments.emplace_back(to_string(rhi_shader_register_shift_s)); arguments.emplace_back("all");
                arguments.emplace_back("-fvk-use-gl-layout"); // wgsl can't express hlsl's packing, the c++ structs align the same way under webgpu
                arguments.emplace_back("-fvk-use-dx-position-w");
                arguments.emplace_back("-fspv-flatten-resource-arrays");
                arguments.emplace_back("-flegacy-macro-expansion");
                arguments.emplace_back("-Wno-ignored-attributes");
                arguments.emplace_back("-Zpc");
                arguments.emplace_back("-D"); arguments.emplace_back("API_WEBGPU=1");
                // no view instancing, the view id becomes a vertex input that reads 0
                arguments.emplace_back("-D"); arguments.emplace_back("SV_ViewID=SP_VIEW_ID");
                if (m_shader_type == RHI_Shader_Type::Compute)
                {
                    arguments.emplace_back("-D"); arguments.emplace_back("SP_SHADER_STAGE_COMPUTE=1");
                }
                for (const auto& [define, value] : m_defines)
                {
                    arguments.emplace_back("-D"); arguments.emplace_back(define + "=" + value);
                }
            }

            IDxcBlob* blob = DirectXShaderCompiler::Compile(source, arguments, m_object_name);
            if (!blob)
                return nullptr;

            vector<uint32_t> words(static_cast<const uint32_t*>(blob->GetBufferPointer()), static_cast<const uint32_t*>(blob->GetBufferPointer()) + blob->GetBufferSize() / 4);
            blob->Release();

            m_descriptors.clear();
            Reflect(m_shader_type, words.data(), static_cast<uint32_t>(words.size()));

            vector<uint32_t> remapped = words;
            remap_bindings(remapped);
            if (m_shader_type == RHI_Shader_Type::Vertex)
            {
                make_buffers_read_only(remapped);
            }
            wgsl = run_tint(remapped, m_object_name);
            if (wgsl.empty())
                return nullptr;

            // the engine binds one buffer as an srv and a uav in the same dispatch, webgpu only allows that when both are
            // writable, compute can always declare read-only storage as read_write (vertex stages can't)
            if (m_shader_type == RHI_Shader_Type::Compute)
            {
                replace_all(wgsl, "var<storage, read>", "var<storage, read_write>");
            }
            else if (m_shader_type == RHI_Shader_Type::Vertex)
            {
                // the vertex stage can't write storage, the uavs it declares are only ever read
                replace_all(wgsl, "var<storage, read_write>", "var<storage, read>");
            }
            restrict_unread_storage_textures(wgsl);

            bindings = parse_bindings(wgsl, to_stage(m_shader_type));
            reflect_extra(remapped, GetEntryPoint(), m_shader_type, bindings, vertex_inputs);

            for (const RHI_Descriptor& descriptor : m_descriptors)
            {
                CachedDescriptor cached;
                snprintf(cached.name, sizeof(cached.name), "%s", descriptor.name.c_str());
                cached.type         = descriptor.type;
                cached.layout       = descriptor.layout;
                cached.slot         = descriptor.slot;
                cached.stage        = descriptor.stage;
                cached.struct_size  = descriptor.struct_size;
                cached.array_length = descriptor.array_length;
                cached.space        = descriptor.space;
                cached.as_array     = descriptor.as_array ? 1 : 0;
                cached.used         = descriptor.used ? 1 : 0;
                cached_descriptors.push_back(cached);
            }

            wgsl_bytes.assign(wgsl.begin(), wgsl.end());
            generated_cache::Save(cache_path(key), key, wgsl_bytes, bindings, vertex_inputs, cached_descriptors);
#endif
        }

        m_descriptors.clear();
        for (const CachedDescriptor& cached : cached_descriptors)
        {
            m_descriptors.emplace_back(string(cached.name), cached.type, cached.layout, cached.slot, cached.stage, cached.struct_size, cached.as_array != 0, cached.array_length, cached.space, cached.used != 0);
        }

        webgpu::Shader* shader = new webgpu::Shader();
        shader->module         = create_module(wgsl, m_object_name);
        shader->bindings       = move(bindings);
        shader->vertex_inputs  = move(vertex_inputs);
        shader->wgsl           = wgsl;
        m_object_size          = wgsl.size();
        if (m_shader_type == RHI_Shader_Type::Pixel)
        {
            shader->fragment_outputs = parse_fragment_outputs(wgsl, GetEntryPoint());
        }
        if (!shader->module)
        {
            SP_LOG_ERROR("Failed to create a shader module for %s", m_object_name.c_str());
            webgpu::destroy_shader(shader);
            return nullptr;
        }

        if (m_shader_type == RHI_Shader_Type::Vertex && m_input_layout)
        {
            m_input_layout->Create(m_vertex_type);
        }

        return shader;
    }

    void RHI_Shader::Reflect(const RHI_Shader_Type shader_type, const uint32_t* ptr, uint32_t size)
    {
#if !defined(__EMSCRIPTEN__)
        SP_ASSERT(ptr != nullptr);
        SP_ASSERT(size != 0);

        spirv_cross::CompilerHLSL compiler(ptr, size);
        if (const char* entry_point = GetEntryPoint())
        {
            for (const spirv_cross::EntryPoint& candidate : compiler.get_entry_points_and_stages())
            {
                if (candidate.name == entry_point)
                {
                    compiler.set_entry_point(candidate.name, candidate.execution_model);
                    break;
                }
            }
        }

        const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        spirv_resources_to_descriptors(compiler, m_descriptors, resources.separate_images, RHI_Descriptor_Type::Image,            shader_type);
        spirv_resources_to_descriptors(compiler, m_descriptors, resources.storage_images,  RHI_Descriptor_Type::TextureStorage,   shader_type);
        spirv_resources_to_descriptors(compiler, m_descriptors, resources.storage_buffers, RHI_Descriptor_Type::StructuredBuffer, shader_type);
        spirv_resources_to_descriptors(compiler, m_descriptors, resources.uniform_buffers, RHI_Descriptor_Type::ConstantBuffer,   shader_type);
#endif
    }

    void RHI_Shader::CompileFromSpirv(const RHI_Shader_Type type, const void* spirv_bytecode, uint64_t spirv_size, const string& name)
    {
        // only vulkan consumes spir-v directly
        m_compilation_state = RHI_ShaderCompilationState::Failed;
    }
}
