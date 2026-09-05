-- Copyright(c) 2015-2026 Panos Karabelas
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is furnished
-- to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
-- FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
-- COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
-- IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
-- CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

-- headers for xess, dlss and steamworks live in third_party, their libs and dlls ship in libraries.7z
-- the only sdk fetched separately is the d3d12 agility sdk (nuget, not part of the archive)

local setup = {}

local PROJECT_ROOT     = path.getabsolute(path.join(_MAIN_SCRIPT_DIR or _SCRIPT_DIR, ".."))
local BINARIES_DIR     = path.join(PROJECT_ROOT, "binaries")
local DATA_DIR         = path.join(PROJECT_ROOT, "data")
local LIBRARIES_DIR    = path.join(PROJECT_ROOT, "third_party", "libraries")
local ARCHIVE_PATH     = path.join(LIBRARIES_DIR, "libraries.7z")
-- portable 7zr, fetched on demand only for libraries.7z (zips use tar)
local SEVEN_ZIP_CACHE  = path.join(PROJECT_ROOT, "third_party", "lzma_sdk", "bin", "7zr.exe")
local SEVEN_ZIP_URL    = "https://www.7-zip.org/a/7zr.exe"

local LIBRARY_URL      = "https://www.dropbox.com/scl/fi/ryshk2l75pjp10fgxkw78/libraries.7z?rlkey=ml1dlwvjnobmsd4kv738aekp7&dl=1"
local LIBRARY_HASH     = "b272cb398e769bcbe08b5070b637b8899fd6c277f55dd083401956449fd13143"

local RUNTIME_DLLS     = {
    path.join(LIBRARIES_DIR, "dxcompiler.dll"),
    path.join(LIBRARIES_DIR, "libxess.dll"),
    path.join(LIBRARIES_DIR, "nvngx_dlss.dll"),
}

-- d3d12 agility sdk, downloaded on demand into third_party/d3d12_agility
-- the middle number of the nuget version is the D3D12SDKVersion exported by the exe
local AGILITY_VERSION     = "1.619.5"
local AGILITY_SDK_VERSION = "619"
local AGILITY_DIR         = path.join(PROJECT_ROOT, "third_party", "d3d12_agility")
local AGILITY_INCLUDE_DIR = path.join(AGILITY_DIR, "include")
local AGILITY_BIN_DIR     = path.join(AGILITY_DIR, "bin", "x64")
local AGILITY_STAMP       = path.join(AGILITY_DIR, "version.txt")
local AGILITY_URL         = "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/" .. AGILITY_VERSION
local AGILITY_NUPKG       = path.join(PROJECT_ROOT, "third_party", "d3d12_agility.nupkg")

-- d3d12core.dll sits next to the exe, matching the exported D3D12SDKPath (".\\")
local AGILITY_RUNTIME_DLLS = { "D3D12Core.dll", "d3d12SDKLayers.dll" }

-- steamworks sdk, committed in third_party/steamworks (headers and win64 redistributables)
local STEAMWORKS_DIR   = path.join(PROJECT_ROOT, "third_party", "steamworks")
local STEAM_DLL        = path.join(STEAMWORKS_DIR, "redistributable_bin", "win64", "steam_api64.dll")
local STEAM_APP_ID     = "480" -- valve spacewar test appid, replace with the real one

local function is_windows()
    return os.host() == "windows"
end

local function file_exists(p)
    local f = io.open(p, "rb")
    if f then f:close() return true end
    return false
end

local function shell_path(p)
    if is_windows() then
        return path.translate(p, "\\")
    end
    return p
end

local function quote(p)
    return '"' .. shell_path(p) .. '"'
end

local function run(cmd)
    if is_windows() then
        cmd = '"' .. cmd .. '"'
    end
    return os.execute(cmd)
end

local function read_text(p)
    local f = io.open(p, "rb")
    if not f then return nil end
    local contents = f:read("*a") or ""
    f:close()
    return (contents:gsub("%s+", ""))
end

local function download_with_progress(url, destination)
    local last_percent = -1
    local result, code = http.download(url, destination, {
        progress = function(total, current)
            if total and total > 0 then
                local percent = math.floor((current / total) * 100)
                if percent ~= last_percent and percent % 5 == 0 then
                    io.write(string.format("\r  progress: %3d%%", percent))
                    io.flush()
                    last_percent = percent
                end
            end
        end
    })
    io.write("\n")
    return result, code
