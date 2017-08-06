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

//= INCLUDES ===============
#include "XmlDocument.h"
#include "pugixml.hpp"
#include "../Logging/Log.h"
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../FileSystem/FileSystem.h"
//==========================

//= NAMESPACES ================
using namespace std;
using namespace pugi;
using namespace Directus::Math;
//=============================

namespace Directus
{
	unique_ptr<xml_document> XmlDocument::m_document;
	vector<shared_ptr<xml_node>> XmlDocument::m_nodes;

	void XmlDocument::Create()
	{
		// Generate new XML document within memory
		m_document = make_unique<xml_document>();

		// Generate XML declaration
		auto declarationNode = m_document->append_child(node_declaration);
		declarationNode.append_attribute("version") = "1.0";
		declarationNode.append_attribute("encoding") = "ISO-8859-1";
		declarationNode.append_attribute("standalone") = "yes";
	}

	void XmlDocument::Release()
	{
		m_nodes.clear();
		m_nodes.shrink_to_fit();
	}

	//= NODES =======================================================================
	void XmlDocument::AddNode(const string& name)
	{
		if (!m_document)
			return;

		auto node = make_shared<xml_node>(m_document->append_child(name.c_str()));
		m_nodes.push_back(node);
	}

	bool XmlDocument::AddChildNode(const string& parentName, const string& name)
	{
		auto parentNode = GetNodeByName(parentName);
		if (!parentNode)
		{
			LOG_WARNING("XmlDocument: Can't add child node \"" + name + "\", parent node \"" + parentName + "\" doesn't exist.");
			return false;
		}

		auto node = make_shared<xml_node>(parentNode->append_child(name.c_str()));
		m_nodes.push_back(node);

		return true;
	}

	//= ADD ATTRIBUTE ================================================================================================
	bool XmlDocument::AddAttribute(const string& nodeName, const char* attributeName, const char* value)
	{
		auto node = GetNodeByName(nodeName);
		if (!node)
		{
			LOG_WARNING("XmlDocument: Can't add attribute \"" + string(attributeName) + "\", node \"" + nodeName + "\" doesn't exist.");
			return false;
		}

		node->append_attribute(attributeName) = value;

		return true;
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& attributeName, const string& value)
	{
		return AddAttribute(nodeName, attributeName.c_str(), value.c_str());
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& attributeName, bool value)
	{
		string valueStr = value ? "true" : "false";
		return AddAttribute(nodeName, attributeName, valueStr);
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& attributeName, int value)
	{
		return AddAttribute(nodeName, attributeName, to_string(value));
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& attributeName, float value)
	{
		return AddAttribute(nodeName, attributeName, to_string(value));
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& attributeName, double value)
	{
		return AddAttribute(nodeName, attributeName, to_string(value));
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& attributeName, Vector2& value)
	{
		return AddAttribute(nodeName, attributeName, value.ToString());
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& attributeName, Vector3& value)
	{
		return AddAttribute(nodeName, attributeName, value.ToString());
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& attributeName, Vector4& value)
	{
		return AddAttribute(nodeName, attributeName, value.ToString());
	}

	//= GET ATTRIBUTE ===================================================================================
	bool XmlDocument::GetAttribute(const string& nodeName, const char* attributeName, string& value)
	{
		xml_attribute attribute = GetAttribute(nodeName, attributeName);

		if (!attribute)
			return false;

		// Get value
		value = attribute.value();

		return true;
	}

	bool XmlDocument::GetAttribute(const string& nodeName, const char* attributeName, int& value)
	{
		xml_attribute attribute = GetAttribute(nodeName, attributeName);

		if (!attribute)
			return false;

		// Get value
		value = attribute.as_int();

		return true;
	}

	bool XmlDocument::GetAttribute(const string& nodeName, const char* attributeName, bool& value)
	{
		xml_attribute attribute = GetAttribute(nodeName, attributeName);

		if (!attribute)
			return false;

		// Get value
		value = attribute.as_bool();

		return true;
	}

	bool XmlDocument::GetAttribute(const string& nodeName, const char* attributeName, float& value)
	{
		xml_attribute attribute = GetAttribute(nodeName, attributeName);

		if (!attribute)
			return false;

		// Get value
		value = attribute.as_float();

		return true;
	}

	bool XmlDocument::GetAttribute(const string& nodeName, const char* attributeName, double& value)
	{
		xml_attribute attribute = GetAttribute(nodeName, attributeName);

		if (!attribute)
			return false;

		// Get value
		value = attribute.as_double();

		return true;
	}

