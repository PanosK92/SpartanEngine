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
#include <sstream>
//=======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        // created on first compile and kept for the lifetime of the process
        IDxcUtils* dxc_utils        = nullptr;
        IDxcCompiler3* compiler = nullptr;
        once_flag initialization;
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

    IDxcBlob* DirectXShaderCompiler::Compile(const string& source, vector<string>& arguments)
    {
        call_once(initialization, []
        {
            if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))) ||
                FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxc_utils))))
            {
                SP_LOG_ERROR("Failed to create DirectXShaderCompiler interfaces");
                return;
            }

            IDxcVersionInfo2* version = nullptr;
            if (SUCCEEDED(compiler->QueryInterface(IID_PPV_ARGS(&version))))
            {
                UINT32 major = 0, minor = 0, flags = 0, commits = 0;
                char* commit = nullptr;
                if (SUCCEEDED(version->GetVersion(&major, &minor)) &&
                    SUCCEEDED(version->GetFlags(&flags)) &&
                    SUCCEEDED(version->GetCommitInfo(&commits, &commit)) && commit && *commit)
                {
                    compiler_version = to_string(major) + ":" + to_string(minor) + ":" +
                        to_string(flags) + ":" + to_string(commits) + ":" + commit;
                }
                CoTaskMemFree(commit);
                version->Release();
            }
            IDxcVersionInfo3* custom_version = nullptr;
            if (SUCCEEDED(compiler->QueryInterface(IID_PPV_ARGS(&custom_version))))
            {
                char* custom = nullptr;
                if (SUCCEEDED(custom_version->GetCustomVersionString(&custom)) && custom && !compiler_version.empty())
                    compiler_version += string(":") + custom;
                CoTaskMemFree(custom);
                custom_version->Release();
            }
        });
        if (!compiler || !dxc_utils) return nullptr;

        // Source already contains every include. Arguments include backend, stage,
        // entry point, defines and optimization flags, so edits cannot reuse stale code.
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
        const auto cache_path = compiler_version.empty() ? filesystem::path{} :
            generated_cache::Path(FileSystem::GetExecutableDirectory(), "shaders", hash.value);
        vector<uint8_t> bytecode;
        if (generated_cache::Load(cache_path, hash.value, bytecode) && !bytecode.empty())
        {
            IDxcBlobEncoding* cached = nullptr;
            if (SUCCEEDED(dxc_utils->CreateBlob(bytecode.data(), static_cast<uint32_t>(bytecode.size()), DXC_CP_ACP, &cached)))
                return cached;
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

        // dxc takes wide strings, the arguments are built as utf8 everywhere else
        vector<wstring> arguments_wide(arguments.size());
        vector<LPCWSTR> arguments_pointers(arguments.size());
        for (size_t i = 0; i < arguments.size(); i++)
        {
            arguments_wide[i]     = FileSystem::StringToWstring(arguments[i]);
            arguments_pointers[i] = arguments_wide[i].c_str();
        }

        IDxcResult* dxc_result = nullptr;
        HRESULT hr             = compiler->Compile(
            &dxc_buffer,
            arguments_pointers.data(),
            static_cast<uint32_t>(arguments_pointers.size()),
            nullptr, // optionally pass a real include handler
            IID_PPV_ARGS(&dxc_result)
        );

        blob_encoding->Release();

        if (FAILED(hr) || !dxc_result || !log_diagnostics(dxc_result))
        {
            SP_LOG_ERROR("Shader compilation failed.");
            if (dxc_result)
            {
                dxc_result->Release();
                dxc_result = nullptr;
            }
        }

        if (!dxc_result) return nullptr;
        IDxcBlob* shader = nullptr;
        dxc_result->GetResult(&shader);
        dxc_result->Release();
        if (shader && !cache_path.empty())
        {
            const auto* bytes = static_cast<const uint8_t*>(shader->GetBufferPointer());
            bytecode.assign(bytes, bytes + shader->GetBufferSize());
            generated_cache::Save(cache_path, hash.value, bytecode);
        }
        return shader;
    }
}