end

local function compute_sha256(p)
    if not file_exists(p) then return nil end

    local cmd
    if is_windows() then
        cmd = string.format(
            'powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath %s).Hash.ToLower()"',
            quote(p)
        )
    else
        cmd = string.format("sha256sum %s | awk '{print $1}'", quote(p))
    end

    local handle = io.popen(cmd, "r")
    if not handle then return nil end
    local result = handle:read("*a") or ""
    handle:close()
    return (result:gsub("%s+", ""))
end

local function copy_file(src, dst)
    os.mkdir(path.getdirectory(dst))
    local ok, err = os.copyfile(src, dst)
    if not ok then
        error(string.format("failed to copy %s -> %s: %s", src, dst, tostring(err)))
    end
end

local function copy_dir(src, dst)
    if not os.isdir(src) then
        error("source directory not found: " .. src)
    end

    if os.isdir(dst) then
        os.rmdir(dst)
    end
    os.mkdir(dst)

    local cmd
    if is_windows() then
        cmd = string.format('xcopy /E /I /Y /Q %s %s >nul', quote(src), quote(dst))
    else
        cmd = string.format('cp -r %s/. %s', quote(src), quote(dst))
    end

    local ok = run(cmd)
    if ok ~= true and ok ~= 0 then
        error(string.format("failed to copy directory %s -> %s", src, dst))
    end
end

local function download_archive()
    os.mkdir(LIBRARIES_DIR)

    print("downloading " .. LIBRARY_URL)
    print("  -> " .. ARCHIVE_PATH)

    local result, code = download_with_progress(LIBRARY_URL, ARCHIVE_PATH)

    if result ~= "OK" then
        error(string.format("download failed: %s (http %s)", tostring(result), tostring(code)))
    end
end

local function which_command(name)
    local cmd
    if is_windows() then
        cmd = string.format('where %s 2>nul', name)
    else
        cmd = string.format('command -v %s 2>/dev/null', name)
    end
    local handle = io.popen(cmd, "r")
    if not handle then
        return nil
    end
    local result = handle:read("*l")
    handle:close()
    if result and result ~= "" then
        return (result:gsub("%s+$", ""))
    end
    return nil
end

local function ensure_7zr()
    if file_exists(SEVEN_ZIP_CACHE) then
        return SEVEN_ZIP_CACHE
    end

    for _, name in ipairs({ "7zr", "7z", "7za" }) do
        local found = which_command(name)
        if found then
            return found
        end
    end

    if not is_windows() then
        error("7zr/7z not found on PATH, needed to extract libraries.7z")
    end

    print("fetching portable 7zr for libraries.7z...")
    os.mkdir(path.getdirectory(SEVEN_ZIP_CACHE))
    local result, code = download_with_progress(SEVEN_ZIP_URL, SEVEN_ZIP_CACHE)
    if result ~= "OK" or not file_exists(SEVEN_ZIP_CACHE) then
        error(string.format("failed to download 7zr.exe: %s (http %s)", tostring(result), tostring(code)))
    end
    return SEVEN_ZIP_CACHE
end

local function extract_zip(archive, destination)
    os.mkdir(destination)
    -- windows 10+ and linux both ship a tar that can read zip
    local cmd = string.format('tar -xf %s -C %s', quote(archive), quote(destination))
    print("extracting " .. path.getname(archive) .. "...")
    local ok = run(cmd)
    if ok ~= true and ok ~= 0 then
        error("zip extraction failed (cmd: " .. cmd .. ")")
    end
end

local function extract_archive()
    local seven_zip = ensure_7zr()
    local cmd = string.format('%s x %s -o%s -aoa -bso0 -bsp1',
        quote(seven_zip), quote(ARCHIVE_PATH), quote(LIBRARIES_DIR))

    print("extracting libraries.7z...")
    local ok = run(cmd)
    if ok ~= true and ok ~= 0 then
        error("extraction failed (cmd: " .. cmd .. ")")
    end
end

