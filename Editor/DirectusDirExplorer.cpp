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
#include "Logging/Log.h"
//==============================

DirectusDirExplorer::DirectusDirExplorer(QWidget *parent) : QTreeView(parent)
{
    QString rootPath = "Assets";
    m_fileExplorer = nullptr;

    m_dirModel = new QFileSystemModel(this);
    m_dirModel->setFilter(QDir::NoDotAndDotDot | QDir::AllDirs);  // Set a filter that displays only folders
    m_dirModel->setRootPath(rootPath);  // Set the root path

    // Set icon provider
    m_iconProvider = new DirectusIconProvider();
    m_iconProvider->Initialize();
    m_dirModel->setIconProvider(m_iconProvider);

    // Set the model to the list view
    this->setModel(m_dirModel);

    // I must set the path manually as the tree view
    // (at least visually) refuses to change anything.
    QModelIndex index = m_dirModel->index(rootPath);
    this->setRootIndex(index);

    // Hide Size, Type, Date Modified columns
    this->setColumnHidden(1, true);
    this->setColumnHidden(2, true);
    this->setColumnHidden(3, true);
}

void DirectusDirExplorer::Initialize(DirectusFileExplorer* fileExplorer)
{
    m_fileExplorer = fileExplorer;
    connect(m_fileExplorer, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(UpdateFromFileExplorer(QModelIndex)));
}

void DirectusDirExplorer::UpdateFileExplorer(QModelIndex index)
{
    QString path = m_dirModel->fileInfo(index).absoluteFilePath();

    if (m_fileExplorer)
    {
        m_fileExplorer->SetRootPath(path);
    }
}

void DirectusDirExplorer::UpdateFromFileExplorer(QModelIndex index)
{
    QString path = m_fileExplorer->GetFileSystemModel()->filePath(index);

    this->scrollTo(m_dirModel->index(path), QAbstractItemView::PositionAtBottom);
}
