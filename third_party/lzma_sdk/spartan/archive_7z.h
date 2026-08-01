// thin in-process 7z api for spartan (lzma sdk, public domain)
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// returns 0 on success
int archive_7z_extract(const char* archive_path, const char* destination_path);
int archive_7z_create(const char* archive_path, const char* const* paths, int path_count);

#ifdef __cplusplus
}
#endif
