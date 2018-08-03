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

//= INCLUDES ==================
#include "../Core/EngineDefs.h"
#include "../Core/Settings.h"
//=============================

namespace Directus
{
	class ENGINE_CLASS RHI_Viewport
	{
	public:
		RHI_Viewport(float topLeftX, float topLeftY, float width, float height, float minDepth, float maxDepth)
		{
			m_topLeftX	= topLeftX;
			m_topLeftY	= topLeftY;
			m_width		= width;
			m_height	= height;
			m_minDepth	= minDepth;
			m_maxDepth	= maxDepth;
		}

		RHI_Viewport(float width = (float)Settings::Get().GetResolutionWidth(), float height = (float)Settings::Get().GetResolutionHeight(), float maxDepth = 1.0f)
		{
			m_topLeftX	= 0.0f;
			m_topLeftY	= 0.0f;
			m_width		= width;
			m_height	= height;
			m_minDepth	= 0.0f;
			m_maxDepth	= 1.0f;
		}

		~RHI_Viewport(){}

		float GetTopLeftX() const { return m_topLeftX; }
		float GetTopLeftY() const { return m_topLeftY; }
		float GetWidth()	const { return m_width; }
		float GetHeight()	const { return m_height; }
		float GetMinDepth() const { return m_minDepth; }
		float GetMaxDepth() const { return m_maxDepth; }

		void SetWidth(float width)		{ m_width = width; }
		void SetHeight(float height)	{ m_height = height; }

	private:
		float m_topLeftX;
		float m_topLeftY;
		float m_width;
		float m_height;
		float m_minDepth;
		float m_maxDepth;
	};
}