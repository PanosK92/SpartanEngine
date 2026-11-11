// in-process sha256 of a file via lzma sdk, hardware accelerated where the cpu supports it
#include "archive_7z.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
#include "../C/Sha256.h"
}

namespace
{
    const size_t k_read_buf_size = (size_t)1 << 20;

    bool prepare_sha256()
    {
        Sha256Prepare();
        return true;
    }
}

int archive_7z_sha256_file(const char* path, char* hex_out)
{
    if (!path || !hex_out)
    {
        return 1;
    }
    hex_out[0] = '\0';

    try
    {
        const std::u8string utf8_path(reinterpret_cast<const char8_t*>(path));
        std::ifstream file(std::filesystem::path(utf8_path), std::ios::binary);
        if (!file)
        {
            return 1;
        }

        // picks the fastest implementation once, a function static so concurrent callers are safe
        static const bool prepared = prepare_sha256();
        (void)prepared;

        CSha256 sha;
        Sha256_Init(&sha);

        std::vector<char> buffer(k_read_buf_size);
        while (file)
        {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = file.gcount();
            if (count > 0)
            {
                Sha256_Update(&sha, reinterpret_cast<const Byte*>(buffer.data()), static_cast<size_t>(count));
            }
        }

        // a read error would leave the hash of a truncated file, which must not pass for the real one
        if (!file.eof() || file.bad())
        {
            return 1;
        }

        Byte digest[SHA256_DIGEST_SIZE];
        Sha256_Final(&sha, digest);

        const char* hex_digits = "0123456789abcdef";
        for (size_t i = 0; i < SHA256_DIGEST_SIZE; i++)
        {
            hex_out[i * 2]     = hex_digits[digest[i] >> 4];
            hex_out[i * 2 + 1] = hex_digits[digest[i] & 0x0F];
        }
        hex_out[SHA256_DIGEST_SIZE * 2] = '\0';
        return 0;
    }
    catch (...)
    {
        hex_out[0] = '\0';
        return 1;
    }
}
