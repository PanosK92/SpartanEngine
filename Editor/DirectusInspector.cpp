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

//= INCLUDES =====================
#include "DirectusInspector.h"
#include <QMimeData>
#include "Logging/Log.h"
#include "FileSystem/FileSystem.h"
#include "Components/Transform.h"
#include "Core/GameObject.h"
#include "DirectusTransform.h"
#include "DirectusCamera.h"
#include "DirectusMeshRenderer.h"
#include "DirectusMaterial.h"
#include "DirectusCollider.h"
#include "DirectusRigidBody.h"
#include "DirectusLight.h"
#include "DirectusScript.h"
#include "DirectusMeshFilter.h"
#include "DirectusMeshCollider.h"
#include "DirectusViewport.h"
#include "DirectusAudioSource.h"
#include "DirectusAudioListener.h"
#include "DirectusIComponent.h"
//================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

DirectusInspector::DirectusInspector(QWidget *parent) : QWidget(parent)
{

}

void DirectusInspector::SetDirectusCore(DirectusViewport* directusViewport)
{
    m_directusViewport = directusViewport;
}

void DirectusInspector::Initialize(QWidget* mainWindow)
{
    m_mainWindow = mainWindow;

    m_transform = new DirectusTransform();
    m_transform->Initialize(this, mainWindow);

    m_camera = new DirectusCamera();
    m_camera->Initialize(this, mainWindow);

    m_meshFilter = new DirectusMeshFilter();
    m_meshFilter->Initialize(this, mainWindow);

    m_material = new DirectusMaterial();
    m_material->Initialize(this, mainWindow);

    m_meshRenderer = new DirectusMeshRenderer();
    m_meshRenderer->Initialize(this, mainWindow);

    m_rigidBody = new DirectusRigidBody();
    m_rigidBody->Initialize(this, mainWindow);

    m_collider = new DirectusCollider();
    m_collider->Initialize(this, mainWindow);

    m_meshCollider = new DirectusMeshCollider();
    m_meshCollider->Initialize(this, mainWindow);

    m_light = new DirectusLight();
    m_light->Initialize(this, mainWindow);

    m_scripts.push_back(new DirectusScript());
    m_scripts[0]->Initialize(this, mainWindow);

    m_audioSource = new DirectusAudioSource();
    m_audioSource->Initialize(this, mainWindow);

    m_audioListener = new DirectusAudioListener();
    m_audioListener->Initialize(this, mainWindow);

    // Make the components to stack nicely below each other
    // instead of spreading to fill the entire space.
    this->layout()->setAlignment(Qt::AlignTop);

    this->layout()->addWidget(m_transform);
    this->layout()->addWidget(m_camera);
    this->layout()->addWidget(m_meshFilter);
    this->layout()->addWidget(m_meshRenderer);   
    this->layout()->addWidget(m_rigidBody);
    this->layout()->addWidget(m_collider);
    this->layout()->addWidget(m_meshCollider);
    this->layout()->addWidget(m_light);
    this->layout()->addWidget(m_scripts[0]);
    this->layout()->addWidget(m_material);
    this->layout()->addWidget(m_audioSource);
    this->layout()->addWidget(m_audioListener);

    m_initialized = true;
}

weak_ptr<GameObject> DirectusInspector::GetInspectedGameObject()
{
    return m_inspectedGameObject;
}

void DirectusInspector::Clear()
{
    Inspect(weak_ptr<GameObject>());
}

void DirectusInspector::InspectMaterialFile(const string& filepath)
{
    Clear();

    DirectusMaterial* material = (DirectusMaterial*)m_material;
    material->ReflectFile(filepath);
}

Context* DirectusInspector::GetContext()
{
    if (!m_directusViewport)
        return nullptr;

    return m_directusViewport->GetEngineContext();
}

DirectusMaterial* DirectusInspector::GetMaterialComponent()
{
    return (DirectusMaterial*)m_material;
}

void DirectusInspector::paintEvent(QPaintEvent* evt)
{
    // Has to be overriden for QSS to take affect
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void DirectusInspector::Inspect(weak_ptr<GameObject> gameobject)
{
    if (!m_initialized)
        return;

    m_inspectedGameObject = gameobject;

    // Make sure we have at least as many script widgets
    // as the GameObject has script components.
    vector<Script*> engineScripts = FitScriptVectorToGameObject();

    if (!gameobject.expired())
    {    
        m_transform->Reflect(gameobject);
        m_camera->Reflect(gameobject);
        m_meshFilter->Reflect(gameobject);
        m_meshRenderer->Reflect(gameobject);
        m_rigidBody->Reflect(gameobject);
        m_collider->Reflect(gameobject);
        m_meshCollider->Reflect(gameobject);
        m_light->Reflect(gameobject);
        m_material->Reflect(gameobject);
        m_audioSource->Reflect(gameobject);
        m_audioListener->Reflect(gameobject);

        for (int i = 0; i < m_scripts.size(); i++)
        {
            DirectusScript* scriptComp = (DirectusScript*)m_scripts[i];
            scriptComp->Reflect(engineScripts[i]);
        }
    }
    else // NOTE: If no item is selected, the gameobject will be null
    {
        m_transform->hide();
        m_camera->hide();
        m_meshFilter->hide();
        m_meshRenderer->hide();     
        m_rigidBody->hide();
        m_collider->hide();
        m_meshCollider->hide();
        m_light->hide();     
        m_material->hide();
        m_audioSource->hide();
        m_audioListener->hide();

        for (int i = 0; i < m_scripts.size(); i++)
             m_scripts[i]->hide();
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
    string scriptPath = mime->text().toStdString();

    if (FileSystem::IsEngineScriptFile(scriptPath) && !m_inspectedGameObject.expired())
    {
        // Make the absolute path, relative
        scriptPath = FileSystem::GetRelativeFilePath(scriptPath);

        // Add a script component and load the script
        Script* scriptComp = m_inspectedGameObject.lock()->AddComponent<Script>();
        scriptComp->AddScript(scriptPath);

        // Update the inspector
        Inspect(m_inspectedGameObject);
    }
}
//===================================================================================

//= HELPER FUNCTIONS  ===============================================================
vector<Script*> DirectusInspector::FitScriptVectorToGameObject()
{
    if (m_inspectedGameObject.expired())
        return vector<Script*>();

    // Clear current script vector
    int scriptCount = (int)m_scripts.size();
    for (int i = 0; i < scriptCount; i++)
    {
       QWidget* widget = m_scripts[i];
       this->layout()->removeWidget(widget);
       delete widget;
    }
    m_scripts.clear();
    m_scripts.shrink_to_fit();

    // Reflect back to the script vector
    auto engineScripts = m_inspectedGameObject._Get()->GetComponents<Script>();
    for (int i = 0; i < (int)engineScripts.size(); i++)
    {
        m_scripts.push_back(new DirectusScript());
        m_scripts.back()->Initialize(this, m_mainWindow);
        this->layout()->addWidget(m_scripts.back());
    }

    return engineScripts;
}
//===================================================================================
