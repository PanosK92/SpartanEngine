/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ===========
#include "Serializer.h"
#include "Log.h"
#include <fstream>
//======================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

std::ofstream out;
std::ifstream in;

void Serializer::StartWriting(std::string path)
{
	out.open(path, std::ios::out | std::ios::binary);
}

void Serializer::StopWriting()
{
	out.flush();
	out.close();
}

void Serializer::StartReading(std::string path)
{
	in.open(path, std::ios::in | std::ios::binary);

	if (in.fail())
	LOG("Can't open " + path, Log::Error);
}

void Serializer::StopReading()
{
	in.clear();
	in.close();
}

void Serializer::SaveBool(bool value)
{
	out.write(reinterpret_cast<char*>(&value), sizeof(value));
}

void Serializer::SaveSTR(std::string value)
{
	int stringSize = value.size();
	out.write(reinterpret_cast<char*>(&stringSize), sizeof(stringSize));
	out.write(const_cast<char*>(value.c_str()), stringSize);
}

void Serializer::SaveInt(int value)
{
	out.write(reinterpret_cast<char*>(&value), sizeof(value));
}

void Serializer::SaveFloat(float value)
{
	out.write(reinterpret_cast<char*>(&value), sizeof(value));
}

void Serializer::SaveVectorSTR(std::vector<std::string> vector)
{
	SaveInt((int)vector.size());
	for (auto i = 0; i < vector.size(); i++)
		SaveSTR(vector[i]);
}

void Serializer::SaveVector2(Vector2 vector)
{
	out.write(reinterpret_cast<char*>(&vector.x), sizeof(vector.x));
	out.write(reinterpret_cast<char*>(&vector.y), sizeof(vector.y));
}

void Serializer::SaveVector3(Vector3 vector)
{
	out.write(reinterpret_cast<char*>(&vector.x), sizeof(vector.x));
	out.write(reinterpret_cast<char*>(&vector.y), sizeof(vector.y));
	out.write(reinterpret_cast<char*>(&vector.z), sizeof(vector.z));
}

void Serializer::SaveVector4(Vector4 vector)
{
	out.write(reinterpret_cast<char*>(&vector.x), sizeof(vector.x));
	out.write(reinterpret_cast<char*>(&vector.y), sizeof(vector.y));
	out.write(reinterpret_cast<char*>(&vector.z), sizeof(vector.z));
	out.write(reinterpret_cast<char*>(&vector.w), sizeof(vector.w));
}

void Serializer::SaveQuaternion(Quaternion quaternion)
{
	out.write(reinterpret_cast<char*>(&quaternion.x), sizeof(quaternion.x));
	out.write(reinterpret_cast<char*>(&quaternion.y), sizeof(quaternion.y));
	out.write(reinterpret_cast<char*>(&quaternion.z), sizeof(quaternion.z));
	out.write(reinterpret_cast<char*>(&quaternion.w), sizeof(quaternion.w));
}

bool Serializer::LoadBool()
{
	bool value;
	in.read(reinterpret_cast<char*>(&value), sizeof(value));

	return value;
}

std::string Serializer::LoadSTR()
{
	int stringSize;
	in.read(reinterpret_cast<char*>(&stringSize), sizeof(stringSize));

	std::string value;
	value.resize(stringSize);
	in.read(const_cast<char*>(value.c_str()), stringSize);

	return value;
}

unsigned int Serializer::LoadUINT()
{
	unsigned int value;
	in.read(reinterpret_cast<char*>(&value), sizeof(value));

	return value;
}

int Serializer::LoadInt()
{
	int value;
	in.read(reinterpret_cast<char*>(&value), sizeof(value));

	return value;
}

float Serializer::LoadFloat()
{
	float value;
	in.read(reinterpret_cast<char*>(&value), sizeof(value));

	return value;
}

std::vector<std::string> Serializer::LoadVectorSTR()
{
	std::vector<std::string> vector;

	int textureIDsCount = LoadInt();
	for (int i = 0; i < textureIDsCount; i++)
		vector.push_back(LoadSTR());

	return vector;
}

Vector2 Serializer::LoadVector2()
{
	Vector2 vector;
	in.read(reinterpret_cast<char*>(&vector.x), sizeof(vector.x));
	in.read(reinterpret_cast<char*>(&vector.y), sizeof(vector.y));

	return vector;
}

Vector3 Serializer::LoadVector3()
{
	Vector3 vector;
	in.read(reinterpret_cast<char*>(&vector.x), sizeof(vector.x));
	in.read(reinterpret_cast<char*>(&vector.y), sizeof(vector.y));
	in.read(reinterpret_cast<char*>(&vector.z), sizeof(vector.z));

	return vector;
}

Vector4 Serializer::LoadVector4()
{
	Vector4 vector;
	in.read(reinterpret_cast<char*>(&vector.x), sizeof(vector.x));
	in.read(reinterpret_cast<char*>(&vector.y), sizeof(vector.y));
	in.read(reinterpret_cast<char*>(&vector.z), sizeof(vector.z));
	in.read(reinterpret_cast<char*>(&vector.w), sizeof(vector.w));

	return vector;
}

Quaternion Serializer::LoadQuaternion()
{
	Quaternion quaternion = Quaternion::Identity();
	in.read(reinterpret_cast<char*>(&quaternion.x), sizeof(quaternion.x));
	in.read(reinterpret_cast<char*>(&quaternion.y), sizeof(quaternion.y));
	in.read(reinterpret_cast<char*>(&quaternion.z), sizeof(quaternion.z));
	in.read(reinterpret_cast<char*>(&quaternion.w), sizeof(quaternion.w));

	return quaternion;
}
