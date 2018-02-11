/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include <string>
//===============

namespace Directus { class Context; }

class Widget
{
public:
	virtual ~Widget() {}
	virtual void Initialize(Directus::Context* context);
	virtual void Begin();
	virtual void Update() = 0;
	virtual void End();
	bool GetIsWindow() { return m_isWindow; }

protected:
	Directus::Context* m_context = nullptr;
	bool m_isVisible = false;
	bool m_isWindow = true;
	std::string m_title;
	int m_windowFlags = 0;
	float m_xMin = 0;
	float m_xMax = 0;
	float m_yMin = 0;
	float m_yMax = 0;
};