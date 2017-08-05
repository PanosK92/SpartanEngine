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
#include <memory>
#include <vector>
#include <string>
//===============

//= FORWARD DECLARATIONS =============================
namespace pugi { class xml_document; class xml_node; }
//====================================================

namespace Directus
{
	class XmlDocument
	{
	public:
		static void Create();
		static void Release();

		//= NODES =======================================================================
		static void AddNode(const std::string& name);
		static bool AddChildNode(const std::string& parentName, const std::string& name);
		//===============================================================================

		//= ATTRIBUTES =========================================================================================
		static bool AddAttribute(const std::string& nodeName, const char* tag, const char* value);
		static bool AddAttribute(const std::string& nodeName, const std::string& tag, const std::string& value);
		static bool AddAttribute(const std::string& nodeName, const std::string& tag, bool value);
		static bool AddAttribute(const std::string& nodeName, const std::string& tag, int value);
		static bool AddAttribute(const std::string& nodeName, const std::string& tag, float value);
		static bool AddAttribute(const std::string& nodeName, const std::string& tag, double value);
		//======================================================================================================

		//= IO =======================================
		static bool Load(const std::string& filePath);
		static bool Save(const std::string& filePath);
		//============================================

	private:
		static std::shared_ptr<pugi::xml_node> GetNodeByName(const std::string& name);

		static std::unique_ptr<pugi::xml_document> m_document;
		static std::vector<std::shared_ptr<pugi::xml_node>> m_nodes;
	};
}
