-- Copyright(c) 2015-2026 Panos Karabelas
-- Licensed under the Spartan Engine License. See license.md in the repository root.
-- https://github.com/PanosK92/SpartanEngine/blob/master/license.md
-- Commercial use requires written permission and negotiated payment terms.

local setup = {}
local ui    = dofile(path.join(_MAIN_SCRIPT_DIR or _SCRIPT_DIR, "console_ui.lua"))

local PROJECT_ROOT     = path.getabsolute(path.join(_MAIN_SCRIPT_DIR or _SCRIPT_DIR, ".."))
local BINARIES_DIR     = path.join(PROJECT_ROOT, "binaries")
local RUNTIME_DIR      = path.join(BINARIES_DIR, "third_party")
local DATA_DIR         = path.join(PROJECT_ROOT, "data")
local LIBRARIES_DIR    = path.join(PROJECT_ROOT, "third_party", "libraries")
local ARCHIVE_PATH     = path.join(LIBRARIES_DIR, "libraries.7z")
-- portable 7zr, fetched on demand only for libraries.7z (zips use tar)
local SEVEN_ZIP_CACHE  = path.join(PROJECT_ROOT, "third_party", "lzma_sdk", "bin", "7zr.exe")
local SEVEN_ZIP_URL    = "https://www.7-zip.org/a/7zr.exe"

local LIBRARY_URL      = "https://www.dropbox.com/scl/fi/byrj4cytjj9krvm49pto6/libraries.7z?rlkey=w7axjqiqdooded5tpovg80nn2&dl=1"
local LIBRARY_HASH     = "f7b452e04879967408109f95c7dfba1dbd86ea8358b2a805edb825e7bcdfcbf4"

local LINUX_LIBRARIES_DIR     = path.join(LIBRARIES_DIR, "linux")
local LINUX_RUNTIME_LIBRARIES = { "libdxcompiler.so", "libdxil.so" }
local MACOS_LIBRARIES_DIR     = path.join(LIBRARIES_DIR, "macos")
local MACOS_RUNTIME_LIBRARIES = { "libdxcompiler.dylib", "libmetalirconverter.dylib" }

local RUNTIME_DLLS     = {
    path.join(LIBRARIES_DIR, "dxcompiler.dll"),
    path.join(LIBRARIES_DIR, "libxess.dll"),
    path.join(LIBRARIES_DIR, "nvngx_dlss.dll"),
    path.join(LIBRARIES_DIR, "amd_fidelityfx_dx12.dll"),
    path.join(LIBRARIES_DIR, "amd_fidelityfx_vk.dll"),
    path.join(LIBRARIES_DIR, "amd_fidelityfx_loader_dx12.dll"),
    path.join(LIBRARIES_DIR, "amd_fidelityfx_loader_vk.dll"),
    path.join(LIBRARIES_DIR, "amd_fidelityfx_upscaler_dx12.dll"),
    path.join(LIBRARIES_DIR, "amd_fidelityfx_upscaler_vk.dll"),
}

-- An untargeted setup stages both native upscaler runtimes. Targeted generation
-- keeps the other backends' installed SDK files intact during extraction.
local function needs_vendor_file(name)
    if ARG_API_GRAPHICS == "webgpu" then
        return not (name:match("^amd_fidelityfx_") or name:match("^libxess%.")
            or name:match("^nvsdk_ngx_") or name == "nvngx_dlss.dll")
    elseif ARG_API_GRAPHICS == "vulkan" then
        return not name:match("_dx12%.dll$")
    elseif ARG_API_GRAPHICS == "d3d12" then
        return not name:match("_vk%.dll$")
    end
    return true
end

-- FidelityFX SDK 1.1.4 signed runtime, fsr 3.1 frame generation; ffx api headers are vendored in third_party/fidelityfx.
local FIDELITYFX_VERSION = "1.1.4"
local FIDELITYFX_URL = "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/v" .. FIDELITYFX_VERSION .. "/PrebuiltSignedDLL/"
local FIDELITYFX_FILES = {
    { name = "amd_fidelityfx_dx12.dll", sha256 = "12a5081257ec95b0b53ad51b4a87fb3c03f97fe0bbb59f9496968f8d50ef93a6" },
    { name = "amd_fidelityfx_vk.dll",   sha256 = "a1624cc4238fef046f30c4d80ce3f47be63fc5f5373f49e3ee9edb9960f54c78" },
}

