# Spartan in-process 7z

Uses [LZMA SDK](https://www.7-zip.org/sdk.html) (public domain) statically linked into the engine.

- `archive_7z_extract` — ANSI-C 7z decoder
- `archive_7z_create` — Format7zR encoder via `CreateObject`

Runtime needs no `7z.exe` / `7z.dll`. Setup may fetch a portable `7zr.exe` into `third_party/lzma_sdk/bin/` only to unpack `libraries.7z`.
