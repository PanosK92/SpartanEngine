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
    void Clear();
    void AddRoot(QTreeWidgetItem* item);
    void AddChild(QTreeWidgetItem* parent, QTreeWidgetItem* child);
    void AddGameObject(GameObject* gameobject, QTreeWidgetItem *parent);
    GameObject* GetSelectedGameObject();
    bool IsAnyGameObjectSelected();
	QTreeWidgetItem* GameObjectToQTreeItem(GameObject* gameobject);
  
	QString m_sceneFileName;	
	Socket* m_socket;

signals:

public slots:
     void Populate();
     void CreateEmptyGameObject();
     void NewScene();
     void OpenScene();
     void SaveScene();
     void SaveSceneAs();
};