-- FSR SDK 2.4.0 upscaling only; retain the existing cross-vendor frame generation runtime.
local FSR_UPSCALING_VERSION = "2.4.0"
local FSR_UPSCALING_URL = "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/v" .. FSR_UPSCALING_VERSION .. "/Kits/FidelityFX/signedbin/"
local FSR_UPSCALING_FILES = {
    { name = "amd_fidelityfx_loader_dx12.dll", sha256 = "e2191d3c0828be45eb8affd4e3ec0378f2293adc345b772a7170edcf3b021ecb" },
    { name = "amd_fidelityfx_loader_vk.dll", sha256 = "38a56a01fb3565c0757d9f5c1e4e69de4b65e5f2e5769b323b200dc1f142bab9" },
    { name = "amd_fidelityfx_upscaler_dx12.dll", sha256 = "aad4fccccb29084a655f9a4f9f613bd22c8c03fb4a0f8840148cc6543b4ccace" },
    { name = "amd_fidelityfx_upscaler_vk.dll", sha256 = "4a7e895af6e0b615d40a84d59eb8c2b1054ad91cdd0c739567940270d51bdab7" },
}

-- DLSS 4.5 Super Resolution; headers are vendored in third_party/dlss.
local DLSS_VERSION = "310.9.16"
local DLSS_URL = "https://raw.githubusercontent.com/NVIDIA/DLSS/v" .. DLSS_VERSION .. "/lib/Windows_x86_64/"
-- Keep the KHR static MT/MTd libraries used by both graphics backends.
local DLSS_FILES = {
    { source = "rel/nvngx_dlss.dll", destination = "nvngx_dlss.dll",
      sha256 = "d00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e" },
    { source = "khr/x64/nvsdk_ngx_khr_s.lib", destination = "nvsdk_ngx_s.lib",
      sha256 = "2534d5fb31a38a7b37b11869272915d1f18c4b484ffd61a0b36956bab9ab739e" },
    { source = "khr/x64/nvsdk_ngx_khr_s_dbg.lib", destination = "nvsdk_ngx_s_dbg.lib",
      sha256 = "f2b62b2d2f4322cd6e267f9bdf814194a85ce3e9243f3432af6d95240b55e901" },
}

-- xess-sr overlay from github so upscaler stays current without libraries.7z churn
local XESS_VERSION     = "3.0.2"
local XESS_DIR         = path.join(PROJECT_ROOT, "third_party", "xess")
local XESS_STAMP       = path.join(XESS_DIR, "version.txt")
local XESS_URL         = "https://github.com/intel/xess/releases/download/v" .. XESS_VERSION .. "/XeSS_SDK_" .. XESS_VERSION .. ".zip"
local XESS_ZIP         = path.join(PROJECT_ROOT, "third_party", "XeSS_SDK_" .. XESS_VERSION .. ".zip")

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

-- agility components live in third_party/d3d12, matching the exported D3D12SDKPath
local AGILITY_RUNTIME_DLLS = { "D3D12Core.dll", "d3d12SDKLayers.dll" }

-- steamworks sdk, downloaded on demand into third_party/steamworks
local STEAMWORKS_DIR   = path.join(PROJECT_ROOT, "third_party", "steamworks")
local STEAM_DLL        = path.join(STEAMWORKS_DIR, "redistributable_bin", "win64", "steam_api64.dll")
local STEAM_LIB        = path.join(STEAMWORKS_DIR, "redistributable_bin", "win64", "steam_api64.lib")
local STEAMWORKS_URL   = "https://github.com/rlabrecque/SteamworksSDK/archive/refs/heads/main.zip"
local STEAMWORKS_ZIP   = path.join(PROJECT_ROOT, "third_party", "steamworks_sdk.zip")
local STEAM_APP_ID     = "480" -- valve spacewar test appid, replace with the real one

-- dawn (webgpu), only fetched when generating the webgpu projects
-- the release ships a static /MD library, it's linked into a dll so it can sit next to the /MT engine
-- the headers and the export list are vendored in third_party/webgpu and must match this version
local DAWN_VERSION     = "v20261106.204713"
local DAWN_FOLDER      = "Dawn-66f899221d41dbc6d6257350c4b654a9b375c7f2-windows-latest-Release"
local DAWN_URL         = "https://github.com/google/dawn/releases/download/" .. DAWN_VERSION .. "/" .. DAWN_FOLDER .. ".tar.gz"
local DAWN_HASH        = "d9fef7fa3171feab2680be072aaf099a492fdcb3b4b06b51687a61b79a954b5c"
local DAWN_ARCHIVE     = path.join(LIBRARIES_DIR, "dawn_windows.tar.gz")
local DAWN_STAMP       = path.join(LIBRARIES_DIR, "dawn_version.txt")
local DAWN_DEF         = path.join(PROJECT_ROOT, "third_party", "webgpu", "webgpu_dawn.def")
local DAWN_SYSTEM_LIBS = "d3d12.lib dxgi.lib d3d11.lib dxguid.lib user32.lib kernel32.lib gdi32.lib ole32.lib onecore.lib"

