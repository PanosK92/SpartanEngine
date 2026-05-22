#pragma once

#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <filesystem>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace pmcp
{
    // JSON-encode a string value (produces "...")
    inline std::string JsonStr(const std::string &s)
    {
        nlohmann::json j = s;
        return j.dump();
    }

    // unescape a JSON string value (handles \\ \/ \" \n \t \r \uXXXX)
    inline std::string JsonUnescape(const std::string &s)
    {
        // nlohmann::json expect a full JSON value.
        // If s is just the raw content of a string (without quotes), wrap it.
        if (s.empty())
            return "";

        std::string wrapped = s;
        if (wrapped.front() != '"')
            wrapped = "\"" + wrapped + "\"";

        try
        {
            return nlohmann::json::parse(wrapped).get<std::string>();
        }
        catch (...)
        {
            return s; // Fallback to raw if parsing fails
        }
    }

    // build a flat JSON object from key/value pairs (values are already JSON-encoded)
    inline std::string JsonObj(const std::vector<std::pair<std::string, std::string>> &kv)
    {
        std::string out = "{";
        bool first = true;
        for (const auto &[k, v] : kv)
        {
            if (!first)
                out += ",";
            out += JsonStr(k) + ":" + v;
            first = false;
        }
        return out + "}";
    }

    // initializer_list overload for convenience
    inline std::string JsonObj(std::initializer_list<std::pair<const char *, std::string>> kv)
    {
        std::vector<std::pair<std::string, std::string>> vec;
        vec.reserve(kv.size());
        for (auto &[k, v] : kv)
            vec.emplace_back(k, v);
        return JsonObj(vec);
    }

    // Parse a tool-call args JSON object once; returns null json on failure
    inline nlohmann::json ParseArgs(const std::string &args)
    {
        try
        {
            return nlohmann::json::parse(args);
        }
        catch (...)
        {
            return {};
        }
    }

    // extract a JSON string value by key from a tool-call args object
    inline std::string ExtractArgStr(const std::string &args, const char *key)
    {
        auto j = ParseArgs(args);
        if (j.contains(key) && j[key].is_string())
            return j[key].get<std::string>();
        return "";
    }

    // extract a JSON integer value by key from a tool-call args object
    inline int64_t ExtractArgInt(const std::string &args, const char *key, int64_t defaultVal = 0)
    {
        auto j = ParseArgs(args);
        if (j.contains(key) && j[key].is_number())
            return j[key].get<int64_t>();
        return defaultVal;
    }

    // extract a JSON string array by key from a tool-call args object
    inline std::vector<std::string> ExtractArgArray(const std::string &args, const char *key)
    {
        auto j = ParseArgs(args);
        if (!j.contains(key) || !j[key].is_array())
            return {};
        std::vector<std::string> result;
        for (const auto &item : j[key])
        {
            if (item.is_string())
                result.push_back(item.get<std::string>());
        }
        return result;
    }

    // extract a JSON number value by key from a tool-call args object
    inline float ExtractArgNum(const std::string &args, const char *key)
    {
        auto j = ParseArgs(args);
        if (j.contains(key) && j[key].is_number())
            return j[key].get<float>();
        return 0.0f;
    }

    // Base64 encode binary data
    inline std::string Base64Encode(const uint8_t *data, size_t len)
    {
        static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        for (size_t i = 0; i < len; i += 3)
        {
            uint32_t n = static_cast<uint32_t>(data[i]) << 16;
            if (i + 1 < len)
                n |= static_cast<uint32_t>(data[i + 1]) << 8;
            if (i + 2 < len)
                n |= static_cast<uint32_t>(data[i + 2]);
            out += table[(n >> 18) & 0x3F];
            out += table[(n >> 12) & 0x3F];
            out += (i + 1 < len) ? table[(n >> 6) & 0x3F] : '=';
            out += (i + 2 < len) ? table[n & 0x3F] : '=';
        }
        return out;
    }

    // Base64 decode string data
    inline std::vector<uint8_t> Base64Decode(const std::string &in)
    {
        static constexpr int table[] = {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
            -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1};

        std::vector<uint8_t> out;
        int val = 0, valb = -8;
        for (uint8_t c : in)
        {
            if (c > 127 || table[c] == -1)
                break;
            val = (val << 6) + table[c];
            valb += 6;
            if (valb >= 0)
            {
                out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }

    // Encode RGBA pixels as an uncompressed PNG.
    // Uses deflate stored blocks (no compression) - simple but valid PNG.
    inline std::vector<uint8_t> EncodeRGBA_PNG(const uint8_t *rgba, int w, int h)
    {
        auto crc32 = [](const uint8_t *data, size_t len) -> uint32_t
        {
            uint32_t c = 0xFFFFFFFF;
            for (size_t i = 0; i < len; i++)
            {
                c ^= data[i];
                for (int j = 0; j < 8; j++)
                {
                    const uint32_t mask = 0u - (c & 1u);
                    c = (c >> 1) ^ (0xEDB88320u & mask);
                }
            }
            return c ^ 0xFFFFFFFF;
        };

        auto put32be = [](std::vector<uint8_t> &v, uint32_t val)
        {
            v.push_back((val >> 24) & 0xFF);
            v.push_back((val >> 16) & 0xFF);
            v.push_back((val >> 8) & 0xFF);
            v.push_back(val & 0xFF);
        };

        auto writeChunk = [&](std::vector<uint8_t> &out, const char *type, const uint8_t *data, size_t len)
        {
            put32be(out, static_cast<uint32_t>(len));
            size_t typeStart = out.size();
            for (int i = 0; i < 4; i++)
                out.push_back(static_cast<uint8_t>(type[i]));
            out.insert(out.end(), data, data + len);
            uint32_t c = crc32(&out[typeStart], 4 + len);
            put32be(out, c);
        };

        std::vector<uint8_t> png;
        // PNG signature
        const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
        png.insert(png.end(), sig, sig + 8);

        // IHDR
        std::vector<uint8_t> ihdr;
        put32be(ihdr, w);
        put32be(ihdr, h);
        ihdr.push_back(8); // bit depth
        ihdr.push_back(6); // color type: RGBA
        ihdr.push_back(0); // compression
        ihdr.push_back(0); // filter
        ihdr.push_back(0); // interlace
        writeChunk(png, "IHDR", ihdr.data(), ihdr.size());

        // IDAT: build raw filtered data (filter byte 0 = None per row)
        size_t rowBytes = w * 4;
        size_t rawSize = h * (1 + rowBytes); // filter byte + pixel data per row
        std::vector<uint8_t> raw(rawSize);
        for (int y = 0; y < h; y++)
        {
            raw[y * (1 + rowBytes)] = 0; // filter: None
            std::memcpy(&raw[y * (1 + rowBytes) + 1], rgba + y * rowBytes, rowBytes);
        }

        // Wrap in zlib: CMF + FLG + stored deflate blocks + Adler32
        // Adler32
        uint32_t a = 1, b = 0;
        for (size_t i = 0; i < raw.size(); i++)
        {
            a = (a + raw[i]) % 65521;
            b = (b + a) % 65521;
        }
        uint32_t adler = (b << 16) | a;

        std::vector<uint8_t> zlib;
        zlib.push_back(0x78); // CMF
        zlib.push_back(0x01); // FLG (no dict, check bits)

        // Split into 65535-byte stored blocks
        size_t offset = 0;
        while (offset < raw.size())
        {
            size_t blockLen = std::min<size_t>(raw.size() - offset, 65535);
            bool last = (offset + blockLen >= raw.size());
            zlib.push_back(last ? 0x01 : 0x00); // BFINAL + BTYPE=00 (stored)
            uint16_t len16 = static_cast<uint16_t>(blockLen);
            uint16_t nlen16 = ~len16;
            zlib.push_back(len16 & 0xFF);
            zlib.push_back((len16 >> 8) & 0xFF);
            zlib.push_back(nlen16 & 0xFF);
            zlib.push_back((nlen16 >> 8) & 0xFF);
            zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + blockLen);
            offset += blockLen;
        }
        put32be(zlib, adler);

        writeChunk(png, "IDAT", zlib.data(), zlib.size());

        // IEND
        writeChunk(png, "IEND", nullptr, 0);

        return png;
    }

    // Strip non-UTF-8 bytes to avoid JSON serialization errors (nlohmann type_error.316).
    // Invalid/truncated multi-byte sequences are dropped silently.
    inline std::string SanitizeUTF8(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size();)
        {
            unsigned char c = static_cast<unsigned char>(s[i]);
            int len;
            if (c < 0x80)
                len = 1;
            else if ((c & 0xE0) == 0xC0)
                len = 2;
            else if ((c & 0xF0) == 0xE0)
                len = 3;
            else if ((c & 0xF8) == 0xF0)
                len = 4;
            else
            {
                ++i;
                continue; // invalid lead byte — drop
            }

            if (i + static_cast<size_t>(len) > s.size())
            {
                ++i;
                continue; // truncated sequence — drop
            }

            bool valid = true;
            for (int j = 1; j < len; ++j)
            {
                if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80)
                {
                    valid = false;
                    break;
                }
            }

            if (valid)
                out.append(s, i, len);
            i += valid ? static_cast<size_t>(len) : 1;
        }
        return out;
    }

    // Returns true if 'path' resolves to a location under 'allowedRoot'.
    // Rejects path traversal attempts (e.g. "../../etc/passwd").
    // Cross-platform: uses std::filesystem::path iteration, works with both / and \.
    inline bool IsPathSafe(const std::string &path, const std::string &allowedRoot)
    {
        std::error_code ec;
        auto canonical = std::filesystem::weakly_canonical(path, ec);
        if (ec)
            return false;
        auto root = std::filesystem::weakly_canonical(allowedRoot, ec);
        if (ec)
            return false;
        // Check that every component of root is a prefix of canonical
        auto rootIt = root.begin();
        auto pathIt = canonical.begin();
        for (; rootIt != root.end(); ++rootIt, ++pathIt)
        {
            if (pathIt == canonical.end() || *pathIt != *rootIt)
                return false;
        }
        return true;
    }
} // namespace pmcp
