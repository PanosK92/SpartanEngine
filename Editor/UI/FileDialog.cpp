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

//= INCLUDES =====================
#include "FileDialog.h"
#include "imgui/imgui.h"
#include "FileSystem/FileSystem.h"
#include "Logging/Log.h"
#include "IconProvider.h"
#include "DragDrop.h"
//================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

static float g_itemSizeMin		= 50.0f;
static float g_itemSizeMax		= 150.0f;

#define GET_WINDOW_NAME (type == FileDialog_Style_Open)		? "Open"		: (type == FileDialog_Style_Load)		? "Load"		: (type == FileDialog_Style_Save) ? "Save" : "View"
#define GET_FILTER_NAME	(m_filter == FileDialog_Filter_All) ? "All (*.*)"	: (m_filter == FileDialog_Filter_Model) ? "Model(*.*)"	: "Scene (*.scene)"
#define GET_BUTTON_NAME (m_style == FileDialog_Style_Open)	? "Open"		: (m_style == FileDialog_Style_Load)	? "Load"		: "Save"

static DragDropPayload g_dragDropPayload;

FileDialog::FileDialog(Context* context, bool standaloneWindow, FileDialog_Filter filter, FileDialog_Style type)
{
	m_context			= context;
	m_filter			= filter;
	m_style				= type;
	m_title				= GET_WINDOW_NAME;
	m_isWindow			= standaloneWindow;
	m_currentDirectory	= FileSystem::GetWorkingDirectory();
	m_pathClicked		= m_currentDirectory;
	m_itemSize			= (type != FileDialog_Style_Basic) ? g_itemSizeMin * 2.0f : g_itemSizeMin;
	m_stopwatch			= make_unique<Stopwatch>();
	m_navigateToPath	= false;
	g_dragDropPayload	= DragDropPayload(g_dragDrop_Type_Texture, nullptr);
}

void FileDialog::SetFilter(FileDialog_Filter filter)
{
	m_filter = filter;
}

void FileDialog::SetStyle(FileDialog_Style type)
{
	m_style = type;
	m_title = GET_WINDOW_NAME;
}

