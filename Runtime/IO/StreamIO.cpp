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
#include <fstream>
#include "../Core/GameObject.h"
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../Math/Quaternion.h"
//=============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

//= STREAMS =
ofstream out;
ifstream in;
//===========

namespace Directus
{

	bool StreamIO::StartWriting(const string& path)
	{
		out.open(path, ios::out | ios::binary);
		return !out.fail();
	}

	void StreamIO::StopWriting()
	{
		out.flush();
		out.close();
	}

	bool StreamIO::StartReading(const string& path)
	{
		in.open(path, ios::in | ios::binary);
		return !in.fail();
	}

	void StreamIO::StopReading()
	{
		in.clear();
		in.close();
	}

	void StreamIO::WriteBool(bool value)
	{
		out.write(reinterpret_cast<char*>(&value), sizeof(value));
	}

	void StreamIO::WriteSTR(string value)
	{
		int stringSize = value.size();
		out.write(reinterpret_cast<char*>(&stringSize), sizeof(stringSize));
		out.write(const_cast<char*>(value.c_str()), stringSize);
	}

	void StreamIO::WriteInt(int value)
	{
		out.write(reinterpret_cast<char*>(&value), sizeof(value));
	}

	void StreamIO::WriteUInt(unsigned int value)
	{
		out.write(reinterpret_cast<char*>(&value), sizeof(value));
	}

	void StreamIO::WriteFloat(float value)
	{
		out.write(reinterpret_cast<char*>(&value), sizeof(value));
	}

	void StreamIO::WriteVectorSTR(vector<string>& vector)
	{
		WriteInt(int(vector.size()));
		for (auto i = 0; i < vector.size(); i++)
		{
			WriteSTR(vector[i]);
		}
	}

	void StreamIO::WriteVector2(Vector2& vector)
	{
		out.write(reinterpret_cast<char*>(&vector.x), sizeof(vector.x));
		out.write(reinterpret_cast<char*>(&vector.y), sizeof(vector.y));
	}

	void StreamIO::WriteVector3(Vector3& vector)
	{
		out.write(reinterpret_cast<char*>(&vector.x), sizeof(vector.x));
		out.write(reinterpret_cast<char*>(&vector.y), sizeof(vector.y));
		out.write(reinterpret_cast<char*>(&vector.z), sizeof(vector.z));
	}

	void StreamIO::WriteVector4(Vector4& vector)
	{
		out.write(reinterpret_cast<char*>(&vector.x), sizeof(vector.x));
		out.write(reinterpret_cast<char*>(&vector.y), sizeof(vector.y));
		out.write(reinterpret_cast<char*>(&vector.z), sizeof(vector.z));
		out.write(reinterpret_cast<char*>(&vector.w), sizeof(vector.w));
	}

	void StreamIO::WriteQuaternion(Quaternion& quaternion)
	{
		out.write(reinterpret_cast<char*>(&quaternion.x), sizeof(quaternion.x));
		out.write(reinterpret_cast<char*>(&quaternion.y), sizeof(quaternion.y));
		out.write(reinterpret_cast<char*>(&quaternion.z), sizeof(quaternion.z));
		out.write(reinterpret_cast<char*>(&quaternion.w), sizeof(quaternion.w));
	}

	bool StreamIO::ReadBool()
	{
		bool value;
		in.read(reinterpret_cast<char*>(&value), sizeof(value));

		return value;
	}

	string StreamIO::ReadSTR()
	{
		int stringSize;
		in.read(reinterpret_cast<char*>(&stringSize), sizeof(stringSize));

		string value;
		value.resize(stringSize);
		in.read(const_cast<char*>(value.c_str()), stringSize);

		return value;
	}

	int StreamIO::ReadInt()
	{
		int value;
		in.read(reinterpret_cast<char*>(&value), sizeof(value));

		return value;
	}

	unsigned int StreamIO::ReadUInt()
	{
		unsigned int value;
		in.read(reinterpret_cast<char*>(&value), sizeof(value));

		return value;
	}

	float StreamIO::ReadFloat()
	{
		float value;
		in.read(reinterpret_cast<char*>(&value), sizeof(value));

		return value;
	}

	vector<string> StreamIO::ReadVectorSTR()
	{
		vector<string> vector;

		int textureIDsCount = ReadInt();
		for (int i = 0; i < textureIDsCount; i++)
			vector.push_back(ReadSTR());

		return vector;
	}

	Vector2 StreamIO::ReadVector2()
	{
		Vector2 vector;
		in.read(reinterpret_cast<char*>(&vector.x), sizeof(vector.x));
		in.read(reinterpret_cast<char*>(&vector.y), sizeof(vector.y));

		return vector;
	}

	Vector3 StreamIO::ReadVector3()
	{
		Vector3 vector;
		in.read(reinterpret_cast<char*>(&vector.x), sizeof(vector.x));
		in.read(reinterpret_cast<char*>(&vector.y), sizeof(vector.y));
		in.read(reinterpret_cast<char*>(&vector.z), sizeof(vector.z));

		return vector;
	}

	Vector4 StreamIO::ReadVector4()
	{
		Vector4 vector;
		in.read(reinterpret_cast<char*>(&vector.x), sizeof(vector.x));
		in.read(reinterpret_cast<char*>(&vector.y), sizeof(vector.y));
		in.read(reinterpret_cast<char*>(&vector.z), sizeof(vector.z));
		in.read(reinterpret_cast<char*>(&vector.w), sizeof(vector.w));

		return vector;
	}

	Quaternion StreamIO::ReadQuaternion()
	{
		Quaternion quaternion = Quaternion(0, 0, 0, 1);
		in.read(reinterpret_cast<char*>(&quaternion.x), sizeof(quaternion.x));
		in.read(reinterpret_cast<char*>(&quaternion.y), sizeof(quaternion.y));
		in.read(reinterpret_cast<char*>(&quaternion.z), sizeof(quaternion.z));
		in.read(reinterpret_cast<char*>(&quaternion.w), sizeof(quaternion.w));

		return quaternion;
	}
}