	string XmlDocument::GetAttributeAsStr(const string& nodeName, const char* attributeName)
	{
		string valueStr;
		GetAttribute(nodeName, attributeName, valueStr);

		return valueStr;
	}

	int XmlDocument::GetAttributeAsInt(const string& nodeName, const char* attributeName)
	{
		int value;
		GetAttribute(nodeName, attributeName, value);

		return value;
	}

	Vector2 XmlDocument::GetAttributeAsVector2(const string& nodeName, const char* attributeName)
	{
		string valueStr;
		GetAttribute(nodeName, attributeName, valueStr);

		Vector2 vec;
		vec.x = atof(FileSystem::GetStringBetweenExpressions(valueStr, "X:", ",").c_str());
		vec.y = atof(FileSystem::GetStringAfterExpression(valueStr, "Y:").c_str());

		return vec;
	}

	Vector3 XmlDocument::GetAttributeAsVector3(const string& nodeName, const char* attributeName)
	{
		string valueStr;
		GetAttribute(nodeName, attributeName, valueStr);

		Vector3 vec;
		vec.x = atof(FileSystem::GetStringBetweenExpressions(valueStr, "X:", ",").c_str());
		vec.y = atof(FileSystem::GetStringBetweenExpressions(valueStr, "Y:", ",").c_str());
		vec.z = atof(FileSystem::GetStringAfterExpression(valueStr, "Z:").c_str());

		return vec;
	}


	Vector4 XmlDocument::GetAttributeAsVector4(const string& nodeName, const char* attributeName)
	{
		string valueStr;
		GetAttribute(nodeName, attributeName, valueStr);

		Vector4 vec;
		vec.x = atof(FileSystem::GetStringBetweenExpressions(valueStr, "X:", ",").c_str());
		vec.y = atof(FileSystem::GetStringBetweenExpressions(valueStr, "Y:", ",").c_str());
		vec.z = atof(FileSystem::GetStringBetweenExpressions(valueStr, "Z:", ",").c_str());
		vec.w = atof(FileSystem::GetStringAfterExpression(valueStr, "W:").c_str());

		return vec;
	}

	//= IO =======================================
	bool XmlDocument::Load(const string& filePath)
	{
		m_document = make_unique<xml_document>();
		xml_parse_result result = m_document->load_file(filePath.c_str());

		if (result.status != status_ok)
		{
			if (result.status == status_file_not_found)
			{
				LOG_ERROR("XmlDocument: File \"" + string(filePath) + "\" was not found.");
			}
			else
			{
				LOG_ERROR("XmlDocument: " + string(result.description()));
			}

			m_document.release();
			return false;
		}

		GetAllNodes();

		return true;
	}

	bool XmlDocument::Save(const string& filePath)
	{
		if (!m_document)
			return false;

		if (FileSystem::FileExists(filePath))
		{
			FileSystem::DeleteFile_(filePath);
		}

		return m_document->save_file(filePath.c_str());
	}

	//= PRIVATE =======================================================
	xml_attribute XmlDocument::GetAttribute(const string& nodeName, const char* attributeName)
	{
		xml_attribute attribute;

		// Make sure the nod exists
		auto node = GetNodeByName(nodeName);
		if (!node)
		{
			LOG_WARNING("XmlDocument: Can't get attribute \"" + string(attributeName) + "\", node \"" + nodeName + "\" doesn't exist.");
			return attribute;
		}

		// Make sure the attribute exists
		attribute = node->attribute(attributeName);
		if (!attribute)
		{
			LOG_WARNING("XmlDocument: Can't get attribute, attribute \"" + string(attributeName) + "\" doesn't exist.");
		}

		return attribute;
	}

	shared_ptr<xml_node> XmlDocument::GetNodeByName(const string& name)
	{
		for (const auto& node : m_nodes)
		{
			if (node->name() == name)
			{
				return node;
			}
		}

		return shared_ptr<xml_node>();
	}

	void XmlDocument::GetAllNodes()
	{
		if (!m_document)
			return;

		Release();

		for (xml_node child = m_document->first_child(); child; child = child.next_sibling())
		{
			m_nodes.push_back(make_shared<xml_node>(child));
			if (child.last_child())
			{
				GetNodes(child);
			}
		}
	}

	void XmlDocument::GetNodes(xml_node node)
	{
		if (!node)
			return;

		for (xml_node child = node.first_child(); child; child = child.next_sibling())
		{
			m_nodes.push_back(make_shared<xml_node>(child));
			if (child.last_child())
			{
				GetNodes(child);
			}
		}
	}
}
