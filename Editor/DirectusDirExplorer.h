#pragma once

//= INCLUDES ====================
#include <QTreeView>
#include <QFileSystemModel>
#include "DirectusFileExplorer.h"
//===============================

class DirectusDirExplorer : public QTreeView
{
    Q_OBJECT
public:
    explicit DirectusDirExplorer(QWidget *parent = 0);
    void SetFileExplorer(DirectusFileExplorer* fileExplorer);
private:
    QFileSystemModel* m_dirModel;
    DirectusFileExplorer* m_fileExplorer;
signals:

public slots:
    void UpdateFileExplorer(QModelIndex index);
};
