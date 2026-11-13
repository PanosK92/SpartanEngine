// in-process 7z extract via statically linked format7zr
// the handler streams each file to disk as it decodes, so memory stays near the archive's dictionary size
// the ansi-c decoder (SzArEx_Extract) is not used, it holds a whole solid block in memory before writing anything,
// which for a multi-gigabyte solid archive is more memory than many machines can give
#include "StdAfx.h"
#include "archive_7z.h"

#include <stdio.h>
#include <string>

#include "../CPP/Common/MyWindows.h"
#include "../CPP/Common/Defs.h"
#include "../CPP/Common/StringConvert.h"
#include "../CPP/Common/UTFConvert.h"

#include "../CPP/Windows/ErrorMsg.h"
#include "../CPP/Windows/FileDir.h"
#include "../CPP/Windows/FileIO.h"
#include "../CPP/Windows/FileName.h"
#include "../CPP/Windows/PropVariant.h"

#include "../CPP/7zip/Common/FileStreams.h"
#include "../CPP/7zip/Archive/IArchive.h"

STDAPI CreateObject(const GUID* clsid, const GUID* iid, void** outObject);

extern "C" const GUID CLSID_Format;

using namespace NWindows;
using namespace NFile;
using namespace NDir;

namespace
{
    // what the decoder may hold while extracting, on top of this it needs the archive's dictionary
    const UInt64 k_decode_memory_limit = (UInt64)1 << 30;

    static FString utf8_to_fstring(const char* utf8)
    {
        FString path = us2fs(GetUnicodeString(utf8, CP_UTF8));
#ifdef _WIN32
        // the engine writes paths with forward slashes, the long path form windows needs past 260 chars only takes backslashes
        path.Replace(FTEXT('/'), FCHAR_PATH_SEPARATOR);
#endif
        return path;
    }

    static std::string to_utf8(const UString& text)
    {
        AString utf8;
        ConvertUnicodeToUTF8(text, utf8);
        return std::string(utf8.Ptr());
    }

