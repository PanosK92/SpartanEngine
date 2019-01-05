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

//= INCLUDES =============
#include "GUIDGenerator.h"
#include <iomanip>
#include <sstream> 
#include <guiddef.h>
#include "objbase.h"
#include <winerror.h>
//========================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	unsigned int GUIDGenerator::Generate()
	{
		hash<string> hasher;
		return (unsigned int)hasher(GenerateAsStr());
	}

	string GUIDGenerator::GenerateAsStr()
	{
		string guidString = "N/A";
		GUID guid;
		HRESULT hr = CoCreateGuid(&guid);
		if (SUCCEEDED(hr))
		{
			stringstream stream;
			stream << hex << uppercase
				<< setw(8) << setfill('0') << guid.Data1
				<< "-" << setw(4) << setfill('0') << guid.Data2
				<< "-" << setw(4) << setfill('0') << guid.Data3
				<< "-";

			for (unsigned int i = 0; i < sizeof(guid.Data4); ++i)
			{
				if (i == 2)
					stream << "-";
				stream << hex << setw(2) << setfill('0') << int(guid.Data4[i]);
			}
			guidString = stream.str();
		}

		return guidString;
	}

	string GUIDGenerator::ToStr(unsigned int guid)
	{
		return to_string(guid);
	}

	unsigned int GUIDGenerator::ToUnsignedInt(const string& guid)
	{
		stringstream sstream(guid);
		unsigned int guidSizeT;
		sstream >> guidSizeT;

		return guidSizeT;
	}
}
