/*
Copyright(c) 2016-2017 Panos Karabelas

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
#include "StreamIO.h"
#include "../Core/GameObject.h"
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../Math/Quaternion.h"
#include "../Logging/Log.h"
#include "../Graphics/Vertex.h"
#include <iostream>
//=============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	StreamIO::StreamIO(const string& path, SreamIOMode mode)
	{
		m_created = false;
		m_mode = mode;

		if (mode == Mode_Write)
		{
			out.open(path, ios::out | ios::binary);
			if (out.fail())
			{
				LOG_ERROR("StreamIO: Failed to open \"" + path + "\" for writing.");
				return;
			}
		}
		else if (mode == Mode_Read)
		{
			in.open(path, ios::in | ios::binary);
			if(in.fail())
			{
				LOG_ERROR("StreamIO: Failed to open \"" + path + "\" for reading.");
				return;
			}
		}

		m_created = true;
	}

	StreamIO::~StreamIO()
	{
		if (m_mode == Mode_Write)
		{
			out.flush();
			out.close();
		}
		else if (m_mode == Mode_Read)
		{
			in.clear();
			in.close();
		}
	}

	void StreamIO::Write(const string& value)
	{
		unsigned int length = value.length();
		Write(length);

		out.write(const_cast<char*>(value.c_str()), length);
	}

	void StreamIO::Write(const vector<string>& value)
	{
		unsigned int size = value.size();
		Write(size);

		for (unsigned int i = 0; i < size; i++)
		{
			Write(value[i]);
		}
	}

	void StreamIO::Write(const Vector2& value)
	{
		out.write(reinterpret_cast<const char*>(&value), sizeof(Vector2));
	}

	void StreamIO::Write(const Vector3& value)
	{
		out.write(reinterpret_cast<const char*>(&value), sizeof(Vector3));
	}

	void StreamIO::Write(const Vector4& value)
	{
		out.write(reinterpret_cast<const char*>(&value), sizeof(Vector4));
	}

	void StreamIO::Write(const Quaternion& value)
	{
		out.write(reinterpret_cast<const char*>(&value), sizeof(Quaternion));
	}

	void StreamIO::Write(const vector<VertexPosTexTBN>& value)
	{
		unsigned int length = value.size();
		Write(length);
		out.write(reinterpret_cast<const char*>(&value[0]), sizeof(VertexPosTexTBN) * length);
	}

	void StreamIO::Write(const vector<unsigned int>& value)
	{
		unsigned int length = value.size();
		Write(length);
		out.write(reinterpret_cast<const char*>(&value[0]), sizeof(unsigned int) * length);
	}

	void StreamIO::Write(const vector<unsigned char>& value)
	{
		unsigned int size = value.size();
		Write(size);
		out.write(reinterpret_cast<const char*>(&value[0]), sizeof(unsigned char) * size);
	}

	void StreamIO::Read(string* value)
	{
		unsigned int length = 0;
		Read(&length);

		value->resize(length);
		in.read(const_cast<char*>(value->c_str()), length);
	}

	void StreamIO::Read(Vector2* value)
	{
		in.read(reinterpret_cast<char*>(value), sizeof(Vector2));
	}

	void StreamIO::Read(Vector3* value)
	{
		in.read(reinterpret_cast<char*>(value), sizeof(Vector3));
	}

	void StreamIO::Read(Vector4* value)
	{
		in.read(reinterpret_cast<char*>(value), sizeof(Vector4));
	}

	void StreamIO::Read(Quaternion* value)
	{
		in.read(reinterpret_cast<char*>(value), sizeof(Quaternion));
	}

	void StreamIO::Read(vector<string>* value)
	{
		value->clear();
		value->shrink_to_fit();

		unsigned int size = 0;
		Read(&size);

		string str;
		for (unsigned int i = 0; i < size; i++)
		{
			Read(&str);
			value->emplace_back(str);
		}
	}

	void StreamIO::Read(vector<VertexPosTexTBN>* value)
	{
		value->clear();
		value->shrink_to_fit();

		unsigned int length = ReadUInt();

		value->reserve(length);
		value->resize(length);

		in.read(reinterpret_cast<char*>(value->data()), sizeof(VertexPosTexTBN) * length);
	}

	void StreamIO::Read(vector<unsigned int>* value)
	{
		value->clear();
		value->shrink_to_fit();

		unsigned int length = ReadUInt();

		value->reserve(length);
		value->resize(length);

		in.read(reinterpret_cast<char*>(value->data()), sizeof(unsigned int) * length);
	}

	void StreamIO::Read(vector<unsigned char>* value)
	{
		value->clear();
		value->shrink_to_fit();

		unsigned int length = ReadUInt();

		value->reserve(length);
		value->resize(length);

		in.read(reinterpret_cast<char*>(value->data()), sizeof(unsigned char) * length);
	}
}
