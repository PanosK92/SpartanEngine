// in-process 7z create via statically linked format7zr
#include "StdAfx.h"
#include "archive_7z.h"

#include <stdio.h>

#include "../CPP/Common/MyWindows.h"
#include "../CPP/Common/Defs.h"
#include "../CPP/Common/IntToString.h"
#include "../CPP/Common/StringConvert.h"

#include "../CPP/Windows/FileDir.h"
#include "../CPP/Windows/FileFind.h"
#include "../CPP/Windows/FileName.h"
#include "../CPP/Windows/PropVariant.h"
#include "../CPP/Windows/PropVariantConv.h"

#include "../CPP/7zip/Common/FileStreams.h"
#include "../CPP/7zip/Archive/IArchive.h"
#include "../CPP/7zip/IPassword.h"

STDAPI CreateObject(const GUID* clsid, const GUID* iid, void** outObject);

extern "C" const GUID CLSID_Format;

using namespace NWindows;
using namespace NFile;
using namespace NDir;

namespace
{
    static void print_error(const char* message)
    {
        fputs("Error: ", stdout);
        fputs(message, stdout);
        fputs("\n", stdout);
    }

    struct CDirItem: public NWindows::NFile::NFind::CFileInfoBase
    {
        UString Path_For_Handler;
        FString FullPath;

        CDirItem(const NWindows::NFile::NFind::CFileInfo& fi) :
            CFileInfoBase(fi)
        {
        }
    };

    class CArchiveUpdateCallback Z7_final:
        public IArchiveUpdateCallback2,
        public ICryptoGetTextPassword2,
        public CMyUnknownImp
    {
        Z7_IFACES_IMP_UNK_2(IArchiveUpdateCallback2, ICryptoGetTextPassword2)
        Z7_IFACE_COM7_IMP(IProgress)
        Z7_IFACE_COM7_IMP(IArchiveUpdateCallback)

    public:
        CRecordVector<UInt64> VolumesSizes;
        UString VolName;
        UString VolExt;
        FString DirPrefix;
        const CObjectVector<CDirItem>* DirItems;
        bool PasswordIsDefined;
        UString Password;
        bool AskPassword;
        bool m_NeedBeClosed;
        FStringVector FailedFiles;
        CRecordVector<HRESULT> FailedCodes;

        CArchiveUpdateCallback() :
            DirItems(nullptr),
            PasswordIsDefined(false),
            AskPassword(false),
            m_NeedBeClosed(false)
        {
        }

        ~CArchiveUpdateCallback()
        {
            Finilize();
        }

        HRESULT Finilize()
        {
            m_NeedBeClosed = false;
            return S_OK;
        }

        void Init(const CObjectVector<CDirItem>* dirItems)
        {
            DirItems = dirItems;
            m_NeedBeClosed = false;
            FailedFiles.Clear();
            FailedCodes.Clear();
        }
    };

    Z7_COM7F_IMF(CArchiveUpdateCallback::SetTotal(UInt64 /* size */))
    {
        return S_OK;
    }

    Z7_COM7F_IMF(CArchiveUpdateCallback::SetCompleted(const UInt64* /* completeValue */))
    {
        return S_OK;
    }

    Z7_COM7F_IMF(CArchiveUpdateCallback::GetUpdateItemInfo(
        UInt32 /* index */, Int32* newData, Int32* newProperties, UInt32* indexInArchive))
    {
        if (newData)
        {
            *newData = BoolToInt(true);
        }
        if (newProperties)
        {
            *newProperties = BoolToInt(true);
        }
        if (indexInArchive)
        {
            *indexInArchive = (UInt32)(Int32)-1;
        }
        return S_OK;
    }

    Z7_COM7F_IMF(CArchiveUpdateCallback::GetProperty(UInt32 index, PROPID propID, PROPVARIANT* value))
    {
        NCOM::CPropVariant prop;
        if (propID == kpidIsAnti)
        {
            prop = false;
            prop.Detach(value);
            return S_OK;
        }

        const CDirItem& di = (*DirItems)[index];
        switch (propID)
        {
        case kpidPath: prop = di.Path_For_Handler; break;
        case kpidIsDir: prop = di.IsDir(); break;
        case kpidSize: prop = di.Size; break;
        case kpidCTime: PropVariant_SetFrom_FiTime(prop, di.CTime); break;
        case kpidATime: PropVariant_SetFrom_FiTime(prop, di.ATime); break;
        case kpidMTime: PropVariant_SetFrom_FiTime(prop, di.MTime); break;
        case kpidAttrib: prop = (UInt32)di.GetWinAttrib(); break;
        case kpidPosixAttrib: prop = (UInt32)di.GetPosixAttrib(); break;
        }
        prop.Detach(value);
        return S_OK;
    }

    Z7_COM7F_IMF(CArchiveUpdateCallback::GetStream(UInt32 index, ISequentialInStream** inStream))
    {
        RINOK(Finilize())
        const CDirItem& dirItem = (*DirItems)[index];
        if (dirItem.IsDir())
        {
            return S_OK;
        }

        CInFileStream* inStreamSpec = new CInFileStream;
        CMyComPtr<ISequentialInStream> inStreamLoc(inStreamSpec);
        const FString path = DirPrefix + dirItem.FullPath;
        if (!inStreamSpec->Open(path))
        {
#ifdef _WIN32
            FailedCodes.Add(HRESULT_FROM_WIN32(::GetLastError()));
#else
            FailedCodes.Add(E_FAIL);
#endif
            FailedFiles.Add(path);
            return S_FALSE;
        }
        *inStream = inStreamLoc.Detach();
        return S_OK;
    }

