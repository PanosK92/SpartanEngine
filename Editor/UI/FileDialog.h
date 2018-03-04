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

//= INCLUDES =================
#include <memory>
#include "Core/Stopwatch.h"
#include "ThumbnailProvider.h"
#include "EditorHelper.h"
//============================

enum FileDialog_Mode
{
	FileDialog_Basic,
	FileDialog_Open,
	FileDialog_Load,
	FileDialog_Save
};

enum FileDialog_Filter
{
	FileDialog_All,
	FileDialog_Scene,
	FileDialog_Model
};

class FileDialog
{
public:
	FileDialog(Directus::Context* context, bool standaloneWindow = true, FileDialog_Filter filter = FileDialog_All, FileDialog_Mode type = FileDialog_Basic);

	// Filter
	FileDialog_Filter GetFilter() { return m_filter; }
	void SetFilter(FileDialog_Filter filter);

	// Style
	FileDialog_Mode GetStyle() { return m_style; }
	void SetStyle(FileDialog_Mode type);

	// Show
	bool Show(bool* isVisible, std::string* path);

private:
	void Dialog_Top(bool* isVisible);
	void Dialog_Middle();
	void Dialog_Bottom(bool* isVisible);
	bool NavigateToDirectory(const std::string& pathClicked);
	void AddThumbnail(const std::string& filePath, Thumbnail_Type type = Thumbnail_Custom);
	void HandleDrag(const std::pair<const std::basic_string<char>, Thumbnail>& entry);
	void HandleClicking(const char* directoryEntry);
	void ContextMenu();

	std::string m_title;

	// Display name, data
	std::map<std::string, Thumbnail> m_directoryEntries;
	std::string m_currentPath;
	std::string m_currentFullPath;

	FileDialog_Mode m_style;
	FileDialog_Filter m_filter;

	char m_fileNameText[BUFFER_TEXT_DEFAULT]{};
	char m_itemLabel[BUFFER_TEXT_DEFAULT]{};

	bool m_isWindow;
	float m_itemSize;
	bool m_selectionMade;
	bool m_isDirty;
	std::unique_ptr<Directus::Stopwatch> m_stopwatch;
	Directus::Context* m_context;
};