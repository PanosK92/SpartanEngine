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

local setup = {}

local PROJECT_ROOT     = path.getabsolute(path.join(_MAIN_SCRIPT_DIR or _SCRIPT_DIR, ".."))
local BINARIES_DIR     = path.join(PROJECT_ROOT, "binaries")
local DATA_DIR         = path.join(PROJECT_ROOT, "data")
local LIBRARIES_DIR    = path.join(PROJECT_ROOT, "third_party", "libraries")
local TOOLS_DIR        = path.join(PROJECT_ROOT, "tools")
local SEVEN_ZIP        = path.join(TOOLS_DIR, "7z.exe")
local ARCHIVE_PATH     = path.join(LIBRARIES_DIR, "libraries.7z")
local ENGINE_AUDIO_LICENSES = {
    {
        "engine_sim_LICENSE.txt",
        "engine_audio_engine_sim_LICENSE.txt"
    }
}

local LIBRARY_URL      = "https://www.dropbox.com/scl/fi/1ikk55avwntblfhf3at3z/libraries.7z?rlkey=iexhlu58ouo603bv6i7kwkxxi&dl=1"
local LIBRARY_HASH     = "3b3586bd80a6dbe170351f3b28e5dabef2a55eec63b39662fbce128600b24105"

local RUNTIME_DLLS     = {
    path.join(LIBRARIES_DIR, "dxcompiler.dll"),
    path.join(LIBRARIES_DIR, "libxess.dll"),
}

-- steamworks sdk, downloaded on demand into third_party/steamworks
local STEAMWORKS_DIR   = path.join(PROJECT_ROOT, "third_party", "steamworks")
local STEAM_DLL        = path.join(STEAMWORKS_DIR, "redistributable_bin", "win64", "steam_api64.dll")
local STEAM_LIB        = path.join(STEAMWORKS_DIR, "redistributable_bin", "win64", "steam_api64.lib")
local STEAMWORKS_URL   = "https://github.com/rlabrecque/SteamworksSDK/archive/refs/heads/main.zip"
local STEAMWORKS_ZIP   = path.join(PROJECT_ROOT, "third_party", "steamworks_sdk.zip")
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

    local last_percent = -1
    local result, code = http.download(LIBRARY_URL, ARCHIVE_PATH, {
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

    if result ~= "OK" then
        error(string.format("download failed: %s (http %s)", tostring(result), tostring(code)))
    end
end

local function extract_archive()
    if not file_exists(SEVEN_ZIP) then
        error("7z executable missing at " .. SEVEN_ZIP)
    end

    local cmd = string.format('%s x %s -o%s -aoa -bso0 -bsp1',
        quote(SEVEN_ZIP), quote(ARCHIVE_PATH), quote(LIBRARIES_DIR))

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

local function ensure_steamworks()
    if file_exists(STEAM_DLL) and file_exists(STEAM_LIB) then
        print("steamworks sdk present, skipping download")
        return
    end

    if not file_exists(SEVEN_ZIP) then
        print("  7z missing, cannot install steamworks sdk")
        return
    end

    print("downloading steamworks sdk...")
    os.mkdir(path.getdirectory(STEAMWORKS_ZIP))

    local last_percent = -1
    local result, code = http.download(STEAMWORKS_URL, STEAMWORKS_ZIP, {
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

    if result ~= "OK" then
        print(string.format("  steamworks download failed: %s (http %s)", tostring(result), tostring(code)))
        return
    end

    local extract_root = path.join(PROJECT_ROOT, "third_party", "steamworks_extract")
    if os.isdir(extract_root) then
        os.rmdir(extract_root)
    end
    os.mkdir(extract_root)

    local extract_cmd = string.format('%s x %s -o%s -aoa -bso0 -bsp1',
        quote(SEVEN_ZIP), quote(STEAMWORKS_ZIP), quote(extract_root))
    print("extracting steamworks sdk...")
    local ok = run(extract_cmd)
    if ok ~= true and ok ~= 0 then
        print("  steamworks extraction failed")
        return
    end

    local sdk_root = path.join(extract_root, "SteamworksSDK-main")
    if not os.isdir(sdk_root) then
        print("  unexpected steamworks archive layout")
        return
    end

    if os.isdir(STEAMWORKS_DIR) then
        os.rmdir(STEAMWORKS_DIR)
    end
    os.mkdir(STEAMWORKS_DIR)

    copy_dir(path.join(sdk_root, "public"), path.join(STEAMWORKS_DIR, "public"))
    copy_dir(path.join(sdk_root, "redistributable_bin"), path.join(STEAMWORKS_DIR, "redistributable_bin"))

    os.rmdir(extract_root)
    os.remove(STEAMWORKS_ZIP)

    if file_exists(STEAM_DLL) and file_exists(STEAM_LIB) then
        print("steamworks sdk installed")
    else
        print("  steamworks sdk install incomplete")
    end
end

function setup.run()
    print("\n[1/5] copying data files into binaries...")
    copy_dir(DATA_DIR, path.join(BINARIES_DIR, "data"))
    copy_file(path.join(TOOLS_DIR, "7z.exe"), path.join(BINARIES_DIR, "7z.exe"))
    copy_file(path.join(TOOLS_DIR, "7z.dll"), path.join(BINARIES_DIR, "7z.dll"))
    for _, license in ipairs(ENGINE_AUDIO_LICENSES) do
        copy_file(
            path.join(
                PROJECT_ROOT,
                "source",
                "runtime",
                "Audio",
                "Engine",
                license[1]
            ),
            path.join(
                BINARIES_DIR,
                "project",
                "licenses",
                license[2]
            )
        )
    end

    print("\n[2/5] ensuring libraries archive is present...")
    ensure_archive()

    print("\n[3/5] extracting archive...")
    extract_archive()

    print("\n[4/5] ensuring steamworks sdk...")
    ensure_steamworks()

    print("\n[5/5] copying runtime dlls into binaries...")
    for _, dll in ipairs(RUNTIME_DLLS) do
        copy_file(dll, path.join(BINARIES_DIR, path.getname(dll)))
    end

    if file_exists(STEAM_DLL) then
        copy_file(STEAM_DLL, path.join(BINARIES_DIR, path.getname(STEAM_DLL)))

        local appid_path = path.join(BINARIES_DIR, "steam_appid.txt")
        if not file_exists(appid_path) then
            local f = io.open(appid_path, "wb")
            f:write(STEAM_APP_ID)
            f:close()
        end
        print("  staged steam_api64.dll and steam_appid.txt")
    else
        print("  steamworks sdk not found, skipping steam staging")
    end

    print("\nsetup complete")
end

return setup
