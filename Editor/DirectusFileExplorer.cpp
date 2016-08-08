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

//= INCLUDES ====================
#include "DirectusFileExplorer.h"
#include <QStandardItem>
#include <QMouseEvent>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include "IO/Log.h"
//===============================

DirectusFileExplorer::DirectusFileExplorer(QWidget *parent) : QListView(parent)
{

}

void DirectusFileExplorer::Initialize()
{
    QString root = "Assets";

    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setFilter(QDir::Files | QDir::AllDirs | QDir::NoDotAndDotDot); // Set a filter that displays only folders
    m_fileModel->setRootPath(root);  // Set the root path

    // Set icon provider
    m_directusIconProvider = new DirectusIconProvider();
    m_directusIconProvider->Initialize();
    m_fileModel->setIconProvider(m_directusIconProvider);

    // Set the model to the tree view
    this->setModel(m_fileModel);

    // I must set the path manually as the tree view
    // (at least visually) refuses to change anything.
    QModelIndex index = m_fileModel->index("Assets");
    this->setRootIndex(index);
}

void DirectusFileExplorer::SetRootPath(QString path)
{
    this->setRootIndex(m_fileModel->setRootPath(path));
}

//= DRAG N DROP RELATED ============================================================================
void DirectusFileExplorer::mousePressEvent(QMouseEvent* event)
{
    // In case this mouse press evolves into a drag and drop
    // we have to keep the starting position in order to determine
    // if it's indeed one, in mouseMoveEvent(QMouseEvent* event)
    if (event->button() == Qt::LeftButton)
              m_dragStartPosition = event->pos();

    QListView::mousePressEvent(event);
}

// Determine whether a drag should begin, and
// construct a drag object to handle the operation.
void DirectusFileExplorer::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton))
            return;

    if ((event->pos() - m_dragStartPosition).manhattanLength() < QApplication::startDragDistance())
            return;

    QModelIndexList selectedItems = this->selectionModel()->selectedIndexes();
    if (selectedItems.empty())
        return;

    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;

    QString filePath = m_fileModel->fileInfo(selectedItems[0]).absoluteFilePath();
    mimeData->setText(filePath);
    drag->setMimeData(mimeData);

    drag->exec();
}
//===================================================================================================
