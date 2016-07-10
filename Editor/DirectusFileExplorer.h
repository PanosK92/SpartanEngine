#pragma once

// INCLUDES ===============
#include <QListView>
#include <QFileSystemModel>
//=========================

class DirectusFileExplorer : public QListView
{
    Q_OBJECT
public:
    explicit DirectusFileExplorer(QWidget *parent = 0);
    void SetRootPath(QString path);

private:
    QFileSystemModel* m_fileModel;

signals:

public slots:

};
