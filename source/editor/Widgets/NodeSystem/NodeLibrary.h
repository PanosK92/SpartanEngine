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
#include "NodeTemplate.h"
#include <memory>
#include <vector>
#include <string>
//=================

class NodeLibrary
{
public:
    NodeLibrary()                              = default;
    ~NodeLibrary()                             = default;

    NodeLibrary(const NodeLibrary&)            = delete;
    NodeLibrary& operator=(const NodeLibrary&) = delete;

    static NodeLibrary& GetInstance()
    {
        static NodeLibrary instance;
        return instance;
    }

    void Initialize();
    void RegisterTemplate(std::unique_ptr<NodeTemplate> node_template);

    [[nodiscard]] std::vector<const NodeTemplate*> SearchTemplates(const std::string& search_text = "", NodeCategory category = NodeCategory::Math) const;
    [[nodiscard]] const std::vector<std::unique_ptr<NodeTemplate>>& GetAllTemplates() const { return m_templates; }

private:
    void RegisterMathNodes();
    void RegisterLogicNodes();
    void RegisterUtilityNodes();

    std::vector<std::unique_ptr<NodeTemplate>> m_templates;
};
