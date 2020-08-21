/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES =================
#include "Spartan.h"
#include "FileStream.h"
#include "../RHI/RHI_Vertex.h"
//============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    FileStream::FileStream(const string& path, uint32_t flags)
    {
        m_is_open    = false;
        m_flags        = flags;

        int ios_flags    = ios::binary;
        ios_flags        |= (flags & FileStream_Read)    ? ios::in    : 0;
        ios_flags        |= (flags & FileStream_Write)    ? ios::out    : 0;
        ios_flags        |= (flags & FileStream_Append)    ? ios::app    : 0;

        if (m_flags & FileStream_Write)
        {
            out.open(path, ios_flags);
            if (out.fail())
            {
                LOG_ERROR("Failed to open \"%s\" for writing", path.c_str());
                return;
            }
        }
        else if (m_flags & FileStream_Read)
        {
            in.open(path, ios_flags);
            if(in.fail())
            {
                LOG_ERROR("Failed to open \"%s\" for reading", path.c_str());
                return;
            }
        }

        m_is_open = true;
    }

    FileStream::~FileStream()
    {
        Close();
    }

    void FileStream::Close()
    {
        if (m_flags & FileStream_Write)
        {
            out.flush();
            out.close();
        }
        else if (m_flags & FileStream_Read)
        {
            in.clear();
            in.close();
        }
    }

    void FileStream::Write(const string& value)
    {
        const auto length = static_cast<uint32_t>(value.length());
        Write(length);

        out.write(const_cast<char*>(value.c_str()), length);
    }

    void FileStream::Write(const vector<string>& value)
    {
        const auto size = static_cast<uint32_t>(value.size());
        Write(size);

        for (uint32_t i = 0; i < size; i++)
        {
            Write(value[i]);
        }
    }

    void FileStream::Write(const vector<RHI_Vertex_PosTexNorTan>& value)
    {
        const auto length = static_cast<uint32_t>(value.size());
        Write(length);
        out.write(reinterpret_cast<const char*>(&value[0]), sizeof(RHI_Vertex_PosTexNorTan) * length);
    }

    void FileStream::Write(const vector<uint32_t>& value)
    {
        const auto length = static_cast<uint32_t>(value.size());
        Write(length);
        out.write(reinterpret_cast<const char*>(&value[0]), sizeof(uint32_t) * length);
    }

    void FileStream::Write(const vector<unsigned char>& value)
    {
        const auto size = static_cast<uint32_t>(value.size());
        Write(size);
        out.write(reinterpret_cast<const char*>(&value[0]), sizeof(unsigned char) * size);
    }

    void FileStream::Write(const vector<std::byte>& value)
    {
        const auto size = static_cast<uint32_t>(value.size());
        Write(size);
        out.write(reinterpret_cast<const char*>(&value[0]), sizeof(std::byte) * size);
    }

    void FileStream::Skip(uint32_t n)
    {
        // Set the seek cursor to offset n from the current position
        if (m_flags & FileStream_Write)
        {
            out.seekp(n, ios::cur);
        }
        else if (m_flags & FileStream_Read)
        {
            in.ignore(n, ios::cur);
        }
    }

    void FileStream::Read(string* value)
    {
        uint32_t length = 0;
        Read(&length);

        value->resize(length);
        in.read(const_cast<char*>(value->c_str()), length);
    }

    void FileStream::Read(vector<string>* vec)
    {
        if (!vec)
            return;

        vec->clear();
        vec->shrink_to_fit();

        uint32_t size = 0;
        Read(&size);

        string str;
        for (uint32_t i = 0; i < size; i++)
        {
            Read(&str);
            vec->emplace_back(str);
        }
    }

    void FileStream::Read(vector<RHI_Vertex_PosTexNorTan>* vec)
    {
        if (!vec)
            return;

        vec->clear();
        vec->shrink_to_fit();

        const auto length = ReadAs<uint32_t>();

        vec->reserve(length);
        vec->resize(length);

        in.read(reinterpret_cast<char*>(vec->data()), sizeof(RHI_Vertex_PosTexNorTan) * length);
    }

    void FileStream::Read(vector<uint32_t>* vec)
    {
        if (!vec)
            return;

        vec->clear();
        vec->shrink_to_fit();

        const auto length = ReadAs<uint32_t>();

        vec->reserve(length);
        vec->resize(length);

        in.read(reinterpret_cast<char*>(vec->data()), sizeof(uint32_t) * length);
    }

    void FileStream::Read(vector<unsigned char>* vec)
    {
        if (!vec)
            return;

        vec->clear();
        vec->shrink_to_fit();

        const auto length = ReadAs<uint32_t>();

        vec->reserve(length);
        vec->resize(length);

        in.read(reinterpret_cast<char*>(vec->data()), sizeof(unsigned char) * length);
    }

    void FileStream::Read(vector<std::byte>* vec)
    {
        if (!vec)
            return;

        vec->clear();
        vec->shrink_to_fit();

        const auto length = ReadAs<uint32_t>();

        vec->reserve(length);
        vec->resize(length);

        in.read(reinterpret_cast<char*>(vec->data()), sizeof(std::byte) * length);
    }
}
