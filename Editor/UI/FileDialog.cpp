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

//= INCLUDES =====================
#include "FileDialog.h"
#include "ImGui/imgui.h"
#include "FileSystem/FileSystem.h"
#include "Logging/Log.h"
#include "EditorHelper.h"
#include "DragDrop.h"
//================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

static float g_itemSizeMin		= 50.0f;
static float g_itemSizeMax		= 150.0f;
static bool g_itemHoveredOnPreviousFrame;

#define GET_WINDOW_NAME (type == FileDialog_Open)		? "Open"		: (type == FileDialog_Load)			? "Load"		: (type == FileDialog_Save) ? "Save" : "View"
#define GET_FILTER_NAME	(m_filter == FileDialog_All)	? "All (*.*)"	: (m_filter == FileDialog_Model)	? "Model(*.*)"	: "Scene (*.scene)"
#define GET_BUTTON_NAME (m_style == FileDialog_Open)	? "Open"		: (m_style == FileDialog_Load)		? "Load"		: "Save"

static DragDropPayload g_dragDropPayload;

FileDialog::FileDialog(Context* context, bool standaloneWindow, FileDialog_Filter filter, FileDialog_Mode type)
{
	m_context			= context;
	m_filter			= filter;
	m_style				= type;
	m_title				= GET_WINDOW_NAME;
	m_isWindow			= standaloneWindow;
	m_currentPath		= FileSystem::GetWorkingDirectory();
	m_currentFullPath	= m_currentPath;
	m_itemSize			= (type != FileDialog_Basic) ? g_itemSizeMin * 2.0f : g_itemSizeMin;
	m_stopwatch			= make_unique<Stopwatch>();
	m_navigateToPath	= true;
}

void FileDialog::SetFilter(FileDialog_Filter filter)
{
	m_filter = filter;
}

void FileDialog::SetStyle(FileDialog_Mode type)
{
	m_style = type;
	m_title = GET_WINDOW_NAME;
}

bool FileDialog::Show(bool* isVisible, string* path)
{
	if (!(*isVisible))
		return false;

	m_selectionMade = false;
	g_itemHoveredOnPreviousFrame = false;

	if (m_isWindow)
	{
		ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
		ImGui::Begin(m_title.c_str(), isVisible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_ResizeFromAnySide | ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImGui::SetWindowFocus();
	}

	if (ImGui::Button("<"))
	{
		m_currentPath		= FileSystem::GetParentDirectory(m_currentPath);
		m_navigateToPath	= true;
	}
	ImGui::SameLine(); ImGui::Text(m_currentPath.c_str());
	ImGui::PushItemWidth(ImGui::GetWindowSize().x * 0.25f);
	ImGui::SliderFloat("##FileDialogSlider", &m_itemSize, g_itemSizeMin, g_itemSizeMax);
	ImGui::PopItemWidth();

	ImGui::Separator();

	// List
	int index = 0;
	int columns = (int)(ImGui::GetWindowContentRegionWidth() / m_itemSize);
	columns = columns < 1 ? 1 : columns;
	ImGui::Columns(columns, nullptr, false);
	for (const auto& entry : m_directoryEntries)
	{
		// ICON
		ImGui::PushID(index);
		if (THUMBNAIL_BUTTON(entry.second, m_itemSize))
		{
			if (m_currentFullPath != entry.first) // Single click
			{
				m_currentFullPath = entry.first;
				EditorHelper::SetCharArray(&m_fileNameText[0], FileSystem::GetFileNameFromFilePath(entry.first));
				m_stopwatch->Start();
			}
			else if (m_stopwatch->GetElapsedTimeMs() <= 500) // Double click
			{
				bool isDirectory	= FileSystem::IsDirectory(entry.first);
				if (isDirectory)
				{
					m_currentPath		= entry.first;
					m_navigateToPath	= true;
				}
				m_selectionMade		= !isDirectory;
			}
		}
		ImGui::PopID();

		// Drag
		if (m_style == FileDialog_Basic)
		{
			if (DragDrop::Get().DragBegin())
			{
				if (FileSystem::IsSupportedModelFile(entry.first))
				{
					g_dragDropPayload.type = g_dragDrop_Type_Model;
					g_dragDropPayload.data = entry.first.c_str();
					DragDrop::Get().DragPayload(g_dragDropPayload, entry.second.texture->GetShaderResource());				
				}
				else if (FileSystem::IsSupportedImageFile(entry.first) || FileSystem::IsEngineTextureFile(entry.first))
				{
					g_dragDropPayload.type = g_dragDrop_Type_Texture;
					g_dragDropPayload.data = entry.first.c_str();
					DragDrop::Get().DragPayload(g_dragDropPayload, entry.second.texture->GetShaderResource());
				}
				DragDrop::Get().DragEnd();
			}
		}

		// Further handle clicking here
		HandleClicking(entry.first);

		// LABEL
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() - m_itemSize - 16); // move to the left of the thumbnail
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + m_itemSize - 10); // move to the bottom of the thumbnail
		ImGui::PushItemWidth(m_itemSize + 8.5f);
		
		EditorHelper::SetCharArray(&m_itemLabel[0], FileSystem::GetFileNameFromFilePath(entry.first));
		ImGui::Text(m_itemLabel);

		ImGui::PopItemWidth();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + m_itemSize + 16); // move to the right of the thumbnail
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - m_itemSize + 10); // move to the top of the thumbnail

		ImGui::NextColumn();
		index++;
	}
	ImGui::Columns(1);

	// Bottom-right buttons
	if (m_style != FileDialog_Basic)
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
		NavigateToDirectory(m_currentPath);
		m_navigateToPath = false;
	}

	if (m_selectionMade)
	{
		(*path) = m_currentFullPath;
	}

	ContextMenu();

	return m_selectionMade;
}

