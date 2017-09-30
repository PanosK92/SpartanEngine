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

#pragma once

//= INCLUDES ====
#include <vector>
//===============

namespace Directus
{
	class GameObject;
	namespace Math
	{
		class Vector2;
		class Vector3;
		class Vector4;
		class Quaternion;
	}

	class StreamIO
	{
	public:
		//= STREAM =======================================
		static bool StartWriting(const std::string& path);
		static void StopWriting();
		static bool StartReading(const std::string& path);
		static void StopReading();
		//================================================

		//= WRITING =================================================
		static void WriteBool(bool value);
		static void WriteSTR(std::string value);
		static void WriteInt(int value);
		static void WriteUInt(unsigned int value);
		static void WriteULong(unsigned long value);
		static void WriteFloat(float value);
		static void WriteVectorSTR(std::vector<std::string>& vector);
		static void WriteVector2(Math::Vector2& vector);
		static void WriteVector3(Math::Vector3& vector);
		static void WriteVector4(Math::Vector4& vector);
		static void WriteQuaternion(Math::Quaternion& quaternion);
		//===========================================================

		//= READING ====================================
		static bool ReadBool();
		static std::string ReadSTR();
		static int ReadInt();
		static unsigned int ReadUInt();
		static unsigned long ReadULong();
		static float ReadFloat();
		static std::vector<std::string> ReadVectorSTR();
		static Math::Vector2 ReadVector2();
		static Math::Vector3 ReadVector3();
		static Math::Vector4 ReadVector4();
		static Math::Quaternion ReadQuaternion();
		//==============================================
	};
}
