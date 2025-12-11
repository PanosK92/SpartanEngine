/*
Copyright(c) 2015-2025 Panos Karabelas & Thomas Ray

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
#include "../Widget.h"
//=================

// TODO: Add functionality to edit node properties such as name, type, value, connections, etc.
// TODO: Integrate with the node system to fetch and update node data.

/**
 * @class NodeProperties
 * @brief A class responsible for displaying, editing, and managing the properties of a selected node.
 * This widget provides an interface for users to view and modify various attributes of nodes within the node system.
 * It typically includes features such as property panels, input fields, and other UI elements that allow for
 * intuitive interaction with node properties.
 */
class NodeProperties : public Widget
{
public:
    explicit NodeProperties(Editor* editor);

    void OnTickVisible() override;
};
