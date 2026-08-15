/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ============================
#include "pch.h"
#include "RHI_DirectXShaderCompiler.h"
#include "../file_system/FileSystem.h"
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

    IDxcResult* DirectXShaderCompiler::Compile(const string& source, vector<string>& arguments)
    {
        if (!compiler || !dxc_utils)
        {
            if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))) ||
                FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxc_utils))))
            {
                SP_LOG_ERROR("Failed to create DirectXShaderCompiler interfaces");
                return nullptr;
            }
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

        if (FAILED(hr) || !log_diagnostics(dxc_result))
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
}
