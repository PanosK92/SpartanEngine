// thin in-process 7z api for spartan (lzma sdk, public domain)
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// returns 0 on success
// on failure error_out, when given, receives a one line reason of at most error_capacity chars (the file and the system error)
int archive_7z_extract(const char* archive_path, const char* destination_path, char* error_out, int error_capacity);
int archive_7z_create(const char* archive_path, const char* const* paths, int path_count);
// writes the lowercase hex sha256 of a file into hex_out, which must hold at least 65 chars
int archive_7z_sha256_file(const char* path, char* hex_out);

#ifdef __cplusplus
}
#endif
