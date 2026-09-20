# Recast Navigation

Vendored from [recastnavigation/recastnavigation](https://github.com/recastnavigation/recastnavigation),
tag **v1.6.0**, commit `b4554541b658630816dba41466eb1cefb624519e`.
Only Recast, Detour and DetourCrowd are included. Upstream sources are unmodified.
Licensed under the zlib license; see [License.txt](License.txt).

Windows links the x64 static Recast, Detour and DetourCrowd libraries from `libraries.7z`.
`tools/build_recast.bat` rebuilds them with MSVC: release uses `/O2 /MT /DNDEBUG`, debug uses
`/Od /MTd /Z7 /D_DEBUG` and the `_debug.lib` suffix. Debug symbols are embedded, so no external
PDB files are needed. Development builds use the release libraries. Other platforms continue
to compile these sources directly through `tools/premake.lua`.
