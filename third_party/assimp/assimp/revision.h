#ifndef ASSIMP_REVISION_H_INC
#define ASSIMP_REVISION_H_INC

#define GitVersion 0x0
#define GitBranch ""

#define VER_MAJOR 5
#define VER_MINOR 4
#define VER_PATCH 3
#define VER_BUILD 0

#define STR_HELP(x) #x
#define STR(x) STR_HELP(x)

#define VER_FILEVERSION             VER_MAJOR,VER_MINOR,VER_PATCH,VER_BUILD
#if (GitVersion == 0)
#define VER_FILEVERSION_STR         STR(VER_MAJOR) "." STR(VER_MINOR) "." STR(VER_PATCH) "." STR(VER_BUILD)
#else
#define VER_FILEVERSION_STR         STR(VER_MAJOR) "." STR(VER_MINOR) "." STR(VER_PATCH) "." STR(VER_BUILD) " (Commit 0)"
#endif
#define VER_COPYRIGHT_STR           "\xA9 2006-2023"

#ifdef  NDEBUG
#define VER_ORIGINAL_FILENAME_STR   "assimp.dll"
#else
#define VER_ORIGINAL_FILENAME_STR   "assimpd.dll"
#endif //  NDEBUG

#endif // ASSIMP_REVISION_H_INC
