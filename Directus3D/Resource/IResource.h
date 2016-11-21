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

//= INCLUDES ========================
#include "../Core/Context.h"
#include "../FileSystem/FileSystem.h"
//===================================

namespace Directus
{
	namespace Resource
	{
		class IResource
		{
		public:
			virtual ~IResource() {}

			std::string GetID() { return m_ID; }
			void SetID(const std::string& ID) { m_ID = ID; }

			std::string GetFilePath() { return m_filePath; }
			void SetFilePath(const std::string& filePath) { m_filePath = filePath; }

			// Resource Save/Load
			virtual bool LoadFromFile(const std::string& filePath) = 0;

			// Metadata Save/Load
			virtual bool SaveMetadata() = 0;

		protected:
			Context* m_context;
			std::string m_ID = DATA_NOT_ASSIGNED;
			std::string m_filePath = DATA_NOT_ASSIGNED;
		};
	}
}