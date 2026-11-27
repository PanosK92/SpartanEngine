/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ============================
#include "pch.h"
#include "RHI_DirectXShaderCompiler.h"
#include "../file_system/FileSystem.h"
#include "../geometry/GeneratedCache.h"
#include <algorithm>
#include <sstream>
//=======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        // dxcompiler.dll has global mutable state across concurrent in-process spir-v compiles and
        // spirv-tools redundancy elimination multiplies memory across parallel g_buffer compiles.
        // keep per-thread interfaces (dxc requires thread affinity) while serializing spir-v calls;
        // dxil compiles, include preprocessing, cache hashing/io, module creation and reflection run in parallel.
        thread_local IDxcUtils* dxc_utils    = nullptr;
        thread_local IDxcCompiler3* compiler = nullptr;
        mutex dxc_mutex;
        once_flag version_initialization;
        string compiler_version;

        // forwards whatever dxc has to say to the log and returns the compile status
        bool log_diagnostics(IDxcResult* dxc_result)
        {
            IDxcBlobEncoding* error_buffer = nullptr;
            HRESULT result                 = dxc_result->GetErrorBuffer(&error_buffer);

            if (SUCCEEDED(result))
            {
                stringstream stream(string(static_cast<char*>(error_buffer->GetBufferPointer()), error_buffer->GetBufferSize()));
                string line;
                while (getline(stream, line, '\n'))
                {
                    if (line.find("error") != string::npos)
                    {
                        SP_LOG_ERROR(line.c_str());
                    }
                    else if (line.find("warning") != string::npos)
                    {
                        SP_LOG_WARNING(line.c_str());
                    }
                    else if (!FileSystem::IsEmptyOrWhitespace(line))
                    {
                        SP_LOG_INFO(line.c_str());
                    }
                }
            }
            else
            {
                SP_LOG_ERROR("Failed to get error buffer");
            }

            if (error_buffer)
            {
                error_buffer->Release();
            }

            dxc_result->GetStatus(&result);

            return result == S_OK;
        }
    }

    IDxcBlob* DirectXShaderCompiler::Compile(const string& source, vector<string>& arguments, const string& name, bool allow_fast_pass, bool* out_used_fast_pass)
    {
        if (out_used_fast_pass)
        {
            *out_used_fast_pass = false;
        }

        {
            lock_guard<mutex> lock(dxc_mutex);
            if (!compiler && FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))))
            {
                SP_LOG_ERROR("Failed to create DirectXShaderCompiler interface");
                return nullptr;
            }
            if (!dxc_utils && FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxc_utils))))
            {
                SP_LOG_ERROR("Failed to create DirectXShaderCompiler utils interface");
                return nullptr;
            }

            // the version is part of every cache key, so all workers must agree on it before their first lookup
            call_once(version_initialization, []
            {
                IDxcVersionInfo2* version = nullptr;
                if (SUCCEEDED(compiler->QueryInterface(IID_PPV_ARGS(&version))))
                {
                    UINT32 major = 0, minor = 0, flags = 0, commits = 0;
                    char* commit = nullptr;
                    if (SUCCEEDED(version->GetVersion(&major, &minor)) && SUCCEEDED(version->GetFlags(&flags)) && SUCCEEDED(version->GetCommitInfo(&commits, &commit)) && commit && *commit)
                    {
                        compiler_version = to_string(major) + ":" + to_string(minor) + ":" + to_string(flags) + ":" + to_string(commits) + ":" + commit;
                    }
                    CoTaskMemFree(commit);
                    version->Release();
                }
                IDxcVersionInfo3* custom_version = nullptr;
                if (SUCCEEDED(compiler->QueryInterface(IID_PPV_ARGS(&custom_version))))
                {
                    char* custom = nullptr;
                    if (SUCCEEDED(custom_version->GetCustomVersionString(&custom)) && custom && !compiler_version.empty())
                    {
                        compiler_version += string(":") + custom;
                    }
                    CoTaskMemFree(custom);
                    custom_version->Release();
                }
            });
        }

        // Source already contains every include. Arguments include backend, stage,
        // entry point, defines and optimization flags, so edits cannot reuse stale code.
        // Always check the optimized cache first so clean shaders never incur a two-pass compile.
        generated_cache::Hash hash;
        hash.Add(string("shader_bytecode_v1"));
#ifdef _WIN32
        hash.Add(string("windows"));
#else
        hash.Add(string("linux"));
