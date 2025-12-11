/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES ======
#include <cstdint>
//=================

typedef uint32_t NodeId;
typedef uint32_t PinId;
typedef uint32_t LinkId;

constexpr NodeId INVALID_NODE_ID    = 0;
constexpr PinId  INVALID_PIN_ID     = 0;
constexpr LinkId INVALID_LINK_ID    = 0;

/**
 * @enum PinType
 * @brief Represents the type of pin in the node system.
 */
enum class PinType : uint8_t
{
    Flow,
    Bool,
    Int,
    Float,
    String,
    Object,
    Function,
    Delegate,
};

inline const char* pin_type_to_string(PinType e)
{
    switch (e)
    {
        case PinType::Flow:     return "Flow";
        case PinType::Bool:     return "Bool";
        case PinType::Int:      return "Int";
        case PinType::Float:    return "Float";
        case PinType::String:   return "String";
        case PinType::Object:   return "Object";
        case PinType::Function: return "Function";
        case PinType::Delegate: return "Delegate";
        default:                return "unknown";
    }
}

/**
 * @enum PinKind
 * @brief Represents the kind of pin in the node system.
 */
enum class PinKind : uint8_t
{
    Input,
    Output
};

inline const char* pin_kind_to_string(PinKind e)
{
    switch (e)
    {
        case PinKind::Input:    return "Input";
        case PinKind::Output:   return "Output";
        default:                return "unknown";
    }
}

/**
 * @enum NodeType
 * @brief Represents the type of node in the node system.
 */
enum class NodeType : uint8_t
{
    Blueprint,
    Simple,
    Tree,
    Comment,
};

inline const char* node_type_to_string(NodeType e)
{
    switch (e)
    {
        case NodeType::Blueprint:     return "Blueprint";
        case NodeType::Simple:        return "Simple";
        case NodeType::Tree:          return "Tree";
        case NodeType::Comment:       return "Comment";
        default:                      return "unknown";
    }
}

/**
 * @enum NodeCategory
 * @brief Represents the category of a node in the node system.
 * This enumeration helps in organizing and classifying nodes based on their functionality.
 */
enum class NodeCategory : uint8_t
{
    Math,
    Function,
    Asset,
    Material,
    Logic,
    Utility,
    Comment
};

inline const char* node_category_to_string(NodeCategory e)
{
    switch (e)
    {
        case NodeCategory::Math:        return "Math";
        case NodeCategory::Function:    return "Function";
        case NodeCategory::Asset:       return "Asset";
        case NodeCategory::Material:    return "Material";
        case NodeCategory::Logic:       return "Logic";
        case NodeCategory::Utility:     return "Utility";
        case NodeCategory::Comment:     return "Comment";
        default:                        return "unknown";
    }
}
