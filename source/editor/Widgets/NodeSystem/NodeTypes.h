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

enum class PinKind : uint8_t
{
    Input,
    Output
};

enum class NodeType : uint8_t
{
    Blueprint,
    Simple,
    Tree,
    Comment,
};

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