#endif
        hash.Add(compiler_version);
        hash.Add(source);
        for (const string& argument : arguments) hash.Add(argument);
        const auto cache_path = compiler_version.empty() ? filesystem::path{} : generated_cache::Path(FileSystem::GetExecutableDirectory() + "/cache", "shaders", hash.value);
        vector<uint8_t> bytecode;
        if (generated_cache::Load(cache_path, hash.value, bytecode) && !bytecode.empty())
        {
            IDxcBlobEncoding* cached = nullptr;
            lock_guard<mutex> lock(dxc_mutex);
            if (SUCCEEDED(dxc_utils->CreateBlob(bytecode.data(), static_cast<uint32_t>(bytecode.size()), DXC_CP_ACP, &cached)))
            {
                return cached;
            }
        }

        // on a cache miss, optionally run a fast unoptimized pass (-Od) so the user can render
        // immediately while the fully optimized bytecode is compiled and cached in pass 2
        const bool already_unoptimized = any_of(arguments.begin(), arguments.end(), [](const string& arg)
        {
            return arg == "-Od" || arg == "-O0";
        });
        const bool use_fast_pass = allow_fast_pass && !already_unoptimized;

        vector<string> compile_arguments = arguments;
        if (use_fast_pass)
        {
            compile_arguments.emplace_back("-Od");
        }

        const bool is_spirv = any_of(compile_arguments.begin(), compile_arguments.end(), [](const string& arg)
        {
            return arg == "-spirv";
        });

        // dxc takes wide strings, the arguments are built as utf8 everywhere else
        vector<wstring> arguments_wide(compile_arguments.size());
        vector<LPCWSTR> arguments_pointers(compile_arguments.size());
        for (size_t i = 0; i < compile_arguments.size(); i++)
        {
            arguments_wide[i]     = FileSystem::StringToWstring(compile_arguments[i]);
            arguments_pointers[i] = arguments_wide[i].c_str();
        }

        string variant;
        for (size_t i = 0; i + 1 < compile_arguments.size(); ++i)
        {
            if (compile_arguments[i] == "-T" || compile_arguments[i] == "-E" || compile_arguments[i] == "-D")
            {
                variant += (variant.empty() ? "" : ", ") + compile_arguments[i + 1];
            }
        }

        IDxcBlob* shader      = nullptr;
        float compile_time_ms = 0.0f;
        {
            unique_lock<mutex> lock(dxc_mutex, defer_lock);
            if (is_spirv)
            {
                lock.lock();
            }

            IDxcBlobEncoding* blob_encoding = nullptr;
            if (FAILED(dxc_utils->CreateBlobFromPinned(source.c_str(), static_cast<uint32_t>(source.size()), CP_UTF8, &blob_encoding)))
            {
                SP_LOG_ERROR("Failed to create shader blob from source.");
                return nullptr;
            }

            DxcBuffer dxc_buffer = {};
            dxc_buffer.Ptr       = blob_encoding->GetBufferPointer();
            dxc_buffer.Size      = blob_encoding->GetBufferSize();
            dxc_buffer.Encoding  = DXC_CP_UTF8;

            const Stopwatch compile_timer;
            IDxcResult* dxc_result = nullptr;
            HRESULT hr             = compiler->Compile(
                &dxc_buffer,
                arguments_pointers.data(),
                static_cast<uint32_t>(arguments_pointers.size()),
                nullptr, // optionally pass a real include handler
                IID_PPV_ARGS(&dxc_result)
            );
            compile_time_ms = compile_timer.GetElapsedTimeMs();

            blob_encoding->Release();

            if (FAILED(hr) || !dxc_result || !log_diagnostics(dxc_result))
            {
                SP_LOG_ERROR("Shader '%s' (%s) compilation failed after %.2f ms", name.c_str(), variant.c_str(), compile_time_ms);
                if (dxc_result)
                {
                    dxc_result->Release();
                    dxc_result = nullptr;
                }
            }

            if (dxc_result)
            {
                dxc_result->GetResult(&shader);
                dxc_result->Release();
            }
        }

        if (!shader)
        {
            return nullptr;
        }

        if (out_used_fast_pass)
        {
            *out_used_fast_pass = use_fast_pass;
        }

        // progress is on screen, only the outliers worth optimizing are logged
        if (compile_time_ms >= 1000.0f)
        {
            SP_LOG_INFO("Compiled shader '%s' (%s%s) in %.2f ms", name.c_str(), variant.c_str(), use_fast_pass ? ", fast pass" : "", compile_time_ms);
        }

        // only persist fully optimized bytecode to disk so the cache always holds the fast runtime version
        if (!use_fast_pass && !cache_path.empty())
        {
            const auto* bytes = static_cast<const uint8_t*>(shader->GetBufferPointer());
            bytecode.assign(bytes, bytes + shader->GetBufferSize());
            generated_cache::Save(cache_path, hash.value, bytecode);
        }
        return shader;
    }
}
