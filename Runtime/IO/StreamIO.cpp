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
#include <iterator>
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

	void StreamIO::Write(const Vector2& vector)
	{
		Write(vector.x);
		Write(vector.y);
	}

	void StreamIO::Write(const Vector3& vector)
	{
		Write(vector.x);
		Write(vector.y);
		Write(vector.z);
	}

	void StreamIO::Write(const Vector4& vector)
	{
		Write(vector.x);
		Write(vector.y);
		Write(vector.z);
		Write(vector.w);
	}

	void StreamIO::Write(const Quaternion& quaternion)
	{
		Write(quaternion.x);
		Write(quaternion.y);
		Write(quaternion.z);
		Write(quaternion.w);
	}

	void StreamIO::Write(const VertexPosTexTBN& value)
	{
		Write(value.position.x);
		Write(value.position.y);
		Write(value.position.z);

		Write(value.uv.x);
		Write(value.uv.y);

		Write(value.normal.x);
		Write(value.normal.y);
		Write(value.normal.z);

		Write(value.tangent.x);
		Write(value.tangent.y);
		Write(value.tangent.z);

		Write(value.bitangent.x);
		Write(value.bitangent.y);
		Write(value.bitangent.z);
	}

	void StreamIO::Write(const vector<unsigned char>& value)
	{
		unsigned int size = value.size();
		Write(size);
		out.write(reinterpret_cast<const char*>(&value[0]), sizeof(value[0]) * size);
	}

	void StreamIO::Read(string& value)
	{
		unsigned int length = 0;
		Read(length);

		value.resize(length);
		in.read(const_cast<char*>(value.c_str()), length);
	}

	void StreamIO::Read(vector<string>& value)
	{
		unsigned int size = 0;
		Read(size);

		string str;
		for (unsigned int i = 0; i < size; i++)
		{
			Read(str);
			value.emplace_back(str);
		}
	}

	void StreamIO::Read(Vector2& value)
	{
		Read(value.x);
		Read(value.y);
	}

	void StreamIO::Read(Vector3& value)
	{
		Read(value.x);
		Read(value.y);
		Read(value.z);
	}

	void StreamIO::Read(Vector4& value)
	{
		Read(value.x);
		Read(value.y);
		Read(value.z);
		Read(value.w);
	}

	void StreamIO::Read(Quaternion& value)
	{
		Read(value.x);
		Read(value.y);
		Read(value.z);
		Read(value.w);
	}

	void StreamIO::Read(VertexPosTexTBN& value)
	{
		Read(value.position.x);
		Read(value.position.y);
		Read(value.position.z);

		Read(value.uv.x);
		Read(value.uv.y);

		Read(value.normal.x);
		Read(value.normal.y);
		Read(value.normal.z);

		Read(value.tangent.x);
		Read(value.tangent.y);
		Read(value.tangent.z);

		Read(value.bitangent.x);
		Read(value.bitangent.y);
		Read(value.bitangent.z);
	}

	void StreamIO::Read(vector<unsigned char>& value)
	{
		unsigned int size = ReadUInt();
		value.reserve(size);
		in.read(reinterpret_cast<char*>(&value[0]), sizeof(value[0]) * size);
	}
}
