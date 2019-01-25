/*
Copyright(c) 2016-2019 Panos Karabelas

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
	FileDialog_Type_Browser,
	FileDialog_Type_FileSelection
};

enum FileDialog_Operation
{
	FileDialog_Op_Open,
	FileDialog_Op_Load,
	FileDialog_Op_Save
};

enum FileDialog_Filter
{
	FileDialog_Filter_All,
	FileDialog_Filter_Scene,
	FileDialog_Filter_Model
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
		m_label			= Directus::FileSystem::GetFileNameFromFilePath(path);
	}

	const std::string& GetPath() const	{ return m_path; }
	const std::string& GetLabel() const	{ return m_label; }
	unsigned int GetID() const			{ return m_id; }
	void* GetShaderResource() const		{ return SHADER_RESOURCE_BY_THUMBNAIL(m_thumbnail); }
	bool IsDirectory()					{ return m_isDirectory; }
	float GetTimeSinceLastClickMs()		{ return (float)m_timeSinceLastClick.count(); }

	void Clicked()	
	{ 
		auto now				= std::chrono::high_resolution_clock::now();
		m_timeSinceLastClick	= now - m_lastClickTime;
		m_lastClickTime			= now;
	}
	
private:
	Thumbnail m_thumbnail;
	unsigned int m_id;
	std::string m_path;
	std::string m_label;
	bool m_isDirectory;
	std::chrono::duration<double, std::milli> m_timeSinceLastClick;
	std::chrono::time_point<std::chrono::high_resolution_clock> m_lastClickTime;
};

class FileDialog
{
public:
	FileDialog(Directus::Context* context, bool standaloneWindow, FileDialog_Type type, FileDialog_Operation operation, FileDialog_Filter filter);

	// Type & Filter
	FileDialog_Type GetType()		{ return m_type; }
	FileDialog_Filter GetFilter()	{ return m_filter; }

	// Operation
	FileDialog_Operation GetOperation() { return m_operation; }
	void SetOperation(FileDialog_Operation operation);

	// Shows the dialog and returns true if a a selection was made
	bool Show(bool* isVisible, std::string* directory = nullptr, std::string* filePath = nullptr);

	void SetCallback_OnItemClicked(const std::function<void(const std::string&)>& callback)			{ m_callback_OnItemClicked = callback; }
	void SetCallback_OnItemDoubleClicked(const std::function<void(const std::string&)>& callback)	{ m_callback_OnItemDoubleClicked = callback; }

private:
	void Show_Top(bool* isVisible);
	void Show_Middle();
	void Show_Bottom(bool* isVisible);

	// Item functionality handling
	void Item_Drag(FileDialog_Item* item);
	void Item_Click(FileDialog_Item* item);
	void Item_ContextMenu(FileDialog_Item* item);

	// Misc	
	bool Dialog_SetCurrentPath(const std::string& path);
	bool Dialog_UpdateFromDirectory(const std::string& path);
	void EmptyArea_ContextMenu();

	FileDialog_Type m_type;
	FileDialog_Operation m_operation;
	FileDialog_Filter m_filter;

	std::string m_title;
	std::string m_currentDirectory;
	std::string m_inputBox;
	std::vector<FileDialog_Item> m_items;
	bool m_isWindow;
	float m_itemSize;
	bool m_selectionMade;
	bool m_isDirty;
	bool m_wasVisible;
	Directus::Context* m_context;

	// Callbacks
	std::function<void(const std::string&)> m_callback_OnItemClicked;
	std::function<void(const std::string&)> m_callback_OnItemDoubleClicked;
};