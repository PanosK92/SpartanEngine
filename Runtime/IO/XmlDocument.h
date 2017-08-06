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

//= FORWARD DECLARATIONS =
namespace pugi
{
	class xml_document; 
	class xml_node;
	class xml_attribute;
}
//========================

namespace Directus
{
	//= FORWARD DECLARATIONS =
	namespace Math
	{
		class Vector2;
		class Vector3;
		class Vector4;
	}
	//========================

	class XmlDocument
	{
	public:
		static void Create();
		static void Release();

		//= NODES =======================================================================
		static void AddNode(const std::string& nodeName);
		static bool AddChildNode(const std::string& parentNodeName, const std::string& childNodeName);
		//===============================================================================

		//= ADD ATTRIBUTE ================================================================================================
		static bool AddAttribute(const std::string& nodeName, const char* attributeName, const char* value);
		static bool AddAttribute(const std::string& nodeName, const std::string& attributeName, const std::string& value);
		static bool AddAttribute(const std::string& nodeName, const std::string& attributeName, bool value);
		static bool AddAttribute(const std::string& nodeName, const std::string& attributeName, int value);
		static bool AddAttribute(const std::string& nodeName, const std::string& attributeName, float value);
		static bool AddAttribute(const std::string& nodeName, const std::string& attributeName, double value);
		static bool AddAttribute(const std::string& nodeName, const std::string& attributeName, Math::Vector2& value);
		static bool AddAttribute(const std::string& nodeName, const std::string& attributeName, Math::Vector3& value);
		static bool AddAttribute(const std::string& nodeName, const std::string& attributeName, Math::Vector4& value);
		//================================================================================================================

		//= GET ATTRIBUTE ===================================================================================
		static bool GetAttribute(const std::string& nodeName, const char* attributeName, std::string& value);
		static bool GetAttribute(const std::string& nodeName, const char* attributeName, int& value);
		static bool GetAttribute(const std::string& nodeName, const char* attributeName, bool& value);
		static bool GetAttribute(const std::string& nodeName, const char* attributeName, float& value);
		static bool GetAttribute(const std::string& nodeName, const char* attributeName, double& value);

		static std::string GetAttributeAsStr(const std::string& nodeName, const char* attributeName);
		static int GetAttributeAsInt(const std::string& nodeName, const char* attributeName);
		static Math::Vector2 GetAttributeAsVector2(const std::string& nodeName, const char* attributeName);
		static Math::Vector3 GetAttributeAsVector3(const std::string& nodeName, const char* attributeName);
		static Math::Vector4 GetAttributeAsVector4(const std::string& nodeName, const char* attributeName);
		//====================================================================================================

		//= IO =======================================
		static bool Load(const std::string& filePath);
		static bool Save(const std::string& filePath);
		//============================================

	private:
		// Returns an attribute as xml_attribute for further handling
		static pugi::xml_attribute GetAttribute(const std::string& nodeName, const char* attributeName);

		// Returns a node by name
		static std::shared_ptr<pugi::xml_node> GetNodeByName(const std::string& name);

		// Returns all the nodes in the current document
		static void GetAllNodes();

		// Returns all the descendant nodes of a node
		static void GetNodes(pugi::xml_node node);

		static std::unique_ptr<pugi::xml_document> m_document;
		static std::vector<std::shared_ptr<pugi::xml_node>> m_nodes;
	};
}