    Z7_COM7F_IMF(CArchiveUpdateCallback::SetOperationResult(Int32 /* operationResult */))
    {
        m_NeedBeClosed = true;
        return S_OK;
    }

    Z7_COM7F_IMF(CArchiveUpdateCallback::GetVolumeSize(UInt32 index, UInt64* size))
    {
        if (VolumesSizes.Size() == 0)
        {
            return S_FALSE;
        }
        if (index >= (UInt32)VolumesSizes.Size())
        {
            index = VolumesSizes.Size() - 1;
        }
        *size = VolumesSizes[index];
        return S_OK;
    }

    Z7_COM7F_IMF(CArchiveUpdateCallback::GetVolumeStream(UInt32 /* index */, ISequentialOutStream** /* volumeStream */))
    {
        return E_NOTIMPL;
    }

    Z7_COM7F_IMF(CArchiveUpdateCallback::CryptoGetTextPassword2(Int32* passwordIsDefined, BSTR* password))
    {
        *passwordIsDefined = BoolToInt(PasswordIsDefined);
        return StringToBstr(Password, password);
    }

    static FString utf8_to_fstring(const char* utf8)
    {
        return us2fs(GetUnicodeString(utf8, CP_UTF8));
    }

    static void add_file_item(
        const FString& full_path,
        const UString& archive_path,
        CObjectVector<CDirItem>& dir_items)
    {
        NFind::CFileInfo fi;
        if (!fi.Find(full_path))
        {
            return;
        }
        CDirItem di(fi);
        di.Path_For_Handler = archive_path;
        di.FullPath = full_path;
        dir_items.Add(di);
    }

    static void add_path_recursive(
        const FString& full_path,
        const UString& archive_path,
        CObjectVector<CDirItem>& dir_items)
    {
        NFind::CFileInfo fi;
        if (!fi.Find(full_path))
        {
            return;
        }

        if (!fi.IsDir())
        {
            add_file_item(full_path, archive_path, dir_items);
            return;
        }

        {
            CDirItem di(fi);
            di.Path_For_Handler = archive_path;
            di.FullPath = full_path;
            dir_items.Add(di);
        }

        FString dir_prefix = full_path;
        dir_prefix.Add_PathSepar();
        NFind::CEnumerator enumerator;
        enumerator.SetDirPrefix(dir_prefix);
        NFind::CFileInfo child;
        while (enumerator.Next(child))
        {
            const FString child_full = dir_prefix + child.Name;
            UString child_arc = archive_path;
            child_arc.Add_PathSepar();
            child_arc += fs2us(child.Name);
            if (child.IsDir())
            {
                add_path_recursive(child_full, child_arc, dir_items);
            }
            else
            {
                add_file_item(child_full, child_arc, dir_items);
            }
        }
    }
}

int archive_7z_create(const char* archive_path, const char* const* paths, int path_count)
{
    if (!archive_path || !paths || path_count <= 0)
    {
        return 1;
    }

#ifdef _WIN32
    extern HINSTANCE g_hInstance;
    if (!g_hInstance)
    {
        g_hInstance = GetModuleHandleW(nullptr);
    }
#endif

    CObjectVector<CDirItem> dir_items;
    for (int i = 0; i < path_count; i++)
    {
        if (!paths[i] || !paths[i][0])
        {
            continue;
        }
        const FString full = utf8_to_fstring(paths[i]);
        NFind::CFileInfo fi;
        if (!fi.Find(full))
        {
            print_error("can't find file for archive");
            return 1;
        }
        add_path_recursive(full, fs2us(full), dir_items);
    }

    if (dir_items.Size() == 0)
    {
        print_error("no files to archive");
        return 1;
    }

    const FString archive_name = utf8_to_fstring(archive_path);
    DeleteFileAlways(archive_name);

    COutFileStream* out_file_stream_spec = new COutFileStream;
    CMyComPtr<IOutStream> out_file_stream = out_file_stream_spec;
    if (!out_file_stream_spec->Create_NEW(archive_name))
    {
        print_error("can't create archive file");
        return 1;
    }

    CMyComPtr<IOutArchive> out_archive;
    if (CreateObject(&CLSID_Format, &IID_IOutArchive, (void**)&out_archive) != S_OK)
    {
        print_error("cannot create 7z out archive");
        return 1;
    }

    CArchiveUpdateCallback* update_callback_spec = new CArchiveUpdateCallback;
    CMyComPtr<IArchiveUpdateCallback2> update_callback(update_callback_spec);
    update_callback_spec->Init(&dir_items);

    const HRESULT result = out_archive->UpdateItems(
        out_file_stream, dir_items.Size(), update_callback);
    update_callback_spec->Finilize();

    if (result != S_OK || update_callback_spec->FailedFiles.Size() != 0)
    {
        print_error("archive update failed");
        return 1;
    }

    return 0;
}
