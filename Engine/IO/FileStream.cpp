/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ===================
#include "FileStream.h"
#include <iostream>
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../Math/Quaternion.h"
#include "../Math/BoundingBox.h"
#include "../World/Entity.h"
#include "../Logging/Log.h"
#include "../RHI/RHI_Vertex.h"
//==============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	FileStream::FileStream(const string& path, FileStreamMode mode)
	{
		m_isOpen = false;
		m_mode = mode;

		if (mode == FileStreamMode_Write)
		{
			out.open(path, ios::out | ios::binary);
			if (out.fail())
			{
				LOGF_ERROR("Failed to open \"%s\" for writing", path.c_str());
				return;
			}
		}
		else if (mode == FileStreamMode_Read)
		{
			in.open(path, ios::in | ios::binary);
			if(in.fail())
			{
				LOGF_ERROR("Failed to open \"%s\" for reading", path.c_str());
				return;
			}
		}

		m_isOpen = true;
	}

	FileStream::~FileStream()
	{
		if (m_mode == FileStreamMode_Write)
		{
			out.flush();
			out.close();
		}
		else if (m_mode == FileStreamMode_Read)
		{
			in.clear();
			in.close();
		}
	}

	void FileStream::Write(const string& value)
	{
		auto length = (unsigned int)value.length();
		Write(length);

		out.write(const_cast<char*>(value.c_str()), length);
	}

	void FileStream::Write(const vector<string>& value)
	{
		auto size = (unsigned int)value.size();
		Write(size);

		for (unsigned int i = 0; i < size; i++)
		{
			Write(value[i]);
		}
	}

	void FileStream::Write(const Vector2& value)
	{
		out.write(reinterpret_cast<const char*>(&value), sizeof(Vector2));
	}

	void FileStream::Write(const Vector3& value)
	{
		out.write(reinterpret_cast<const char*>(&value), sizeof(Vector3));
	}

	void FileStream::Write(const Vector4& value)
	{
		out.write(reinterpret_cast<const char*>(&value), sizeof(Vector4));
	}

	void FileStream::Write(const Quaternion& value)
	{
		out.write(reinterpret_cast<const char*>(&value), sizeof(Quaternion));
	}

	void FileStream::Write(const BoundingBox& value)
	{
		out.write(reinterpret_cast<const char*>(&value), sizeof(BoundingBox));
	}

	void FileStream::Write(const vector<RHI_Vertex_PosUvNorTan>& value)
	{
		auto length = (unsigned int)value.size();
		Write(length);
		out.write(reinterpret_cast<const char*>(&value[0]), sizeof(RHI_Vertex_PosUvNorTan) * length);
	}

	void FileStream::Write(const vector<unsigned int>& value)
	{
		auto length = (unsigned int)value.size();
		Write(length);
		out.write(reinterpret_cast<const char*>(&value[0]), sizeof(unsigned int) * length);
	}

	void FileStream::Write(const vector<unsigned char>& value)
	{
		auto size = (unsigned int)value.size();
		Write(size);
		out.write(reinterpret_cast<const char*>(&value[0]), sizeof(unsigned char) * size);
	}

	void FileStream::Write(const vector<std::byte>& value)
	{
		auto size = (unsigned int)value.size();
		Write(size);
		out.write(reinterpret_cast<const char*>(&value[0]), sizeof(std::byte) * size);
	}

	void FileStream::Read(string* value)
	{
		unsigned int length = 0;
		Read(&length);

		value->resize(length);
		in.read(const_cast<char*>(value->c_str()), length);
	}

	void FileStream::Read(Vector2* value)
	{
		in.read(reinterpret_cast<char*>(value), sizeof(Vector2));
	}

	void FileStream::Read(Vector3* value)
	{
		in.read(reinterpret_cast<char*>(value), sizeof(Vector3));
	}

	void FileStream::Read(Vector4* value)
	{
		in.read(reinterpret_cast<char*>(value), sizeof(Vector4));
	}

	void FileStream::Read(Quaternion* value)
	{
		in.read(reinterpret_cast<char*>(value), sizeof(Quaternion));
	}

	void FileStream::Read(BoundingBox* value)
	{
		in.read(reinterpret_cast<char*>(value), sizeof(BoundingBox));
	}

	void FileStream::Read(vector<string>* vec)
	{
		if (!vec)
			return;

		vec->clear();
		vec->shrink_to_fit();

		unsigned int size = 0;
		Read(&size);

		string str;
		for (unsigned int i = 0; i < size; i++)
		{
			Read(&str);
			vec->emplace_back(str);
		}
	}

	void FileStream::Read(vector<RHI_Vertex_PosUvNorTan>* vec)
	{
		if (!vec)
			return;

		vec->clear();
		vec->shrink_to_fit();

		unsigned int length = ReadUInt();

		vec->reserve(length);
		vec->resize(length);

		in.read(reinterpret_cast<char*>(vec->data()), sizeof(RHI_Vertex_PosUvNorTan) * length);
	}

	void FileStream::Read(vector<unsigned int>* vec)
	{
		if (!vec)
			return;

		vec->clear();
		vec->shrink_to_fit();

		unsigned int length = ReadUInt();

		vec->reserve(length);
		vec->resize(length);

		in.read(reinterpret_cast<char*>(vec->data()), sizeof(unsigned int) * length);
	}

	void FileStream::Read(vector<unsigned char>* vec)
	{
		if (!vec)
			return;

		vec->clear();
		vec->shrink_to_fit();

		unsigned int length = ReadUInt();

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

		unsigned int length = ReadUInt();

		vec->reserve(length);
		vec->resize(length);

		in.read(reinterpret_cast<char*>(vec->data()), sizeof(std::byte) * length);
	}
}
