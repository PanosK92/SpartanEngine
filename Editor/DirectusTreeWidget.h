#pragma once

//= INCLUDES ===========
#include <QTreeWidget>
#include <QtCore>
#include "Core/Socket.h"
#include <QVariant>
#include "QMouseEvent"
//======================

class DirectusTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit DirectusTreeWidget(QWidget* parent = 0);
    void SetEngineSocket(Socket* socket);
    virtual void mousePressEvent(QMouseEvent *event);

private:
    Socket* m_socket;
    void Clear();
    QTreeWidgetItem* GameObjectToQTreeItem(GameObject* gameobject);
    void AddRoot(QTreeWidgetItem* item);
    void AddChild(QTreeWidgetItem* parent, QTreeWidgetItem* child);
    void AddGameObject(GameObject* gameobject, QTreeWidgetItem *parent);
    GameObject* GetSelectedGameObject();
    bool IsAnyGameObjectSelected();
    QString m_sceneFileName;

signals:

public slots:
     void Populate();
     void CreateEmptyGameObject();
     void NewScene();
     void OpenScene();
     void SaveScene();
     void SaveSceneAs();
};
