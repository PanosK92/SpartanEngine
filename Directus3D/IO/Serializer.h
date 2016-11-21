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

#pragma once

//= INCLUDES ==================
#include <vector>
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../Math/Quaternion.h"
//=============================

class GameObject;

class Serializer
{
public:
	//= STREAMS ======================================
	static bool StartWriting(const std::string& path);
	static void StopWriting();
	static bool StartReading(const std::string& path);
	static void StopReading();
	//================================================
	
	//= SAVING =========================================================
	static void WriteBool(bool value);
	static void WriteSTR(std::string value);
	static void WriteInt(int value);
	static void WriteFloat(float value);
	static void WriteVectorSTR(std::vector<std::string>& vector);
	static void WriteVector2(Directus::Math::Vector2& vector);
	static void WriteVector3(Directus::Math::Vector3& vector);
	static void WriteVector4(Directus::Math::Vector4& vector);
	static void WriteQuaternion(Directus::Math::Quaternion& quaternion);
	//==================================================================

	//= READING ========================================
	static bool ReadBool();
	static std::string ReadSTR();
	static unsigned int ReadUINT();
	static int ReadInt();
	static float ReadFloat();
	static std::vector<std::string> ReadVectorSTR();
	static Directus::Math::Vector2 ReadVector2();
	static Directus::Math::Vector3 ReadVector3();
	static Directus::Math::Vector4 ReadVector4();
	static Directus::Math::Quaternion ReadQuaternion();
	//=================================================
};
