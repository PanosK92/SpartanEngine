#pragma once

//= INCLUDES ===========
#include <QTreeWidget>
#include "Core/Socket.h"
//======================

class DirectusTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit DirectusTreeWidget(QWidget* parent = 0);
    void SetEngineSocket(Socket* socket);

private:
    Socket* m_socket;
    void Clear();

signals:

public slots:
     void Populate();
     void CreateEmptyGameObject();
     void NewScene();
     void OpenScene();
     void SaveScene();
     void SaveSceneAs();
};
