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
//============================

DirectusInspector::DirectusInspector(QWidget *parent) : QWidget(parent)
{

}

void DirectusInspector::SetDirectusCore(DirectusCore* directusCore)
{
    m_directusCore = directusCore;
}

void DirectusInspector::Initialize()
{
    m_transform = new DirectusTransform();
    m_transform->Initialize(m_directusCore);

    m_camera = new DirectusCamera();
    m_camera->Initialize(m_directusCore);

    m_meshRenderer = new DirectusMeshRenderer();
    m_meshRenderer->Initialize();

    m_material = new DirectusMaterial();
    m_material->Initialize(m_directusCore);

    m_mesh = new DirectusMesh();
    m_mesh->Initialize();

    m_rigidBody = new DirectusRigidBody();
    m_rigidBody->Initialize();

    m_collider = new DirectusCollider();
    m_collider->Initialize();

    m_light = new DirectusLight();
    m_light->Initialize(m_directusCore);

    m_script = new DirectusScript();
    m_script->Initialize(m_directusCore);

    m_meshCollider = new DirectusMeshCollider();
    m_meshCollider->Initialize();;

    this->layout()->addWidget(m_transform);
    this->layout()->addWidget(m_camera);
    this->layout()->addWidget(m_meshRenderer);
    this->layout()->addWidget(m_material);
    this->layout()->addWidget(m_mesh);
    this->layout()->addWidget(m_rigidBody);
    this->layout()->addWidget(m_collider);
    this->layout()->addWidget(m_light);
    this->layout()->addWidget(m_script);
    this->layout()->addWidget(m_meshCollider);
}

void DirectusInspector::paintEvent(QPaintEvent* evt)
{
    // Has to be overriden for QSS to take affect
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void DirectusInspector::inspect(GameObject* gameobject)
{
    if (gameobject)
    {    
        m_transform->Reflect(gameobject);
        m_camera->Reflect(gameobject);
        m_meshRenderer->Reflect(gameobject);
        m_material->Reflect(gameobject);
        m_mesh->Reflect(gameobject);
        m_rigidBody->Reflect(gameobject);
        m_collider->Reflect(gameobject);
        m_light->Reflect(gameobject);
        m_script->Reflect(gameobject);
        m_meshCollider->Reflect(gameobject);
    }
    else // NOTE: If no item is selected, the gameobject will be null
    {
        m_transform->hide();
        m_camera->hide();
        m_meshRenderer->hide();
        m_material->hide();    
        m_mesh->hide();
        m_rigidBody->hide();
        m_collider->hide();
        m_light->hide();
        m_script->hide();
        m_meshCollider->hide();
    }
}
