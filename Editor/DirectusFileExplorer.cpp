//= INCLUDES ====================
#include "DirectusFileExplorer.h"
//===============================

DirectusFileExplorer::DirectusFileExplorer(QWidget *parent) : QListView(parent)
{
    QString root = "Assets/";
    m_fileModel = new QFileSystemModel(this);

    // Set a filter that displays only folders
    m_fileModel->setFilter(QDir::NoDotAndDotDot | QDir::Files);

    // Set the root path
    m_fileModel->setRootPath(root);

    // Set the model to the tree view
    this->setModel(m_fileModel);
}

void DirectusFileExplorer::SetRootPath(QString path)
{
     this->setRootIndex(m_fileModel->setRootPath(path));
}
