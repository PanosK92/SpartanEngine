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

//= INCLUDES =====================
#include <memory>
#include "IconProvider.h"
#include "EditorHelper.h"
#include "FileSystem/FileSystem.h"
//================================

enum FileDialog_Type
{
	FileDialog_Browser,
	FileDialog_Open,
	FileDialog_Load,
	FileDialog_Save
};

enum FileDialog_FileFilter_Type
{
	FileDialog_FileFilter_All,
	FileDialog_FileFilter_Scene,
	FileDialog_FileFilter_Model
};

class FileDialog_Item
{
public:
	FileDialog_Item(const std::string& path, const Thumbnail& thumbnail)
	{
		m_path			= path;
		m_thumbnail		= thumbnail;
		m_id			= GENERATE_GUID;
		m_isDirectory	= Directus::FileSystem::IsDirectory(path);
		EditorHelper::SetCharArray(&m_label[0], Directus::FileSystem::GetFileNameFromFilePath(path));
	}

	const char* GetPath() const		{ return m_path.c_str(); }
	const char* GetLabel() const	{ return &m_label[0]; }
	unsigned int GetID() const		{ return m_id; }
	void* GetShaderResource() const { return SHADER_RESOURCE_BY_THUMBNAIL(m_thumbnail); }
	bool IsDirectory()				{ return m_isDirectory; }
	float GetTimeSinceLastClickMs() { return (float)m_timeSinceLastClick.count(); }

	void Clicked()	
	{ 
		auto now				= std::chrono::high_resolution_clock::now();
		m_timeSinceLastClick	= now - m_lastClickTime;
		m_lastClickTime			= now;
	}
	

private:
	Thumbnail m_thumbnail;
	std::string m_path;
	unsigned int m_id;
	char m_label[BUFFER_TEXT_DEFAULT]{};
	bool m_isDirectory;
	std::chrono::duration<double, std::milli> m_timeSinceLastClick;
	std::chrono::time_point<std::chrono::high_resolution_clock> m_lastClickTime;
};

class FileDialog
{
public:
	FileDialog(Directus::Context* context, bool standaloneWindow = true, FileDialog_FileFilter_Type filter = FileDialog_FileFilter_All, FileDialog_Type type = FileDialog_Browser);

	// Filter
	FileDialog_FileFilter_Type GetFilter() { return m_filter; }
	void SetFilter(FileDialog_FileFilter_Type filter);

	// Style
	FileDialog_Type GetStyle() { return m_style; }
	void SetStyle(FileDialog_Type type);

	// Shows the dialog and returns true if a a selection was made
	bool Show(bool* isVisible, std::string* pathDoubleClicked = nullptr);

	void SetCallback_OnPathClicked(const std::function<void(const std::string&)>& callback)			{ m_callback_OnPathClicked = callback; }
	void SetCallback_OnPathDoubleClicked(const std::function<void(const std::string&)>& callback)	{ m_callback_OnPathDoubleClicked = callback; }

private:
	void Show_Top(bool* isVisible);
	void Show_Middle();
	void Show_Bottom(bool* isVisible);

	// Item functionality handling
	void Item_Drag(FileDialog_Item* item);
	void Item_Click(FileDialog_Item* item);
	void Item_ContextMenu(FileDialog_Item* item);

	// Misc
	bool Dialog_UpdateFromDirectory(const char* path = nullptr);
	void Dialog_Click();
	void Dialog_ContextMenu();
	void Dialog_SetCurrentPath(const std::string& path);

	std::string m_title;
	std::string m_currentPath;
	unsigned int m_currentPathID;
	char m_selectedFileName[BUFFER_TEXT_DEFAULT]{};
	std::vector<FileDialog_Item> m_items;
	FileDialog_Type m_style;
	FileDialog_FileFilter_Type m_filter;
	bool m_isWindow;
	float m_itemSize;
	bool m_selectionMade;
	bool m_isDirty;
	bool m_wasVisible;
	Directus::Context* m_context;

	// Callbacks
	std::function<void(const std::string&)> m_callback_OnPathClicked;
	std::function<void(const std::string&)> m_callback_OnPathDoubleClicked;
};