bool FileDialog::Show(bool* isVisible, string* path)
{
	if (!(*isVisible))
		return false;

	m_selectionMade = false;

	if (m_isWindow)
	{
		ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
		ImGui::Begin(m_title.c_str(), isVisible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_ResizeFromAnySide | ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImGui::SetWindowFocus();
	}

	if (ImGui::Button("<"))
	{
		m_pathClicked		= FileSystem::GetParentDirectory(m_currentDirectory);
		m_navigateToPath	= true;
	}
	ImGui::SameLine(); ImGui::Text(m_currentDirectory.c_str());
	ImGui::PushItemWidth(ImGui::GetWindowSize().x * 0.25f);
	ImGui::SliderFloat("##FileDialogSlider", &m_itemSize, g_itemSizeMin, g_itemSizeMax);
	ImGui::PopItemWidth();

	ImGui::Separator();

	// List
	int index = 0;
	int columns = ImGui::GetWindowContentRegionWidth() / m_itemSize;
	columns = columns < 1 ? 1 : columns;
	ImGui::Columns(columns, nullptr, false);
	for (const auto& item : m_directoryContents)
	{
		// ICON
		ImGui::PushID(index);
		if (ICON_PROVIDER_IMAGE_BUTTON(item.second, m_itemSize))
		{
			if (m_pathClicked != item.first) // Single click
			{
				m_pathClicked = item.first;
				EditorHelper::SetCharArray(&m_fileNameText[0], FileSystem::GetFileNameFromFilePath(m_pathClicked));
				m_stopwatch->Start();
			}
			else if (m_stopwatch->GetElapsedTimeMs() <= 500) // Double click
			{
				bool isDirectory	= FileSystem::IsDirectory(m_pathClicked);
				m_navigateToPath	= isDirectory;
				m_pathClicked		= !isDirectory ? item.first : item.first + "/";
				m_selectionMade		= !isDirectory;
				m_stopwatch->Start();
			}
		}

		if (m_style == FileDialog_Style_Basic)
		{
			g_dragDropPayload.payload = item.first.c_str();
			DragDrop::SendPayload(g_dragDropPayload);
		}
		
		ImGui::PopID();

		// LABEL
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() - m_itemSize - 16); // move to the left of the thumbnail
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + m_itemSize - 10); // move to the bottom of the thumbnail
		ImGui::PushItemWidth(ImGui::GetColumnWidth());
		
		EditorHelper::SetCharArray(&m_itemLabel[0], FileSystem::GetFileNameFromFilePath(item.first));
		ImGui::InputText("##Temp", m_itemLabel, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_ReadOnly);

		ImGui::PopItemWidth();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + m_itemSize + 16); // move to the left of the thumbnail
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - m_itemSize + 10); // move to the bottom of the thumbnail

		ImGui::NextColumn();
		index++;
	}
	ImGui::Columns(1);

	// Bottom-right buttons
	if (m_style != FileDialog_Style_Basic)
	{
		ImGui::SetCursorPosY(ImGui::GetWindowSize().y - 35); // move to the bottom of the window
		ImGui::Separator();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f); // move to the bottom of the window
		
		ImGui::PushItemWidth(ImGui::GetWindowSize().x - 235);
		ImGui::InputText("##FileName", m_fileNameText, BUFFER_TEXT_DEFAULT);
		ImGui::PopItemWidth();
		
		ImGui::SameLine(); ImGui::Text(GET_FILTER_NAME);

		ImGui::SameLine(); if (ImGui::Button(GET_BUTTON_NAME))
		{
			m_selectionMade = true;			
		}

		ImGui::SameLine(); if (ImGui::Button("Cancel"))
		{
			m_selectionMade = false;
			(*isVisible) = false;
		}		
	}

	if (m_isWindow)
	{
		ImGui::End();
	}

	if (m_navigateToPath)
	{
		ViewPath(m_pathClicked);
		m_navigateToPath = false;
	}

	if (m_selectionMade)
	{
		(*path) = FileSystem::GetDirectoryFromFilePath(m_currentDirectory) + &m_fileNameText[0];
	}

	return m_selectionMade;
}

void FileDialog::ViewPath(const string& pathClicked)
{
	if (pathClicked.empty())
		return;

	// Directory
	if (FileSystem::IsDirectory(pathClicked))
	{
		NavigateToDirectory(pathClicked);
	}
	// File
	else
	{

	}
}

bool FileDialog::NavigateToDirectory(const string& pathClicked)
{
	if (!FileSystem::IsDirectory(pathClicked))
	{
		LOG_WARNING("FileDialog: Can't navigate to directory, provided directory is invalid.");
		return false;
	}

	if (m_currentDirectory == pathClicked && !m_directoryContents.empty())
		return true;

	m_currentDirectory = pathClicked;
	m_directoryContents.clear();

	// Get directories
	vector<string> childDirectories = FileSystem::GetDirectoriesInDirectory(m_currentDirectory);
	for (const auto& childDir : childDirectories)
	{
		m_directoryContents[childDir] = Icon_Folder;
	}

	// Get files (based on filter)
	vector<string> childFiles;
	if (m_filter == FileDialog_Filter_All)
	{
		childFiles = FileSystem::GetFilesInDirectory(m_currentDirectory);
		for (const auto& childFile : childFiles)
		{
			m_directoryContents[childFile] = Icon_File_Default;
		}
	}
	else if (m_filter == FileDialog_Filter_Scene)
	{
		childFiles = FileSystem::GetSupportedSceneFilesInDirectory(m_currentDirectory);
		for (const auto& childFile : childFiles)
		{
			m_directoryContents[childFile] = Icon_File_Scene;
		}
	}
	else if (m_filter == FileDialog_Filter_Model)
	{
		childFiles = FileSystem::GetSupportedModelFilesInDirectory(m_currentDirectory);
		for (const auto& childFile : childFiles)
		{
			m_directoryContents[childFile] = Icon_File_Model;
		}
	}

	EditorHelper::SetCharArray(&m_fileNameText[0], "");
	
	return true;
}
