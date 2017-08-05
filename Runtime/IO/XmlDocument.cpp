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

//= INCLUDES ==============
#include "XmlDocument.h"
#include "pugixml.hpp"
#include "../Logging/Log.h"
//=========================

//= NAMESPACES ======
using namespace std;
using namespace pugi;
//===================

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
			LOG_WARNING("Can't add child node, parent node doesn't exist.");
			return false;
		}

		auto node = make_shared<xml_node>(parentNode->append_child(name.c_str()));
		m_nodes.push_back(node);

		return true;
	}

	//= ATTRIBUTES =========================================================================================
	bool XmlDocument::AddAttribute(const string& nodeName, const char* tag, const char* value)
	{
		auto node = GetNodeByName(nodeName);
		if (!node)
		{
			LOG_WARNING("Can't add attribute, node doesn't exist.");
			return false;
		}

		node->append_attribute(tag) = value;

		return true;
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& tag, const string& value)
	{
		return AddAttribute(nodeName, tag.c_str(), value.c_str());
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& tag, bool value)
	{
		string valueStr = value ? "true" : "false";
		return AddAttribute(nodeName, tag, valueStr);
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& tag, int value)
	{
		return AddAttribute(nodeName, tag, to_string(value));
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& tag, float value)
	{
		return AddAttribute(nodeName, tag, to_string(value));
	}

	bool XmlDocument::AddAttribute(const string& nodeName, const string& tag, double value)
	{
		return AddAttribute(nodeName, tag, to_string(value));
	}

	//= IO =======================================
	bool XmlDocument::Load(const string& filePath)
	{
		m_document = make_unique<xml_document>();
		xml_parse_result result = m_document->load_file(filePath.c_str());
		
		if (result.status != status_ok)
		{
			LOG_ERROR(result.description());
			m_document.release();
			return false;
		}

		return true;
	}

	bool XmlDocument::Save(const string& filePath)
	{
		if (!m_document)
			return false;

		return m_document->save_file(filePath.c_str());
	}

	//= PRIVATE =======================================================
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
}