bool FileDialog::NavigateToDirectory(const string& pathClicked)
{
	if (!FileSystem::IsDirectory(pathClicked))
	{
		LOG_WARNING("FileDialog: Can't navigate to directory, provided directory is invalid.");
		return false;
	}

	m_directoryEntries.clear();

	// Get directories
	vector<string> childDirectories = FileSystem::GetDirectoriesInDirectory(pathClicked);
	for (const auto& childDir : childDirectories)
	{
		AddThumbnail(childDir, Thumbnail_Folder);
	}

	// Get files (based on filter)
	vector<string> childFiles;
	if (m_filter == FileDialog_All)
	{
		childFiles = FileSystem::GetFilesInDirectory(pathClicked);
		for (const auto& childFile : childFiles)
		{	
			AddThumbnail(childFile);
		}
	}
	else if (m_filter == FileDialog_Scene)
	{
		childFiles = FileSystem::GetSupportedSceneFilesInDirectory(pathClicked);
		for (const auto& childFile : childFiles)
		{
			AddThumbnail(childFile, Thumbnail_File_Scene);
		}
	}
	else if (m_filter == FileDialog_Model)
	{
		childFiles = FileSystem::GetSupportedModelFilesInDirectory(pathClicked);
		for (const auto& childFile : childFiles)
		{
			AddThumbnail(childFile, Thumbnail_File_Model);
		}
	}

	EditorHelper::SetCharArray(&m_fileNameText[0], "");
	
	return true;
}

void FileDialog::AddThumbnail(const std::string& filePath, Thumbnail_Type type)
{
	m_directoryEntries[filePath] = ThumbnailProvider::Get().Thumbnail_Load(filePath, type, m_itemSize);
}

void FileDialog::HandleClicking(const std::string& directoryEntry)
{
	// Ensure we are hovering over the window (since we are handling clicks manually)
	if (!ImGui::IsMouseHoveringWindow())
		return;

	// Left click
	if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_Default))
	{
		g_itemHoveredOnPreviousFrame = true;
	}

	// Right click
	if (ImGui::IsMouseClicked(1))
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
		{
			if (FileSystem::IsDirectory(directoryEntry))
			{
				m_currentFullPath = directoryEntry;
			}
			else
			{
				m_currentFullPath = FileSystem::GetDirectoryFromFilePath(m_currentPath) + FileSystem::GetFileNameFromFilePath(directoryEntry);
			}
			g_itemHoveredOnPreviousFrame = true;
		}
		else
		{
			ImGui::OpenPopup("##FileDialogContextMenu");		
		}			
	}

	// Clicking on empty space
	if(((ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) && !g_itemHoveredOnPreviousFrame) && m_stopwatch->GetElapsedTimeMs() >= 500)
	{
		m_currentFullPath = m_currentPath;
	}
}

void FileDialog::ContextMenu()
{
	if (!ImGui::BeginPopup("##FileDialogContextMenu"))
		return;

	if (ImGui::MenuItem("Create folder"))
	{
		FileSystem::CreateDirectory_(m_currentPath + "New folder");
		NavigateToDirectory(m_currentPath);
	}
	else if (ImGui::MenuItem("Delete"))
	{
		if (FileSystem::IsDirectory(m_currentFullPath))
		{
			FileSystem::DeleteDirectory(m_currentFullPath);
			NavigateToDirectory(m_currentPath);
		}
		else
		{
			FileSystem::DeleteFile_(m_currentFullPath);
			NavigateToDirectory(m_currentPath);
		}
	}
	
	ImGui::Separator();
	if (ImGui::MenuItem("Open in file explorer"))
	{
		FileSystem::OpenDirectoryWindow(m_currentFullPath);
	}

	ImGui::EndPopup();
}