    // the system's own wording plus the code, the wording is localized and the code is what can be searched for
    static std::string hresult_text(HRESULT result)
    {
        char code[16];
        snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(result));
        return to_utf8(NError::MyFormatMessage(result)) + " (" + code + ")";
    }

    static const char* operation_result_text(Int32 operation_result)
    {
        switch (operation_result)
        {
        case NArchive::NExtract::NOperationResult::kUnsupportedMethod: return "unsupported compression method";
        case NArchive::NExtract::NOperationResult::kDataError:         return "corrupt data";
        case NArchive::NExtract::NOperationResult::kCRCError:          return "checksum mismatch";
        case NArchive::NExtract::NOperationResult::kUnavailable:       return "unavailable data";
        case NArchive::NExtract::NOperationResult::kUnexpectedEnd:     return "unexpected end of the archive";
        case NArchive::NExtract::NOperationResult::kDataAfterEnd:      return "data after the end of the archive";
        case NArchive::NExtract::NOperationResult::kIsNotArc:          return "not an archive";
        case NArchive::NExtract::NOperationResult::kHeadersError:      return "damaged headers";
        default:                                                       return "decoding error";
        }
    }

    class CArchiveExtractCallback Z7_final:
        public IArchiveExtractCallback,
        public CMyUnknownImp
    {
        Z7_IFACES_IMP_UNK_1(IArchiveExtractCallback)
        Z7_IFACE_COM7_IMP(IProgress)

    public:
        IInArchive* Archive;
        FString DirPrefix;
        UString ItemPath;  // name inside the archive of the item being extracted
        std::string Error; // the first failure, empty while everything worked
        COutFileStream* OutFileStreamSpec;
        CMyComPtr<ISequentialOutStream> OutFileStream;

        CArchiveExtractCallback() :
            Archive(nullptr),
            OutFileStreamSpec(nullptr)
        {
        }

        // call right after the failed file system call, before anything else can replace the last error
        HRESULT FailWithLastError(const char* what, const FString& path)
        {
            const HRESULT result = GetLastError_noZero_HRESULT();
            if (Error.empty())
            {
                Error = std::string(what) + " " + to_utf8(fs2us(path)) + ": " + hresult_text(result);
            }
            return E_ABORT;
        }
    };

    Z7_COM7F_IMF(CArchiveExtractCallback::SetTotal(UInt64 /* size */))
    {
        return S_OK;
    }

    Z7_COM7F_IMF(CArchiveExtractCallback::SetCompleted(const UInt64* /* completeValue */))
    {
        return S_OK;
    }

    Z7_COM7F_IMF(CArchiveExtractCallback::GetStream(UInt32 index, ISequentialOutStream** outStream, Int32 askExtractMode))
    {
        *outStream = nullptr;
        OutFileStream.Release();
        OutFileStreamSpec = nullptr;

        NCOM::CPropVariant path_prop;
        RINOK(Archive->GetProperty(index, kpidPath, &path_prop))
        if (path_prop.vt != VT_BSTR)
        {
            if (Error.empty())
            {
                Error = "an archive entry has no name";
            }
            return E_ABORT;
        }
        ItemPath = path_prop.bstrVal;

        if (askExtractMode != NArchive::NExtract::NAskMode::kExtract)
        {
            return S_OK;
        }

        NCOM::CPropVariant dir_prop;
        RINOK(Archive->GetProperty(index, kpidIsDir, &dir_prop))
        const bool is_dir = dir_prop.vt == VT_BOOL && dir_prop.boolVal != VARIANT_FALSE;

        const FString full_path = DirPrefix + us2fs(ItemPath);
        if (is_dir)
        {
            if (!CreateComplexDir(full_path))
            {
                return FailWithLastError("could not create the folder", full_path);
            }
            return S_OK;
        }

        const int slash_pos = ItemPath.ReverseFind_PathSepar();
        if (slash_pos >= 0)
        {
            const FString parent = DirPrefix + us2fs(ItemPath.Left(static_cast<unsigned>(slash_pos)));
            if (!CreateComplexDir(parent))
            {
                return FailWithLastError("could not create the folder", parent);
            }
        }

        OutFileStreamSpec = new COutFileStream;
        CMyComPtr<ISequentialOutStream> out_stream(OutFileStreamSpec);
        if (!OutFileStreamSpec->Create_ALWAYS(full_path))
        {
            OutFileStreamSpec = nullptr;
            return FailWithLastError("could not create the file", full_path);
        }
        OutFileStream = out_stream;
        *outStream = out_stream.Detach();
        return S_OK;
    }

    Z7_COM7F_IMF(CArchiveExtractCallback::PrepareOperation(Int32 /* askExtractMode */))
    {
        return S_OK;
    }

    Z7_COM7F_IMF(CArchiveExtractCallback::SetOperationResult(Int32 operationResult))
    {
        HRESULT close_result = S_OK;
        if (OutFileStream)
        {
            close_result = OutFileStreamSpec->Close();
        }
        OutFileStream.Release();
        OutFileStreamSpec = nullptr;

        // an install must not continue past a file that did not come out right
        if (operationResult != NArchive::NExtract::NOperationResult::kOK)
        {
            if (Error.empty())
            {
                Error = std::string(operation_result_text(operationResult)) + " in " + to_utf8(ItemPath);
            }
            return E_ABORT;
        }

        if (close_result != S_OK)
        {
            if (Error.empty())
            {
                Error = "could not finish writing " + to_utf8(ItemPath) + ": " + hresult_text(close_result);
            }
            return E_ABORT;
        }

        return S_OK;
    }

    static bool extract(const char* archive_path, const char* destination_path, std::string& error)
    {
        if (!archive_path || !archive_path[0] || !destination_path || !destination_path[0])
        {
            error = "no archive or no destination was given";
            return false;
        }

        const FString archive_name = utf8_to_fstring(archive_path);
        CInFileStream* file_spec = new CInFileStream;
        CMyComPtr<IInStream> file(file_spec);
        if (!file_spec->Open(archive_name))
        {
            error = "could not open the archive: " + hresult_text(GetLastError_noZero_HRESULT());
            return false;
        }

        CMyComPtr<IInArchive> archive;
        if (CreateObject(&CLSID_Format, &IID_IInArchive, (void**)&archive) != S_OK)
        {
            error = "the 7z reader could not be created";
            return false;
        }

        // left alone, the lzma2 decoder buffers a block per cpu thread and allows itself about half of the system's memory,
        // the cap only costs decoding threads and keeps the extraction small on any machine
        {
            CMyComPtr<ISetProperties> set_properties;
            archive->QueryInterface(IID_ISetProperties, (void**)&set_properties);
            const wchar_t* names[]       = { L"memuse" };
            NCOM::CPropVariant values[1] = { k_decode_memory_limit };
            if (!set_properties || set_properties->SetProperties(names, values, 1) != S_OK)
            {
                error = "the 7z reader did not accept its memory limit";
                return false;
            }
        }

        const UInt64 scan_size = 1 << 23;
        const HRESULT open_result = archive->Open(file, &scan_size, nullptr);
        if (open_result != S_OK)
        {
            error = open_result == S_FALSE ? "the file is not a 7z archive or its headers are damaged" : "could not read the archive: " + hresult_text(open_result);
            return false;
        }

        const FString destination = utf8_to_fstring(destination_path);
        if (!CreateComplexDir(destination))
        {
            error = "could not create the folder " + std::string(destination_path) + ": " + hresult_text(GetLastError_noZero_HRESULT());
            archive->Close();
            return false;
        }

        CArchiveExtractCallback* callback_spec = new CArchiveExtractCallback;
        CMyComPtr<IArchiveExtractCallback> callback(callback_spec);
        callback_spec->Archive   = archive;
        callback_spec->DirPrefix = destination;
        NName::NormalizeDirPathPrefix(callback_spec->DirPrefix);

        const HRESULT result = archive->Extract(nullptr, (UInt32)(Int32)-1, false, callback);
        archive->Close();

        if (!callback_spec->Error.empty())
        {
            error = callback_spec->Error;
            return false;
        }

        if (result != S_OK)
        {
            // a write that failed half way (a full disk for instance) ends the extraction with the write's own error
            error = result == S_FALSE ? std::string("corrupt data") : hresult_text(result);
            if (!callback_spec->ItemPath.IsEmpty())
            {
                error += ", while extracting " + to_utf8(callback_spec->ItemPath);
            }
            return false;
        }

        return true;
    }
}

int archive_7z_extract(const char* archive_path, const char* destination_path, char* error_out, int error_capacity)
{
    std::string error;
    bool success = false;
    try
    {
        success = extract(archive_path, destination_path, error);
    }
    catch (...)
    {
        // the sdk reports a failed allocation by throwing
        success = false;
        error   = "out of memory";
    }

    if (error_out && error_capacity > 0)
    {
        snprintf(error_out, static_cast<size_t>(error_capacity), "%s", success ? "" : error.c_str());
    }

    return success ? 0 : 1;
}
