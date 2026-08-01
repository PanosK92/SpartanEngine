// in-process 7z extract via lzma sdk ansi-c decoder
#include "archive_7z.h"

#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

extern "C" {
#include "../C/7z.h"
#include "../C/7zAlloc.h"
#include "../C/7zBuf.h"
#include "../C/7zCrc.h"
#include "../C/7zFile.h"
#include "../C/CpuArch.h"
}

namespace
{
    const size_t k_input_buf_size = (size_t)1 << 18;
    const ISzAlloc g_alloc = { SzAlloc, SzFree };

#ifdef _WIN32
    std::wstring utf8_to_wide(const char* utf8)
    {
        if (!utf8 || !utf8[0])
        {
            return std::wstring();
        }
        const int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        if (len <= 0)
        {
            return std::wstring();
        }
        std::wstring out(static_cast<size_t>(len - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), len);
        return out;
    }

    bool create_dir_wide(const std::wstring& path)
    {
        if (path.empty())
        {
            return false;
        }
        if (CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS)
        {
            return true;
        }
        // create parents
        for (size_t i = 0; i < path.size(); i++)
        {
            if (path[i] == L'\\' || path[i] == L'/')
            {
                const std::wstring parent = path.substr(0, i);
                if (!parent.empty() && parent.back() != L':')
                {
                    CreateDirectoryW(parent.c_str(), nullptr);
                }
            }
        }
        return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
    }
#else
    bool create_dir_utf8(const std::string& path)
    {
        if (path.empty())
        {
            return false;
        }
        std::string cur;
        for (size_t i = 0; i < path.size(); i++)
        {
            cur.push_back(path[i]);
            if (path[i] == '/' || i + 1 == path.size())
            {
                if (cur.empty() || cur == "/")
                {
                    continue;
                }
                mkdir(cur.c_str(), 0755);
            }
        }
        return true;
    }
#endif

    std::string utf16_to_utf8(const UInt16* s)
    {
        std::string out;
        for (size_t i = 0; s[i] != 0; )
        {
            UInt32 c = s[i++];
            if (c >= 0xD800 && c < 0xDC00 && s[i] != 0)
            {
                const UInt32 c2 = s[i++];
                if (c2 >= 0xDC00 && c2 < 0xE000)
                {
                    c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
                }
            }
            if (c < 0x80)
            {
                out.push_back(static_cast<char>(c));
            }
            else if (c < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
            else if (c < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (c >> 12)));
                out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (c >> 18)));
                out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
        return out;
    }

    void normalize_separators(std::string& path)
    {
        for (char& c : path)
        {
            if (c == '\\')
            {
                c = '/';
            }
        }
    }
}

int archive_7z_extract(const char* archive_path, const char* destination_path)
{
    if (!archive_path || !destination_path)
    {
        return 1;
    }

    CFileInStream archive_stream;
    CLookToRead2 look_stream;
    CSzArEx db;
    SRes res = SZ_OK;

    FileInStream_CreateVTable(&archive_stream);
    archive_stream.wres = 0;
    LookToRead2_CreateVTable(&look_stream, False);
    look_stream.buf = nullptr;

#ifdef _WIN32
    {
        const std::wstring wide = utf8_to_wide(archive_path);
        if (wide.empty() || InFile_OpenW(&archive_stream.file, wide.c_str()) != 0)
        {
            return 1;
        }
    }
    create_dir_wide(utf8_to_wide(destination_path));
#else
    if (InFile_Open(&archive_stream.file, archive_path) != 0)
    {
        return 1;
    }
    create_dir_utf8(destination_path);
#endif

    look_stream.buf = static_cast<Byte*>(ISzAlloc_Alloc(&g_alloc, k_input_buf_size));
    if (!look_stream.buf)
    {
        File_Close(&archive_stream.file);
        return 1;
    }
    look_stream.bufSize = k_input_buf_size;
    look_stream.realStream = &archive_stream.vt;
    LookToRead2_INIT(&look_stream)

    CrcGenerateTable();
    SzArEx_Init(&db);
    res = SzArEx_Open(&db, &look_stream.vt, &g_alloc, &g_alloc);

    UInt16* temp = nullptr;
    size_t temp_size = 0;
    UInt32 block_index = 0xFFFFFFFF;
    Byte* out_buffer = nullptr;
    size_t out_buffer_size = 0;

    if (res == SZ_OK)
    {
        for (UInt32 i = 0; i < db.NumFiles; i++)
        {
            size_t offset = 0;
            size_t out_size_processed = 0;
            const BoolInt is_dir = SzArEx_IsDir(&db, i);
            size_t len = SzArEx_GetFileNameUtf16(&db, i, nullptr);
            if (len > temp_size)
            {
                SzFree(nullptr, temp);
                temp_size = len;
                temp = static_cast<UInt16*>(SzAlloc(nullptr, temp_size * sizeof(UInt16)));
                if (!temp)
                {
                    res = SZ_ERROR_MEM;
                    break;
                }
            }

            SzArEx_GetFileNameUtf16(&db, i, temp);
            std::string relative = utf16_to_utf8(temp);
            normalize_separators(relative);

            std::string out_path = destination_path;
            if (!out_path.empty() && out_path.back() != '/' && out_path.back() != '\\')
            {
                out_path.push_back('/');
            }
            out_path += relative;

            if (is_dir)
            {
#ifdef _WIN32
                create_dir_wide(utf8_to_wide(out_path.c_str()));
#else
                create_dir_utf8(out_path);
#endif
                continue;
            }

            // ensure parent dirs exist
            {
                const size_t slash = out_path.find_last_of("/\\");
                if (slash != std::string::npos)
                {
                    const std::string parent = out_path.substr(0, slash);
#ifdef _WIN32
                    create_dir_wide(utf8_to_wide(parent.c_str()));
#else
                    create_dir_utf8(parent);
#endif
                }
            }

            res = SzArEx_Extract(
                &db, &look_stream.vt, i,
                &block_index, &out_buffer, &out_buffer_size,
                &offset, &out_size_processed,
                &g_alloc, &g_alloc);
            if (res != SZ_OK)
            {
                break;
            }

            CSzFile out_file;
#ifdef _WIN32
            {
                const std::wstring wide = utf8_to_wide(out_path.c_str());
                if (wide.empty() || OutFile_OpenW(&out_file, wide.c_str()) != 0)
                {
                    res = SZ_ERROR_FAIL;
                    break;
                }
            }
#else
            if (OutFile_Open(&out_file, out_path.c_str()) != 0)
            {
                res = SZ_ERROR_FAIL;
                break;
            }
#endif
            size_t processed = out_size_processed;
            const WRes wres = File_Write(&out_file, out_buffer + offset, &processed);
            File_Close(&out_file);
            if (wres != 0 || processed != out_size_processed)
            {
                res = SZ_ERROR_FAIL;
                break;
            }
        }
    }

    ISzAlloc_Free(&g_alloc, out_buffer);
    SzFree(nullptr, temp);
    SzArEx_Free(&db, &g_alloc);
    ISzAlloc_Free(&g_alloc, look_stream.buf);
    File_Close(&archive_stream.file);

    return res == SZ_OK ? 0 : 1;
}
