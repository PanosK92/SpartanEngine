//==============================
#include "DirectusDirExplorer.h"
#include "IO/Log.h"
//==============================

DirectusDirExplorer::DirectusDirExplorer(QWidget *parent) : QTreeView(parent)
{
    m_fileExplorer = nullptr;

    QString root = "C:/";
    m_dirModel = new QFileSystemModel(this);

    // Set a filter that displays only folders
    m_dirModel->setFilter(QDir::NoDotAndDotDot | QDir::AllDirs);

    // Set the root path
    m_dirModel->setRootPath(root);

    // Set the model to the list view
    this->setModel(m_dirModel);
}

void DirectusDirExplorer::SetFileExplorer(DirectusFileExplorer* fileExplorer)
{
    m_fileExplorer = fileExplorer;
}

void DirectusDirExplorer::UpdateFileExplorer(QModelIndex index)
{
    QString path = m_dirModel->fileInfo(index).absolutePath();

    if (m_fileExplorer)
    {
        LOG("Path send!");
        m_fileExplorer->SetRootPath(path);
    }
}
