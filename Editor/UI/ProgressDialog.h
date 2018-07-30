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

namespace Directus
{
	class Context;
}

class ProgressDialog
{
public:
	ProgressDialog(char* title = "Hold on...");
	~ProgressDialog();

	static ProgressDialog& Get()
	{
		static ProgressDialog instance;
		return instance;
	}

	void Update();
	void SetIsVisible(bool isVisible) { m_isVisible = isVisible; }
	void SetProgress(float progress) { m_progress = progress; }
	void SetProgressStatus(const std::string& progressStatus) { m_progressStatus = progressStatus; }

private:
	void ShowProgressBar();

	std::string m_title;
	bool m_isVisible;
	float m_progress;
	std::string m_progressStatus;
};