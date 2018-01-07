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

//= INCLUDES =====================
#include "FileDialog.h"
#include "../imgui/imgui.h"
#include "FileSystem/FileSystem.h"
#include "Logging/Log.h"
#include "../IconProvider.h"
#include "Resource/Resource.h"
//================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

static float g_itemSizeMin	= 50.0f;
static float g_itemSizeMax	= 150.0f;

FileDialog::FileDialog(Context* context, bool standaloneWindow, FileDialog_Filter filter, FileDialog_Style type)
{
	m_context = context;

	SetFilter(filter);
	SetStyle(type);

	m_isWindow = standaloneWindow;
	m_pathVisible = FileSystem::GetWorkingDirectory();
	m_pathDoubleClicked = m_pathVisible;
	m_isInDirectory = false;
	m_itemSize = (type != FileDialog_Style_Basic) ? g_itemSizeMin * 2.0f : g_itemSizeMin;
	m_stopwatch = make_unique<Stopwatch>();
}

void FileDialog::SetFilter(FileDialog_Filter filter)
{
	m_filter = filter;
}

void FileDialog::SetStyle(FileDialog_Style type)
{
	m_style = type;
	m_title = (type == FileDialog_Style_Open) ? "Open" : (type == FileDialog_Style_Load) ? "Load" : (type == FileDialog_Style_Save) ? "Save" : "View";
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
		m_pathDoubleClicked = FileSystem::GetParentDirectory(m_pathVisible);
		m_isInDirectory = false;
	}
	ImGui::SameLine(); ImGui::Text(m_pathVisible.c_str());
	ImGui::PushItemWidth(ImGui::GetWindowSize().x * 0.25f);
	ImGui::SliderFloat("##FileDialogSlider", &m_itemSize, g_itemSizeMin, g_itemSizeMax);
	ImGui::PopItemWidth();

	ImGui::Separator();

	// Display list
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
			if (m_pathClicked != item.first) // First time clicking this item
			{
				m_pathClicked = item.first;
				m_stopwatch->Start();
			}
			else // Not the first time clicking this item
			{
				// Simulate double click
				if (m_stopwatch->GetElapsedTimeMs() <= 500)
				{
					m_pathDoubleClicked = item.first;
					m_isInDirectory		= false;
					m_selectionMade		= true;
					(*path)				= m_pathClicked;
					m_stopwatch->Start();
				}
			}

			if (m_style == FileDialog_Style_Basic)
			{		
				if (ImGui::BeginDragDropSource())
				{
					auto texture = GetOrLoadTexture(item.first, m_context);
					ImGui::SetDragDropPayload("tex", &texture, sizeof(texture), ImGuiCond_Once);
					ImGui::EndDragDropSource();
				}
			}
		}
		ImGui::PopID();

		// LABEL
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() - m_itemSize - 16); // move to the left of the thumbnail
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + m_itemSize - 10); // move to the bottom of the thumbnail
		ImGui::PushItemWidth(ImGui::GetColumnWidth());
		
		SetCharArray(&m_labelText[0], FileSystem::GetFileNameFromFilePath(item.first));
		ImGui::InputText("##Temp", m_labelText, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_ReadOnly);

		ImGui::PopItemWidth();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + m_itemSize + 16); // move to the left of the thumbnail
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - m_itemSize + 10); // move to the bottom of the thumbnail

		ImGui::NextColumn();
		index++;
	}
	ImGui::Columns(1);

	if (m_style != FileDialog_Style_Basic)
	{
		ImGui::SetCursorPosY(ImGui::GetWindowSize().y - 35); // move to the bottom of the window
		ImGui::Separator();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f); // move to the bottom of the window
		
		ImGui::PushItemWidth(ImGui::GetWindowSize().x - 235);
		SetCharArray(&m_fileNameText[0], FileSystem::GetFileNameFromFilePath(m_pathClicked));
		ImGui::InputText("##FileName", m_fileNameText, BUFFER_TEXT_DEFAULT);
		ImGui::PopItemWidth();
		
		ImGui::SameLine(); ImGui::Text((m_filter == FileDialog_Filter_All) ? "All (*.*)" : (m_filter == FileDialog_Filter_Model) ? "Model(*.*)" : "Scene (*.scene)");

		ImGui::SameLine(); if (ImGui::Button(m_style == FileDialog_Style_Open ? "Open" : m_style == FileDialog_Style_Load ? "Load" : "Save"))
		{
			if (m_style != FileDialog_Style_Save)
			{
				(*path) = m_pathClicked;
			}
			else
			{
				(*path) = &m_fileNameText[0];
			}
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

	ViewPath(m_pathDoubleClicked);

	return m_selectionMade;
}

void FileDialog::ViewPath(const string& pathDoubleClicked)
{
	if (pathDoubleClicked.empty() || m_isInDirectory)
		return;

	// Directory
	if (FileSystem::IsDirectory(pathDoubleClicked))
	{
		m_isInDirectory = NavigateToDirectory(pathDoubleClicked);
	}
	// File
	else
	{

	}
}

bool FileDialog::NavigateToDirectory(const string& pathDoubleClicked)
{
	if (!FileSystem::IsDirectory(pathDoubleClicked))
	{
		LOG_WARNING("FileDialog: Can't navigate to directory, provided directory is invalid.");
		return false;
	}

	if (m_pathVisible == pathDoubleClicked && !m_directoryContents.empty())
		return true;

	m_pathVisible = pathDoubleClicked;
	m_directoryContents.clear();

	// Get directories
	vector<string> childDirectories = FileSystem::GetDirectoriesInDirectory(m_pathVisible);
	for (const auto& childDir : childDirectories)
	{
		m_directoryContents[childDir] = Icon_Folder;
	}

	// Get files (based on filter)
	vector<string> childFiles;
	if (m_filter == FileDialog_Filter_All)
	{
		childFiles = FileSystem::GetFilesInDirectory(m_pathVisible);
		for (const auto& childFile : childFiles)
		{
			m_directoryContents[childFile] = Icon_File_Default;
		}
	}
	else if (m_filter == FileDialog_Filter_Scene)
	{
		childFiles = FileSystem::GetSupportedSceneFilesInDirectory(m_pathVisible);
		for (const auto& childFile : childFiles)
		{
			m_directoryContents[childFile] = Icon_File_Scene;
		}
	}
	else if (m_filter == FileDialog_Filter_Model)
	{
		childFiles = FileSystem::GetSupportedModelFilesInDirectory(m_pathVisible);
		for (const auto& childFile : childFiles)
		{
			m_directoryContents[childFile] = Icon_File_Model;
		}
	}
	
	return true;
}
