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
local BUILD_SCRIPTS    = path.join(PROJECT_ROOT, "build_scripts")
local SEVEN_ZIP        = path.join(BUILD_SCRIPTS, "7z.exe")
local ARCHIVE_PATH     = path.join(LIBRARIES_DIR, "libraries.7z")

local LIBRARY_URL      = "https://www.dropbox.com/scl/fi/soird6jf3416nd43tr3hl/libraries.7z?rlkey=0k7i9sb5jsdcqgd2dw3f2n82s&dl=1"
local LIBRARY_HASH     = "3f74a14c1c686c8a5ac13e0cb5480b817b6ab671efae9fe11733a728899f4ed1"

local RUNTIME_DLLS     = {
    path.join(LIBRARIES_DIR, "dxcompiler.dll"),
    path.join(LIBRARIES_DIR, "libxess.dll"),
}

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

function setup.is_already_set_up()
    for _, dll in ipairs(RUNTIME_DLLS) do
        if not file_exists(dll) then return false end
    end
    if not os.isdir(path.join(BINARIES_DIR, "data")) then return false end
    if not file_exists(path.join(BINARIES_DIR, "7z.exe")) then return false end
    return true
end

function setup.run()
    print("\n[1/4] copying data files into binaries...")
    copy_dir(DATA_DIR, path.join(BINARIES_DIR, "data"))
    copy_file(path.join(BUILD_SCRIPTS, "7z.exe"), path.join(BINARIES_DIR, "7z.exe"))
    copy_file(path.join(BUILD_SCRIPTS, "7z.dll"), path.join(BINARIES_DIR, "7z.dll"))

    print("\n[2/4] ensuring libraries archive is present...")
    ensure_archive()

    print("\n[3/4] extracting archive...")
    extract_archive()

    print("\n[4/4] copying runtime dlls into binaries...")
    for _, dll in ipairs(RUNTIME_DLLS) do
        copy_file(dll, path.join(BINARIES_DIR, path.getname(dll)))
    end

    print("\nsetup complete")
end

return setup