local function is_windows()
    return os.host() == "windows"
end

local function file_exists(p)
    local f = io.open(p, "rb")
    if f then f:close() return true end
    return false
end

local function file_size(p)
    local f = io.open(p, "rb")
    if not f then return 0 end
    local sz = f:seek("end") or 0
    f:close()
    return sz
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

local function download_with_progress(url, destination, label)
    local display_label = label or path.getname(destination)
    local prog          = ui.create_progress(display_label)
    local started       = os.clock()
    local total_bytes   = 0

    prog:update(0, "connecting...")

    local result, code = http.download(url, destination, {
        progress = function(total, current)
            if total and total > 0 then
                total_bytes   = total
                local percent = (current / total) * 100
                local elapsed = math.max(os.clock() - started, 0.001)
                local rate    = current / elapsed
                local eta     = rate > 0 and ((total - current) / rate) or 0
                local detail  = string.format("%s / %s  %s  %s/s", ui.format_bytes(current), ui.format_bytes(total), ui.glyph.dot, ui.format_bytes(rate))
                if eta >= 1.0 and current < total then
                    detail = detail .. "  " .. ui.glyph.dot .. "  ETA " .. ui.format_duration(eta)
                end
                prog:update(percent, detail)
            elseif current and current > 0 then
                prog:update(0, ui.format_bytes(current))
            end
        end
    })

    if result == "OK" then
        local sz = total_bytes > 0 and total_bytes or file_size(destination)
        prog:finish("downloaded (" .. ui.format_bytes(sz) .. ")", "ok")
    else
        ui.activity_clear()
        ui.item_err(display_label, string.format("download failed: %s (http %s)", tostring(result), tostring(code)))
    end

    return result, code
end

