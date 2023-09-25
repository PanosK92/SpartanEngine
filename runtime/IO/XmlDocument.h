/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ===================
#include <memory>
#include <string>
#include <vector>
#include "../Core/Definitions.h"
//==============================

//= FORWARD DECLARATIONS =
namespace pugi
{
    class xml_document; 
    class xml_node;
    class xml_attribute;
}
//========================

namespace Spartan
{
    //= FORWARD DECLARATIONS =
    namespace Math
    {
        class Vector2;
        class Vector3;
        class Vector4;
    }
    //========================

    class SP_CLASS XmlDocument
    {
    public:
        XmlDocument();
        ~XmlDocument();

        //= NODES =================================================================================
        void AddNode(const std::string& node_name);
        bool AddChildNode(const std::string& parent_node_name, const std::string& child_node_name);
        //=========================================================================================

        //= NODE ATTRIBUTES =========================================================================================
        bool AddAttribute(const std::string& node_name, const std::string& attribute_name, const std::string& value);
        bool AddAttribute(const std::string& node_name, const std::string& attribute_name, bool value);
        bool AddAttribute(const std::string& node_name, const std::string& attribute_name, int value);
        bool AddAttribute(const std::string& node_name, const std::string& attribute_name, uint32_t value);
        bool AddAttribute(const std::string& node_name, const std::string& attribute_name, float value);
        bool AddAttribute(const std::string& node_name, const std::string& attribute_name, double value);
        bool AddAttribute(const std::string& node_name, const std::string& attribute_name, Math::Vector2& value);
        bool AddAttribute(const std::string& node_name, const std::string& attribute_name, Math::Vector3& value);
        bool AddAttribute(const std::string& node_name, const std::string& attribute_name, Math::Vector4& value);

        bool GetAttribute(const std::string& node_name, const std::string& attribute_name, std::string* value);
        bool GetAttribute(const std::string& node_name, const std::string& attribute_name, int* value);
        bool GetAttribute(const std::string& node_name, const std::string& attribute_name, uint32_t* value);
        bool GetAttribute(const std::string& node_name, const std::string& attribute_name, bool* value);
        bool GetAttribute(const std::string& node_name, const std::string& attribute_name, float* value);
        bool GetAttribute(const std::string& node_name, const std::string& attribute_name, double* value);
        bool GetAttribute(const std::string& node_name, const std::string& attribute_name, Math::Vector2* value);
        bool GetAttribute(const std::string& node_name, const std::string& attribute_name, Math::Vector3* value);
        bool GetAttribute(const std::string& node_name, const std::string& attribute_name, Math::Vector4* value);

        template <class T>
        T GetAttributeAs(const std::string& node_name, const std::string& attribute_name)
        {
            T value;
            GetAttribute(node_name, attribute_name, &value);
            return value;
        }
        //===========================================================================================================


        //= IO ======================================
        bool Load(const std::string& filePath);
        bool Save(const std::string& filePath) const;
        //===========================================

    private:
        pugi::xml_attribute GetAttribute(const std::string& nodeName, const std::string& attributeName);
        std::shared_ptr<pugi::xml_node> GetNodeByName(const std::string& name);
        void GetAllNodes();
        void GetNodes(pugi::xml_node node);

        std::unique_ptr<pugi::xml_document> m_document;
        std::vector<std::shared_ptr<pugi::xml_node>> m_nodes;
    };
}
