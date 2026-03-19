/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES ==================
#include <fstream>
#include <vector>
#include <span>
#include <limits>
#include <cstdint>
#include <type_traits>
//===============================

namespace spartan
{
    namespace BinaryIO
    {
        inline bool checked_mul_u64(const uint64_t a, const uint64_t b, uint64_t& out)
        {
            if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a)
                return false;

            out = a * b;
            return true;
        }

        inline bool checked_add_u64(const uint64_t a, const uint64_t b, uint64_t& out)
        {
            if (a > std::numeric_limits<uint64_t>::max() - b)
                return false;

            out = a + b;
            return true;
        }

        struct ByteBudget
        {
            uint64_t used = 0;
            uint64_t limit = 0;

            bool AddBytes(const uint64_t bytes)
            {
                uint64_t next_used = 0;
                if (!checked_add_u64(used, bytes, next_used))
                    return false;

                if (next_used > limit)
                    return false;

                used = next_used;
                return true;
            }

            bool AddArray(const uint64_t count, const uint64_t stride)
            {
                uint64_t bytes = 0;
                return checked_mul_u64(count, stride, bytes) && AddBytes(bytes);
            }
        };

        struct BoundedReader
        {
            explicit BoundedReader(std::ifstream& in_stream, const uint64_t in_remaining)
                : stream(in_stream), remaining(in_remaining)
            {
            }

            bool ReadBytes(void* out_data, const size_t byte_count)
            {
                if (byte_count > remaining)
                    return false;

                stream.read(reinterpret_cast<char*>(out_data), static_cast<std::streamsize>(byte_count));
                if (!stream.good())
                    return false;

                remaining -= byte_count;
                return true;
            }

            std::ifstream& stream;
            uint64_t remaining = 0;
        };

        inline bool try_get_file_size(std::ifstream& stream, uint64_t& file_size)
        {
            stream.seekg(0, std::ios::end);
            const std::streamoff end = stream.tellg();
            if (end < 0)
                return false;

            stream.seekg(0, std::ios::beg);
            file_size = static_cast<uint64_t>(end);
            return true;
        }

        template <typename T>
        bool read_pod(BoundedReader& reader, T& out_value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            return reader.ReadBytes(&out_value, sizeof(T));
        }

        template <typename T>
        bool read_array(BoundedReader& reader, std::vector<T>& out_values, const uint32_t count)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            out_values.resize(count);
            if (count == 0)
                return true;

            return reader.ReadBytes(out_values.data(), static_cast<size_t>(count) * sizeof(T));
        }

        template <typename T>
        bool read_array(BoundedReader& reader, std::span<T> out_values, const uint32_t count)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            if (out_values.size() < count)
                return false;

            if (count == 0)
                return true;

            return reader.ReadBytes(out_values.data(), static_cast<size_t>(count) * sizeof(T));
        }

        template <typename T>
        bool write_pod(std::ofstream& stream, const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            stream.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
            return stream.good();
        }

        template <typename T>
        bool write_array(std::ofstream& stream, const std::vector<T>& values)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            if (values.empty())
                return true;

            stream.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
            return stream.good();
        }

        template <typename T>
        bool write_array(std::ofstream& stream, std::span<T> values)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            if (values.empty())
                return true;

            stream.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
            return stream.good();
        }
    }
}
