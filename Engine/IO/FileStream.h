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

#pragma once

//= INCLUDES =====
#include <vector>
#include <fstream>
//================

namespace Directus
{
	class Entity;
	struct RHI_Vertex_PosUvNorTan;
	namespace Math
	{
		class Vector2;
		class Vector3;
		class Vector4;
		class Quaternion;
		class BoundingBox;
	}

	enum FileStreamMode
	{
		FileStreamMode_Read,
		FileStreamMode_Write
	};

	class FileStream
	{
	public:
		FileStream(const std::string& path, FileStreamMode mode);
		~FileStream();

		bool IsOpen() { return m_isOpen; }

		//= WRITING ==================================================
		template <class T, class = typename std::enable_if<
			std::is_same<T, bool>::value ||
			std::is_same<T, unsigned char>::value ||
			std::is_same<T, std::byte>::value ||
			std::is_same<T, int>::value ||
			std::is_same<T, long>::value ||
			std::is_same<T, long long>::value ||
			std::is_same<T, unsigned>::value ||
			std::is_same<T, unsigned long>::value ||
			std::is_same<T, unsigned long long>::value ||
			std::is_same<T, float>::value ||
			std::is_same<T, double>::value ||
			std::is_same<T, long double>::value
		>::type>
		void Write(T value)
		{
			out.write(reinterpret_cast<char*>(&value), sizeof(value));
		}

		void Write(const std::string& value);
		void Write(const Math::Vector2& value);
		void Write(const Math::Vector3& value);
		void Write(const Math::Vector4& value);	
		void Write(const Math::Quaternion& value);
		void Write(const Math::BoundingBox& value);
		void Write(const std::vector<std::string>& value);
		void Write(const std::vector<RHI_Vertex_PosUvNorTan>& value);
		void Write(const std::vector<unsigned int>& value);
		void Write(const std::vector<unsigned char>& value);
		void Write(const std::vector<std::byte>& value);
		//===========================================================
		
		//= READING ================================================
		template <class T, class = typename std::enable_if<
			std::is_same<T, bool>::value ||
			std::is_same<T, unsigned char>::value ||
			std::is_same<T, std::byte>::value ||
			std::is_same<T, int>::value ||
			std::is_same<T, long>::value ||
			std::is_same<T, long long>::value ||
			std::is_same<T, unsigned>::value ||
			std::is_same<T, unsigned long>::value ||
			std::is_same<T, unsigned long long>::value ||
			std::is_same<T, float>::value ||
			std::is_same<T, double>::value ||
			std::is_same<T, long double>::value
		>::type>
			void Read(T* value)
		{
			in.read(reinterpret_cast<char*>(value), sizeof(T));
		}

		void Read(std::string* value);	
		void Read(Math::Vector2* value);
		void Read(Math::Vector3* value);
		void Read(Math::Vector4* value);
		void Read(Math::Quaternion* value);
		void Read(Math::BoundingBox* value);
		void Read(std::vector<std::string>* vec);
		void Read(std::vector<RHI_Vertex_PosUvNorTan>* vec);
		void Read(std::vector<unsigned int>* vec);
		void Read(std::vector<unsigned char>* vec);
		void Read(std::vector<std::byte>* vec);

		// Helps when reading enums
		int ReadInt()
		{
			int value = 0;
			Read(&value);
			return value;
		}

		unsigned int ReadUInt()
		{
			unsigned int value = 0;
			Read(&value);
			return value;
		}
		//==========================================================

	private:
		std::ofstream out;
		std::ifstream in;
		FileStreamMode m_mode;
		bool m_isOpen;
	};
}
