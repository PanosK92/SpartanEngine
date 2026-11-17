-- Copyright(c) 2015-2026 Panos Karabelas
-- Licensed under the Spartan Engine License. See license.md in the repository root.
-- https://github.com/PanosK92/SpartanEngine/blob/master/license.md
-- Commercial use requires written permission and negotiated payment terms.

-- lzma sdk sources: in-process 7z extract + create (compiled into spartan)
-- C++ units are grouped into unity translation units so <windows.h> / Common.h are parsed 4 times instead of 77
local LZMA_ROOT = path.getabsolute(path.join(_MAIN_SCRIPT_DIR or _SCRIPT_DIR, "../third_party/lzma_sdk"))

local function lzma_sources()
    local files_list = {}
    local function add(rel)
        table.insert(files_list, path.join(LZMA_ROOT, rel))
    end

    local unity_cpp = {
        "unity_cpp_common_win",
        "unity_cpp_7zip_common",
        "unity_cpp_7zip_7z",
        "unity_cpp_7zip_compress",
        "create_object",
        "archive_create",
        "archive_extract",
        "file_sha256"
    }
    for _, f in ipairs(unity_cpp) do
        add("spartan/" .. f .. ".cpp")
    end

    local c = {
        "7zStream", "Alloc", "Bcj2", "Bcj2Enc", "Bra", "Bra86", "BraIA64",
        "CpuArch", "Delta", "LzFind", "LzFindMt", "LzFindOpt", "Lzma2Dec",
        "Lzma2DecMt", "Lzma2Enc", "LzmaDec", "LzmaEnc", "MtCoder", "MtDec",
        "SwapBytes", "Threads", "7zCrc", "7zCrcOpt",
        "7zAlloc", "7zBuf", "7zFile", "7zDec", "7zArcIn",
        "Sha256", "Sha256Opt"
    }
    for _, f in ipairs(c) do
        add("C/" .. f .. ".c")
    end

    return files_list
end

return {
    root = LZMA_ROOT,
    sources = lzma_sources,
}
