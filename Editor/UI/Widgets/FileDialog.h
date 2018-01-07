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

#pragma once

//= INCLUDES ===============
#include <map>
#include <memory>
#include "Core/Stopwatch.h"
#include "../IconProvider.h"
#include "../EditorHelper.h"
//==========================

enum FileDialog_Style
{
	FileDialog_Style_Basic,
	FileDialog_Style_Open,
	FileDialog_Style_Load,
	FileDialog_Style_Save
};

enum FileDialog_Filter
{
	FileDialog_Filter_All,
	FileDialog_Filter_Scene,
	FileDialog_Filter_Model
};

class FileDialog
{
public:
	FileDialog(Directus::Context* context, bool standaloneWindow = true, FileDialog_Filter filter = FileDialog_Filter_All, FileDialog_Style style = FileDialog_Style_Basic);

	// Filter
	FileDialog_Filter GetFilter() { return m_filter; }
	void SetFilter(FileDialog_Filter filter);

	// Style
	FileDialog_Style GetStyle() { return m_style; }
	void SetStyle(FileDialog_Style style);
	
	// Show
	bool Show(bool* isVisible, std::string* path);

private:	
	void ViewPath(const std::string& pathDoubleClicked);
	bool NavigateToDirectory(const std::string& pathDoubleClicked);

	std::string m_title;
	std::string m_pathVisible;
	std::string m_pathClicked;
	std::string m_pathDoubleClicked;
	FileDialog_Style m_style;
	FileDialog_Filter m_filter;
	bool m_isWindow;
	float m_itemSize;
	bool m_selectionMade = false;
	bool m_isInDirectory;
	std::map<std::string, IconProvider_Icon> m_directoryContents;
	std::unique_ptr<Directus::Stopwatch> m_stopwatch;
	char m_fileNameText[BUFFER_TEXT_DEFAULT];
	char m_labelText[BUFFER_TEXT_DEFAULT];
	Directus::Context* m_context;
};