# Spartan in-process 7z

Uses [LZMA SDK](https://www.7-zip.org/sdk.html) (public domain) statically linked into the engine.

- `archive_7z_extract` — Format7zR decoder via `CreateObject`, streams files to disk under a 1 GiB decoder memory cap and reports why it failed. Not the ANSI-C decoder (`SzArEx_Extract`): that one holds a whole solid block in memory, 7.9 GiB for `project.7z`
- `archive_7z_create` — Format7zR encoder via `CreateObject`
- `archive_7z_sha256_file` — SHA-256 of a file, so verifying a download needs no `certutil` / `sha256sum`

Runtime needs no `7z.exe` / `7z.dll`. Setup may fetch a portable `7zr.exe` into `third_party/lzma_sdk/bin/` only to unpack `libraries.7z`.
