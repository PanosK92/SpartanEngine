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

//= INCLUDES =================
#include "DirectusInspector.h"
#include "IO/Log.h"
#include "IO/FileHelper.h"
#include <QMimeData>
//============================

DirectusInspector::DirectusInspector(QWidget *parent) : QWidget(parent)
{

}

void DirectusInspector::SetDirectusCore(DirectusCore* directusCore)
{
    m_directusCore = directusCore;
}

void DirectusInspector::Initialize(QWidget* mainWindow)
{
    m_transform = new DirectusTransform();
    m_transform->Initialize(m_directusCore);

    m_camera = new DirectusCamera();
    m_camera->Initialize(m_directusCore, this, mainWindow);

    m_meshFilter = new DirectusMeshFilter();
    m_meshFilter->Initialize(m_directusCore, this, mainWindow);

    m_meshRenderer = new DirectusMeshRenderer();
    m_meshRenderer->Initialize(m_directusCore, this, mainWindow);

    m_material = new DirectusMaterial();
    m_material->Initialize(m_directusCore, this, mainWindow);

    m_rigidBody = new DirectusRigidBody();
    m_rigidBody->Initialize(m_directusCore, this, mainWindow);

    m_collider = new DirectusCollider();
    m_collider->Initialize(m_directusCore, this, mainWindow);

    m_meshCollider = new DirectusMeshCollider();
    m_meshCollider->Initialize(m_directusCore, this, mainWindow);

    m_light = new DirectusLight();
    m_light->Initialize(m_directusCore, this, mainWindow);

    m_script = new DirectusScript();
    m_script->Initialize(m_directusCore, this, mainWindow);

    // Make the components to stack nicely below each other
    // instead of spreading to fill the entire space.
    this->layout()->setAlignment(Qt::AlignTop);

    this->layout()->addWidget(m_transform);
    this->layout()->addWidget(m_camera);
    this->layout()->addWidget(m_meshFilter);
    this->layout()->addWidget(m_meshRenderer);
    this->layout()->addWidget(m_material); 
    this->layout()->addWidget(m_rigidBody);
    this->layout()->addWidget(m_collider);
    this->layout()->addWidget(m_meshCollider);
    this->layout()->addWidget(m_light);
    this->layout()->addWidget(m_script);

    m_initialized = true;
}

GameObject* DirectusInspector::GetInspectedGameObject()
{
    return m_inspectedGameObject;
}

void DirectusInspector::paintEvent(QPaintEvent* evt)
{
    // Has to be overriden for QSS to take affect
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void DirectusInspector::Inspect(GameObject* gameobject)
{
    if (!m_initialized)
        return;

    m_inspectedGameObject = gameobject;

    if (gameobject)
    {    
        m_transform->Reflect(gameobject);
        m_camera->Reflect(gameobject);
        m_meshFilter->Reflect(gameobject);
        m_meshRenderer->Reflect(gameobject);
        m_material->Reflect(gameobject);
        m_rigidBody->Reflect(gameobject);
        m_collider->Reflect(gameobject);
        m_meshCollider->Reflect(gameobject);
        m_light->Reflect(gameobject);
        m_script->Reflect(gameobject);   
    }
    else // NOTE: If no item is selected, the gameobject will be null
    {
        m_transform->hide();
        m_camera->hide();
        m_meshFilter->hide();
        m_meshRenderer->hide();
        m_material->hide();    
        m_rigidBody->hide();
        m_collider->hide();
        m_meshCollider->hide();
        m_light->hide();
        m_script->hide();    
    }
}

//= DROP ============================================================================
void DirectusInspector::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void DirectusInspector::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void DirectusInspector::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();

    // Get the ID of the file being dragged
    const QMimeData *mime = event->mimeData();
    std::string scriptPath = mime->text().toStdString();

    if (FileHelper::IsSupportedScript(scriptPath) && m_inspectedGameObject)
    {
        // Make the absolute path, relative
        scriptPath = FileHelper::GetRelativePathFromAbsolutePath(scriptPath);

        // Add a script component and load the script
        Script* scriptComp = m_inspectedGameObject->AddComponent<Script>();
        scriptComp->AddScript(scriptPath, 0);

        // Update the engine and the inspector (to reflect the changes)
        m_directusCore->Update();
        Inspect(m_inspectedGameObject);
    }
}
//=========================================================================================
