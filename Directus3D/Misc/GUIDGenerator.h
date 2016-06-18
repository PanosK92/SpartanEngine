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

//= INCLUDES ========
#include <iomanip>
#include <sstream> 
#include <guiddef.h>
#include "Objbase.h"
#include <winerror.h>
//===================

#define GENERATE_GUID GUIDGenerator::Generate()

class GUIDGenerator
{
public:
	static std::string Generate();
};

inline std::string GUIDGenerator::Generate()
{
	GUID guid;
	std::string guidString;
	HRESULT hr = CoCreateGuid(&guid);
	if (SUCCEEDED(hr))
	{
		std::stringstream stream;
		stream << std::hex << std::uppercase
			<< std::setw(8) << std::setfill('0') << guid.Data1
			<< "-" << std::setw(4) << std::setfill('0') << guid.Data2
			<< "-" << std::setw(4) << std::setfill('0') << guid.Data3
			<< "-";
		for (int i = 0; i < sizeof(guid.Data4); ++i)
		{
			if (i == 2)
				stream << "-";
			stream << std::hex << std::setw(2) << std::setfill('0') << int(guid.Data4[i]);
		}
		guidString = stream.str();
	}

	return guidString;
}