local function compute_sha256(p)
    if not file_exists(p) then return nil end

    local cmd
    if is_windows() then
        -- Avoid starting a PowerShell runtime for every library on a clean setup.
        -- Only the digest line is independent of the Windows display language.
        local process = io.popen('""%SystemRoot%\\System32\\certutil.exe" -hashfile ' .. quote(p) .. ' SHA256 2>nul"', "r")
        if process then
            local digest
            for line in process:lines() do
                local hex = line:gsub("%s", "")
                if #hex == 64 and hex:match("^%x+$") then
                    digest = hex:lower()
                end
            end
            if process:close() and digest then
                return digest
            end
        end
        -- Use .NET directly: PowerShell 7's inherited module path can hide
        -- Get-FileHash from Windows PowerShell during project generation.
        local literal_path = shell_path(p):gsub("'", "''")
        cmd = string.format(
            'powershell -NoProfile -Command "$s = [IO.File]::OpenRead(\'%s\'); ' ..
            '$h = [Security.Cryptography.SHA256]::Create(); try { ' ..
            '[BitConverter]::ToString($h.ComputeHash($s)).Replace(\'-\', \'\').ToLowerInvariant() ' ..
            '} finally { $s.Dispose(); $h.Dispose() }"',
            literal_path
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

local function files_identical(a, b)
    local fa = io.open(a, "rb")
    if not fa then return false end
    local fb = io.open(b, "rb")
    if not fb then fa:close() return false end

    local identical = fa:seek("end") == fb:seek("end")
    fa:seek("set")
    fb:seek("set")
    while identical do
        local chunk_a = fa:read(1024 * 1024)
        local chunk_b = fb:read(1024 * 1024)
        if chunk_a ~= chunk_b then
            identical = false
        elseif not chunk_a then
            break
        end
    end

    fa:close()
    fb:close()
    return identical
end

local function copy_file(src, dst)
    -- a running engine locks its dlls, so leave identical copies alone
    if files_identical(src, dst) then
        return false
    end

    os.mkdir(path.getdirectory(dst))
    -- macos caches the code signature of a binary per file, overwriting one in place gets the next launch killed
    os.remove(dst)
    local ok, err = os.copyfile(src, dst)
    if not ok then
        error(string.format("failed to copy %s -> %s: %s", src, dst, tostring(err)))
    end
    return true
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

    local result, code = download_with_progress(LIBRARY_URL, ARCHIVE_PATH, "libraries.7z")

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

    os.mkdir(path.getdirectory(SEVEN_ZIP_CACHE))
    local result, code = download_with_progress(SEVEN_ZIP_URL, SEVEN_ZIP_CACHE, "7zr.exe (portable)")
    if result ~= "OK" or not file_exists(SEVEN_ZIP_CACHE) then
        error(string.format("failed to download 7zr.exe: %s (http %s)", tostring(result), tostring(code)))
    end
    return SEVEN_ZIP_CACHE
end

local function extract_zip(archive, destination, label)
    os.mkdir(destination)
    local name    = label or path.getname(archive)
    local started = os.clock()
    ui.activity_start(name, "extracting...")
    -- windows 10+ and linux both ship a tar that can read zip
    local cmd = string.format('tar -xf %s -C %s', quote(archive), quote(destination))
    local ok  = run(cmd)
    if ok ~= true and ok ~= 0 then
        ui.activity_done("err", name, "extraction failed")
        error("zip extraction failed (cmd: " .. cmd .. ")")
    end
    ui.activity_done("ok", name, "extracted " .. ui.glyph.dot .. " " .. ui.format_duration(os.clock() - started))
end

local function extract_archive()
    local seven_zip = ensure_7zr()
    local cmd = string.format('%s x %s -o%s -aoa -bso0 -bsp1',
        quote(seven_zip), quote(ARCHIVE_PATH), quote(LIBRARIES_DIR))
    for _, dll in ipairs(RUNTIME_DLLS) do
        local name = path.getname(dll)
        if not needs_vendor_file(name) then
            cmd = cmd .. " -x!" .. name
        end
    end
    if ARG_API_GRAPHICS == "webgpu" then
        cmd = cmd .. " -x!libxess.lib -x!nvsdk_ngx_s.lib -x!nvsdk_ngx_s_dbg.lib"
    end

    local prog = ui.create_progress("libraries.7z")
    prog:update(0, "extracting...")

    local popen_cmd = is_windows() and ('"' .. cmd .. ' 2>&1"') or (cmd .. " 2>&1")
    local handle    = io.popen(popen_cmd, "r")
    if handle then
        local digits = ""
        while true do
            local ch = handle:read(1)
            if not ch then
                break
            end
            if ch >= "0" and ch <= "9" then
                digits = digits .. ch
            elseif ch == "%" and #digits > 0 then
                local pct = tonumber(digits)
                if pct and pct >= 0 and pct <= 100 then
                    prog:update(pct, "extracting into third_party/libraries/")
                end
                digits = ""
            else
                digits = ""
            end
        end
        local ok, _, code = handle:close()
        if ok ~= true and ok ~= 0 and code ~= 0 then
            ui.activity_clear()
            ui.item_err("libraries.7z", "extraction failed")
            error("extraction failed (cmd: " .. cmd .. ")")
        end
        prog:update(100, "extracting into third_party/libraries/")
        prog:finish("extracted into third_party/libraries/", "ok")
        return
    end

    local ok = run(cmd)
    if ok ~= true and ok ~= 0 then
        ui.activity_clear()
        ui.item_err("libraries.7z", "extraction failed")
        error("extraction failed (cmd: " .. cmd .. ")")
    end
    prog:finish("extracted into third_party/libraries/", "ok")
end

local function ensure_archive()
    if file_exists(ARCHIVE_PATH) then
        local started = os.clock()
        ui.activity_start("libraries.7z", "verifying SHA-256...")
        local hash = compute_sha256(ARCHIVE_PATH)
        if hash == LIBRARY_HASH then
            local info = ui.format_bytes(file_size(ARCHIVE_PATH)) .. " " .. ui.glyph.dot .. " SHA-256 ok (" .. ui.format_duration(os.clock() - started) .. ")"
            ui.activity_done("cached", "libraries.7z", info)
            return
        end
        ui.activity_done("warn", "libraries.7z", "hash mismatch, redownloading")
        os.remove(ARCHIVE_PATH)
    end

    download_archive()

    ui.activity_start("libraries.7z", "verifying SHA-256...")
    local hash = compute_sha256(ARCHIVE_PATH)
    if hash ~= LIBRARY_HASH then
        ui.activity_done("err", "libraries.7z", "SHA-256 mismatch after download")
        error(string.format("hash mismatch after download (expected %s, got %s)",
            LIBRARY_HASH, tostring(hash)))
    end
    ui.activity_done("ok", "libraries.7z", "SHA-256 verified")
end

local function ensure_agility_sdk()
    if not is_windows() then
        ui.item_skip("D3D12 Agility SDK", "Windows only")
        return
    end

    if read_text(AGILITY_STAMP) == AGILITY_VERSION and file_exists(path.join(AGILITY_BIN_DIR, "D3D12Core.dll")) then
        ui.item_cached("D3D12 Agility SDK " .. AGILITY_VERSION, "D3D12SDKVersion " .. AGILITY_SDK_VERSION)
        return
    end

    os.mkdir(path.getdirectory(AGILITY_NUPKG))

    local result, code = download_with_progress(AGILITY_URL, AGILITY_NUPKG, "D3D12 Agility " .. AGILITY_VERSION)
    if result ~= "OK" then
        error(string.format("agility sdk download failed: %s (http %s)", tostring(result), tostring(code)))
    end

    local extract_root = path.join(PROJECT_ROOT, "third_party", "d3d12_agility_extract")
    if os.isdir(extract_root) then
        os.rmdir(extract_root)
    end

    -- the nupkg is a plain zip
    extract_zip(AGILITY_NUPKG, extract_root, "D3D12 Agility package")

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

    ui.item_ok("D3D12 Agility SDK " .. AGILITY_VERSION, "installed (D3D12SDKVersion " .. AGILITY_SDK_VERSION .. ")")
end

local function stage_agility_runtime()
    if not is_windows() then
        return 0
    end

    local staged  = 0
    local updated = 0

    for _, dll in ipairs(AGILITY_RUNTIME_DLLS) do
        local source = path.join(AGILITY_BIN_DIR, dll)
        if file_exists(source) then
            if copy_file(source, path.join(RUNTIME_DIR, "d3d12", dll)) then
                updated = updated + 1
            end
            staged = staged + 1
        end
    end

    if staged > 0 then
        local state = updated > 0 and string.format("%d copied, %d up to date", updated, staged - updated) or "up to date"
        ui.item_ok("D3D12 Agility runtime", string.format("%d dlls in binaries/third_party/d3d12/ (%s)", staged, state))
    else
        ui.item_warn("D3D12 Agility runtime", "sdk not found, using in-box d3d12")
    end
    return staged
end

local function find_vcvars()
    local vswhere = path.join(os.getenv("ProgramFiles(x86)") or "C:/Program Files (x86)", "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not file_exists(vswhere) then
        return nil
    end
    local handle = io.popen(quote(vswhere) .. " -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath", "r")
    if not handle then
        return nil
    end
    local installation = (handle:read("*l") or ""):gsub("%s+$", "")
    handle:close()
    local vcvars = path.join(installation, "VC", "Auxiliary", "Build", "vcvars64.bat")
    return file_exists(vcvars) and vcvars or nil
end

local function ensure_dawn()
    local dll  = path.join(LIBRARIES_DIR, "webgpu_dawn.dll")
    local lib  = path.join(LIBRARIES_DIR, "webgpu_dawn.lib")
    local tint = path.join(LIBRARIES_DIR, "tint.exe")

    local present = read_text(DAWN_STAMP) == DAWN_VERSION and file_exists(dll) and file_exists(lib) and file_exists(tint)
    if present then
        ui.item_cached("Google Dawn " .. DAWN_VERSION, "webgpu_dawn.dll + tint.exe")
    else
        if compute_sha256(DAWN_ARCHIVE) ~= DAWN_HASH then
            os.mkdir(LIBRARIES_DIR)
            os.remove(DAWN_ARCHIVE)
            local result, code = download_with_progress(DAWN_URL, DAWN_ARCHIVE, "Google Dawn " .. DAWN_VERSION)
            if result ~= "OK" then
                error(string.format("dawn download failed: %s (http %s)", tostring(result), tostring(code)))
            end
            ui.activity_start("Google Dawn", "verifying SHA-256...")
            local hash = compute_sha256(DAWN_ARCHIVE)
            if hash ~= DAWN_HASH then
                ui.activity_done("err", "Google Dawn", "SHA-256 mismatch")
                error(string.format("dawn hash mismatch (expected %s, got %s)", DAWN_HASH, tostring(hash)))
            end
            ui.activity_done("ok", "Google Dawn", "SHA-256 verified")
        end

        local extract_root = path.join(LIBRARIES_DIR, "dawn_extract")
        if os.isdir(extract_root) then
            os.rmdir(extract_root)
        end
        os.mkdir(extract_root)
        local t_extract = os.clock()
        ui.activity_start("Google Dawn", "extracting archive...")
        local cmd = string.format('tar -xzf %s -C %s --strip-components=2 %s/bin/tint.exe %s/lib/webgpu_dawn.lib', quote(DAWN_ARCHIVE), quote(extract_root), DAWN_FOLDER, DAWN_FOLDER)
        local ok = run(cmd)
        if ok ~= true and ok ~= 0 then
            ui.activity_done("err", "Google Dawn", "extraction failed")
            error("dawn extraction failed (cmd: " .. cmd .. ")")
        end
        ui.activity_done("ok", "Google Dawn", "extracted " .. ui.glyph.dot .. " " .. ui.format_duration(os.clock() - t_extract))

        local vcvars = find_vcvars()
        if not vcvars then
            error("dawn needs the msvc linker, install visual studio with the c++ workload")
        end

        -- vcvars chokes on very long inherited paths, a script with a minimal path sidesteps that and the quoting
        local script = path.join(extract_root, "link_dawn.bat")
        local f = io.open(script, "wb")
        f:write("@echo off\r\n")
        f:write("set PATH=%SystemRoot%\\system32;%SystemRoot%\r\n")
        f:write("call " .. quote(vcvars) .. " >nul || exit /b 1\r\n")
        f:write(string.format("link /nologo /DLL /DEF:%s /OUT:%s /IMPLIB:%s /MACHINE:X64 %s %s\r\n", quote(DAWN_DEF), quote(dll), quote(lib), quote(path.join(extract_root, "webgpu_dawn.lib")), DAWN_SYSTEM_LIBS))
        f:close()
        local t_link = os.clock()
        ui.activity_start("webgpu_dawn.dll", "linking with MSVC...")
        ok = run(quote(script))
        if (ok ~= true and ok ~= 0) or not file_exists(dll) then
            ui.activity_done("err", "webgpu_dawn.dll", "link failed")
            error("linking webgpu_dawn.dll failed")
        end
        copy_file(path.join(extract_root, "tint.exe"), tint)
        os.remove(path.join(LIBRARIES_DIR, "webgpu_dawn.exp"))
        os.rmdir(extract_root)

        f = io.open(DAWN_STAMP, "wb")
        f:write(DAWN_VERSION)
        f:close()
        ui.activity_done("ok", "webgpu_dawn.dll", "linked " .. ui.glyph.dot .. " " .. ui.format_duration(os.clock() - t_link))
    end

    -- the shader compiler runs tint next to the executable
    local c1 = copy_file(dll, path.join(RUNTIME_DIR, "webgpu_dawn.dll"))
    local c2 = copy_file(tint, path.join(BINARIES_DIR, "tint.exe"))
    local state = (c1 or c2) and "staged" or "up to date"
    ui.item_ok("WebGPU runtime", "webgpu_dawn.dll + tint.exe (" .. state .. ")")
end

local function ensure_xess_sdk()
    if not is_windows() then
        ui.item_skip("Intel XeSS SDK", "Windows only")
        return
    end

    local required = {
        path.join(XESS_DIR, "xess", "xess.h"),
        path.join(LIBRARIES_DIR, "libxess.lib"),
        path.join(LIBRARIES_DIR, "libxess.dll"),
    }

    local present = read_text(XESS_STAMP) == XESS_VERSION
        and compute_sha256(path.join(LIBRARIES_DIR, "libxess.dll")) == "251659dd84a3e84de67c886a4186e01f3eca49b00641906fe38bb6b807e5d5b7"
        and compute_sha256(path.join(LIBRARIES_DIR, "libxess.lib")) == "5e16bf3745358b54ecfb52f04bc4327536409f981a317819005075f57e0be813"
    if present then
        for _, p in ipairs(required) do
            if not file_exists(p) then
                present = false
                break
            end
        end
    end

    if present then
        ui.item_cached("Intel XeSS SDK " .. XESS_VERSION, "SHA-256 verified")
        return
    end

    os.mkdir(path.getdirectory(XESS_ZIP))

    local result, code = download_with_progress(XESS_URL, XESS_ZIP, "Intel XeSS " .. XESS_VERSION)
    if result ~= "OK" then
        error(string.format("xess sdk download failed: %s (http %s)", tostring(result), tostring(code)))
    end

    local extract_root = path.join(PROJECT_ROOT, "third_party", "xess_sdk_extract")
    if os.isdir(extract_root) then
        os.rmdir(extract_root)
    end

    extract_zip(XESS_ZIP, extract_root, "Intel XeSS archive")

    local sdk_root = extract_root
    if not os.isdir(path.join(sdk_root, "inc")) then
        -- some zips nest one directory
        for _, entry in ipairs(os.matchdirs(path.join(extract_root, "*"))) do
            if os.isdir(path.join(entry, "inc")) then
                sdk_root = entry
                break
            end
        end
    end

    if not os.isdir(path.join(sdk_root, "inc")) then
        error("unexpected xess sdk archive layout")
    end

    os.mkdir(XESS_DIR)
    copy_dir(path.join(sdk_root, "inc", "xess"), path.join(XESS_DIR, "xess"))
    copy_file(path.join(sdk_root, "lib", "libxess.lib"), path.join(LIBRARIES_DIR, "libxess.lib"))
    copy_file(path.join(sdk_root, "bin", "libxess.dll"), path.join(LIBRARIES_DIR, "libxess.dll"))

    local f = io.open(XESS_STAMP, "wb")
    f:write(XESS_VERSION)
    f:close()

    os.rmdir(extract_root)
    os.remove(XESS_ZIP)

    ui.item_ok("Intel XeSS SDK " .. XESS_VERSION, "installed")
end

local function ensure_dlss_sdk()
    if not is_windows() then
        ui.item_skip("NVIDIA DLSS SDK", "Windows only")
        return
    end

    -- Check actual files: extracting libraries.7z can overwrite a newer SDK.
    local downloaded = 0
    for _, entry in ipairs(DLSS_FILES) do
        local destination = path.join(LIBRARIES_DIR, entry.destination)
        if compute_sha256(destination) ~= entry.sha256 then
            local temporary = destination .. ".tmp"
            local result, code = download_with_progress(DLSS_URL .. entry.source, temporary, "DLSS " .. entry.destination)
            if result ~= "OK" or compute_sha256(temporary) ~= entry.sha256 then
                os.remove(temporary)
                error(string.format("dlss download or checksum failed: %s (http %s)", tostring(result), tostring(code)))
            end
            copy_file(temporary, destination)
            os.remove(temporary)
            downloaded = downloaded + 1
        end
    end
    if downloaded > 0 then
        ui.item_ok("NVIDIA DLSS " .. DLSS_VERSION, string.format("%d/%d files updated", downloaded, #DLSS_FILES))
    else
        ui.item_cached("NVIDIA DLSS " .. DLSS_VERSION, string.format("%d files verified", #DLSS_FILES))
    end
end

local function ensure_fidelityfx_sdk()
    if not is_windows() then
        ui.item_skip("AMD FidelityFX SDK", "Windows only")
        return
    end

    local function ensure_runtime(label, version, url, files)
        local checked    = 0
        local downloaded = 0
        for _, entry in ipairs(files) do
            local destination = path.join(LIBRARIES_DIR, entry.name)
            if needs_vendor_file(entry.name) then
                checked = checked + 1
                if compute_sha256(destination) ~= entry.sha256 then
                    local temporary = destination .. ".tmp"
                    local result, code = download_with_progress(url .. entry.name, temporary, entry.name)
                    if result ~= "OK" or compute_sha256(temporary) ~= entry.sha256 then
                        os.remove(temporary)
                        error(string.format("fidelityfx download or checksum failed: %s (http %s)", tostring(result), tostring(code)))
                    end
                    copy_file(temporary, destination)
                    os.remove(temporary)
                    downloaded = downloaded + 1
                end
            end
        end
        local noun = checked == 1 and "file" or "files"
        if downloaded > 0 then
            ui.item_ok(label .. " " .. version, string.format("%d/%d %s updated", downloaded, checked, noun))
        else
            ui.item_cached(label .. " " .. version, string.format("%d %s verified", checked, noun))
        end
    end
    ensure_runtime("AMD FidelityFX FG", FIDELITYFX_VERSION, FIDELITYFX_URL, FIDELITYFX_FILES)
    ensure_runtime("AMD FSR Upscaler", FSR_UPSCALING_VERSION, FSR_UPSCALING_URL, FSR_UPSCALING_FILES)
end

local function ensure_steamworks()
    if file_exists(STEAM_DLL) and file_exists(STEAM_LIB) then
        ui.item_cached("Steamworks SDK", "win64 lib + dll ready")
        return
    end

    os.mkdir(path.getdirectory(STEAMWORKS_ZIP))

    local result, code = download_with_progress(STEAMWORKS_URL, STEAMWORKS_ZIP, "Steamworks SDK")

    if result ~= "OK" then
        ui.item_warn("Steamworks SDK", string.format("optional download failed (%s, http %s)", tostring(result), tostring(code)))
        return
    end

    local extract_root = path.join(PROJECT_ROOT, "third_party", "steamworks_extract")
    if os.isdir(extract_root) then
        os.rmdir(extract_root)
    end

    local ok, err = pcall(extract_zip, STEAMWORKS_ZIP, extract_root, "Steamworks archive")
    if not ok then
        ui.item_warn("Steamworks SDK", "extraction failed: " .. tostring(err))
        return
    end

    local sdk_root = path.join(extract_root, "SteamworksSDK-main")
    if not os.isdir(sdk_root) then
        ui.item_warn("Steamworks SDK", "unexpected archive layout")
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
        ui.item_ok("Steamworks SDK", "installed")
    else
        ui.item_warn("Steamworks SDK", "install incomplete")
    end
end

-- libraries.7z only holds windows binaries, linux and macos build them with tools/<os>_dependencies.sh
local function run_linux()
    local is_macos     = os.host() == "macosx"
    local library_dir  = is_macos and MACOS_LIBRARIES_DIR or LINUX_LIBRARIES_DIR
    local libraries    = is_macos and MACOS_RUNTIME_LIBRARIES or LINUX_RUNTIME_LIBRARIES
    local dependencies = is_macos and "tools/macos_dependencies.sh" or "tools/linux_dependencies.sh"

    ui.step_begin(1, 2, "Sync Engine Data")
    local t_data = os.clock()
    ui.activity_start("data/", "copying into binaries/data/...")
    copy_dir(DATA_DIR, path.join(BINARIES_DIR, "data"))
    ui.activity_done("ok", "Engine data", "synced into binaries/data/ (" .. ui.format_duration(os.clock() - t_data) .. ")")

    ui.step_begin(2, 2, "Stage Runtime Libraries")
    for _, so in ipairs(libraries) do
        local source = path.join(library_dir, so)
        if file_exists(source) then
            local copied = copy_file(source, path.join(BINARIES_DIR, so))
            if copied then
                ui.item_ok(so, "copied to binaries/")
            else
                ui.item_cached(so, "up to date in binaries/")
            end
        else
            ui.item_warn(so, "missing — run " .. dependencies .. " first")
        end
    end
end

function setup.run()
    local started = os.clock()

    if not is_windows() then
        run_linux()
        setup.elapsed = os.clock() - started
        return
    end

    local is_webgpu   = ARG_API_GRAPHICS == "webgpu"
    local total_steps = is_webgpu and 8 or 7
    local step        = 0

    local function next_step(title)
        step = step + 1
        ui.step_begin(step, total_steps, title)
    end

    next_step("Sync Engine Data")
    local t_data = os.clock()
    ui.activity_start("data/", "copying into binaries/data/...")
    copy_dir(DATA_DIR, path.join(BINARIES_DIR, "data"))
    ui.activity_done("ok", "Engine data", "synced into binaries/data/ (" .. ui.format_duration(os.clock() - t_data) .. ")")

    next_step("Core Libraries Archive")
    ensure_archive()

    next_step("Extract Libraries")
    extract_archive()

    next_step("Upscaling & Frame Generation SDKs")
    if is_webgpu then
        ui.item_skip("XeSS / DLSS / FidelityFX", "not used by WebGPU backend")
    else
        ensure_xess_sdk()
        ensure_dlss_sdk()
        ensure_fidelityfx_sdk()
    end

    next_step("DirectX 12 Agility SDK")
    if ARG_API_GRAPHICS == "vulkan" then
        ui.item_skip("D3D12 Agility SDK " .. AGILITY_VERSION, "not used by Vulkan backend")
    else
        ensure_agility_sdk()
    end

    next_step("Steamworks SDK")
    ensure_steamworks()

    if is_webgpu then
        next_step("Google Dawn (WebGPU)")
        ensure_dawn()
    end

    next_step("Stage Runtime Binaries")
    local needed_dlls = {}
    for _, dll in ipairs(RUNTIME_DLLS) do
        if needs_vendor_file(path.getname(dll)) then
            table.insert(needed_dlls, dll)
        end
    end

    local prog    = ui.create_progress("Vendor runtimes")
    local ready   = 0
    local copied  = 0
    local missing = {}
    for i, dll in ipairs(needed_dlls) do
        local name = path.getname(dll)
        prog:update((i / math.max(#needed_dlls, 1)) * 100, name)
        if file_exists(dll) then
            if copy_file(dll, path.join(RUNTIME_DIR, name)) then
                copied = copied + 1
            end
            ready = ready + 1
        else
            table.insert(missing, name)
        end
    end
    local state_text = copied > 0 and string.format("%d copied, %d up to date", copied, ready - copied) or "up to date"
    prog:finish(string.format("%d/%d dlls ready (%s)", ready, #needed_dlls, state_text), #missing > 0 and "warn" or "ok")
    for _, name in ipairs(missing) do
        ui.item_warn(name, "missing runtime dll")
    end

    if ARG_API_GRAPHICS ~= "vulkan" then
        stage_agility_runtime()
    end

    if file_exists(STEAM_DLL) then
        local steam_copied = copy_file(STEAM_DLL, path.join(RUNTIME_DIR, path.getname(STEAM_DLL)))

        local appid_path = path.join(BINARIES_DIR, "steam_appid.txt")
        if not file_exists(appid_path) then
            local f = io.open(appid_path, "wb")
            f:write(STEAM_APP_ID)
            f:close()
            steam_copied = true
        end
        ui.item_ok("Steamworks runtime", "steam_api64.dll + steam_appid.txt (" .. (steam_copied and "staged" or "up to date") .. ")")
    else
        ui.item_skip("Steamworks runtime", "sdk not found")
    end

    setup.elapsed = os.clock() - started
end

setup.agility_sdk_version = AGILITY_SDK_VERSION
setup.agility_stamp_path  = AGILITY_STAMP
setup.ui                  = ui
setup.elapsed             = 0

return setup
