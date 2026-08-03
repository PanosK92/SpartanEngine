-- lzma sdk sources: in-process 7z extract + create (compiled into spartan)
local LZMA_ROOT = path.getabsolute(path.join(_MAIN_SCRIPT_DIR or _SCRIPT_DIR, "../third_party/lzma_sdk"))

local function lzma_sources()
    local files_list = {}
    local function add(rel)
        table.insert(files_list, path.join(LZMA_ROOT, rel))
    end

    local common = {
        "CRC", "CrcReg", "IntToString", "LzFindPrepare", "NewHandler",
        "MyString", "StringConvert", "StringToInt", "MyVector", "Wildcard"
    }
    for _, f in ipairs(common) do
        add("CPP/Common/" .. f .. ".cpp")
    end

    local win = {
        "FileDir", "FileFind", "FileIO", "FileName", "PropVariant",
        "PropVariantConv", "Synchronization", "System", "TimeUtils", "ErrorMsg"
    }
    for _, f in ipairs(win) do
        add("CPP/Windows/" .. f .. ".cpp")
    end

    local zcommon = {
        "CreateCoder", "CWrappers", "InBuffer", "InOutTempBuffer", "FilterCoder",
        "LimitedStreams", "MethodId", "MethodProps", "OutBuffer", "ProgressUtils",
        "PropId", "StreamBinder", "StreamObjects", "StreamUtils", "UniqBlocks",
        "VirtThread", "FileStreams"
    }
    for _, f in ipairs(zcommon) do
        add("CPP/7zip/Common/" .. f .. ".cpp")
    end

    add("CPP/7zip/Archive/ArchiveExports.cpp")

    local arcommon = {
        "CoderMixer2", "HandlerOut", "InStreamWithCRC", "ItemNameUtils",
        "OutStreamWithCRC", "ParseProperties"
    }
    for _, f in ipairs(arcommon) do
        add("CPP/7zip/Archive/Common/" .. f .. ".cpp")
    end

    local z7 = {
        "7zCompressionMode", "7zDecode", "7zEncode", "7zExtract", "7zFolderInStream",
        "7zHandler", "7zHandlerOut", "7zHeader", "7zIn", "7zOut", "7zProperties",
        "7zSpecStream", "7zUpdate", "7zRegister"
    }
    for _, f in ipairs(z7) do
        add("CPP/7zip/Archive/7z/" .. f .. ".cpp")
    end

    local compress = {
        "CodecExports", "Bcj2Coder", "Bcj2Register", "BcjCoder", "BcjRegister",
        "BranchMisc", "BranchRegister", "ByteSwap", "CopyCoder", "CopyRegister",
        "DeltaFilter", "Lzma2Decoder", "Lzma2Encoder", "Lzma2Register",
        "LzmaDecoder", "LzmaEncoder", "LzmaRegister"
    }
    for _, f in ipairs(compress) do
        add("CPP/7zip/Compress/" .. f .. ".cpp")
    end

    local c = {
        "7zStream", "Alloc", "Bcj2", "Bcj2Enc", "Bra", "Bra86", "BraIA64",
        "CpuArch", "Delta", "LzFind", "LzFindMt", "LzFindOpt", "Lzma2Dec",
        "Lzma2DecMt", "Lzma2Enc", "LzmaDec", "LzmaEnc", "MtCoder", "MtDec",
        "SwapBytes", "Threads", "7zCrc", "7zCrcOpt",
        "7zAlloc", "7zBuf", "7zFile", "7zDec", "7zArcIn"
    }
    for _, f in ipairs(c) do
        add("C/" .. f .. ".c")
    end

    add("spartan/create_object.cpp")
    add("spartan/archive_create.cpp")
    add("spartan/archive_extract.cpp")

    return files_list
end

return {
    root = LZMA_ROOT,
    sources = lzma_sources,
}