local function ensure_archive()
    if file_exists(ARCHIVE_PATH) then
        local hash = compute_sha256(ARCHIVE_PATH)
        if hash == LIBRARY_HASH then
            print("libraries.7z present and hash matches, skipping download")
            return
        end
        print("libraries.7z hash mismatch, redownloading")
        os.remove(ARCHIVE_PATH)
    end

    download_archive()

    local hash = compute_sha256(ARCHIVE_PATH)
    if hash ~= LIBRARY_HASH then
        error(string.format("hash mismatch after download (expected %s, got %s)",
            LIBRARY_HASH, tostring(hash)))
    end
end

local function ensure_agility_sdk()
    if not is_windows() then
        print("  not windows, skipping agility sdk")
        return
    end

    if read_text(AGILITY_STAMP) == AGILITY_VERSION and file_exists(path.join(AGILITY_BIN_DIR, "D3D12Core.dll")) then
        print("agility sdk " .. AGILITY_VERSION .. " present, skipping download")
        return
    end

    print("downloading agility sdk " .. AGILITY_VERSION .. "...")
    os.mkdir(path.getdirectory(AGILITY_NUPKG))

    local result, code = download_with_progress(AGILITY_URL, AGILITY_NUPKG)
    if result ~= "OK" then
        error(string.format("agility sdk download failed: %s (http %s)", tostring(result), tostring(code)))
    end

    local extract_root = path.join(PROJECT_ROOT, "third_party", "d3d12_agility_extract")
    if os.isdir(extract_root) then
        os.rmdir(extract_root)
    end

    -- the nupkg is a plain zip
    extract_zip(AGILITY_NUPKG, extract_root)

    local native_root = path.join(extract_root, "build", "native")
    if not os.isdir(native_root) then
        error("unexpected agility sdk archive layout")
    end

    if os.isdir(AGILITY_DIR) then
        os.rmdir(AGILITY_DIR)
    end
    os.mkdir(AGILITY_DIR)

    -- headers must shadow the windows sdk copies, so they live on their own include root
    copy_dir(path.join(native_root, "include"), AGILITY_INCLUDE_DIR)
    copy_dir(path.join(native_root, "bin", "x64"), AGILITY_BIN_DIR)

    os.rmdir(extract_root)
    os.remove(AGILITY_NUPKG)

    local f = io.open(AGILITY_STAMP, "wb")
    f:write(AGILITY_VERSION)
    f:close()

    print("agility sdk " .. AGILITY_VERSION .. " installed (D3D12SDKVersion " .. AGILITY_SDK_VERSION .. ")")
end

local function stage_agility_runtime()
    if not is_windows() then
        return
    end

    local staged = 0

    for _, dll in ipairs(AGILITY_RUNTIME_DLLS) do
        local source = path.join(AGILITY_BIN_DIR, dll)
        if file_exists(source) then
            copy_file(source, path.join(BINARIES_DIR, dll))
            staged = staged + 1
        end
    end

    if staged > 0 then
        print(string.format("  staged %d agility dll(s) into binaries/", staged))
    else
        print("  agility sdk not found, skipping agility staging")
    end
end

local function stage_steam_runtime()
    if not file_exists(STEAM_DLL) then
        print("  steamworks sdk not found, skipping steam staging")
        return
    end

    copy_file(STEAM_DLL, path.join(BINARIES_DIR, path.getname(STEAM_DLL)))

    local appid_path = path.join(BINARIES_DIR, "steam_appid.txt")
    if not file_exists(appid_path) then
        local f = io.open(appid_path, "wb")
        f:write(STEAM_APP_ID)
        f:close()
    end
    print("  staged steam_api64.dll and steam_appid.txt")
end

function setup.run()
    print("\n[1/5] copying data files into binaries...")
    copy_dir(DATA_DIR, path.join(BINARIES_DIR, "data"))

    print("\n[2/5] ensuring libraries archive is present...")
    ensure_archive()

    print("\n[3/5] extracting archive...")
    extract_archive()

    print("\n[4/5] ensuring d3d12 agility sdk...")
    ensure_agility_sdk()

    print("\n[5/5] copying runtime dlls into binaries...")
    for _, dll in ipairs(RUNTIME_DLLS) do
        if file_exists(dll) then
            copy_file(dll, path.join(BINARIES_DIR, path.getname(dll)))
        else
            print("  missing runtime dll: " .. path.getname(dll))
        end
    end

    stage_agility_runtime()
    stage_steam_runtime()

    print("\nsetup complete")
end

setup.agility_sdk_version = AGILITY_SDK_VERSION
setup.agility_stamp_path  = AGILITY_STAMP

return setup
