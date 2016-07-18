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

#pragma once

//= INCLUDES =======================
#include <QTreeWidget>
#include <QtCore>
#include "Core/Socket.h"
#include <QVariant>
#include <QMouseEvent>
#include "DirectusInspector.h"
#include "DirectusCore.h"
//==================================

class DirectusHierarchy : public QTreeWidget
{
    Q_OBJECT
public:
    explicit DirectusHierarchy(QWidget* parent = 0);
    void SetDirectusCore(DirectusCore* directusCore);
    void Initialize(DirectusInspector* inspector, QWidget* mainWindow);
    virtual void mousePressEvent(QMouseEvent *event);
    virtual void selectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    virtual void mouseMoveEvent(QMouseEvent* event);
    virtual void dragEnterEvent(QDragEnterEvent* event);
    virtual void dragMoveEvent (QDragMoveEvent* event);
    virtual void dropEvent(QDropEvent* event);

private:

    void AddRoot(QTreeWidgetItem* item);
    void AddChild(QTreeWidgetItem* parent, QTreeWidgetItem* child);
    void AddGameObject(GameObject* gameobject, QTreeWidgetItem *parent);

    QTreeWidgetItem* GameObjectToTreeItem(GameObject* gameobject);
    GameObject* TreeItemToGameObject(QTreeWidgetItem* treeItem);
    QTreeWidgetItem* GetSelectedItem();
    GameObject* GetSelectedGameObject();

    bool IsAnyGameObjectSelected();   
  
	QString m_sceneFileName;	
	Socket* m_socket;
    DirectusInspector* m_inspector;
    DirectusCore* m_directusCore;
    QWidget* m_mainWindow;
    QPoint m_dragStartPosition;

public slots:
    void Clear();
    void Populate();
    void CreateEmptyGameObject();
    void NewScene();
    void OpenScene();
    void SaveScene();
    void SaveSceneAs();
    void LoadModel();
    void ShowContextMenu(const QPoint& pos);

signals:
    void SceneLoadingStarted();
    void ModelLoadingStarted();
};
