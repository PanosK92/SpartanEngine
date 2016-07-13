/*
Copyright(c) 2016 Panos Karabelas

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

//==============================
#include "DirectusDirExplorer.h"
#include "IO/Log.h"
//==============================

DirectusDirExplorer::DirectusDirExplorer(QWidget *parent) : QTreeView(parent)
{
    QString rootPath = "Assets";
    m_fileExplorer = nullptr;
    m_dirModel = new QFileSystemModel(this);

    // Set a filter that displays only folders
    m_dirModel->setFilter(QDir::NoDotAndDotDot | QDir::AllDirs);

    // Set the root path
    m_dirModel->setRootPath(rootPath);

    // Set the model to the list view
    this->setModel(m_dirModel);

    // I must set the path manually as the tree view
    // (at least visually) refuses to change anything.
    QModelIndex index = m_dirModel->index(rootPath);
    this->setRootIndex(index);
}

void DirectusDirExplorer::SetFileExplorer(DirectusFileExplorer* fileExplorer)
{
    m_fileExplorer = fileExplorer;
}

void DirectusDirExplorer::UpdateFileExplorer(QModelIndex index)
{
    QString path = m_dirModel->fileInfo(index).absoluteFilePath();

    if (m_fileExplorer)
        m_fileExplorer->SetRootPath(path);
}
