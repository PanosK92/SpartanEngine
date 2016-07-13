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
void DirectusInspector::Initialize()
{
    m_transform = new DirectusTransform();
    m_transform->Initialize();

    m_camera = new DirectusCamera();
    m_camera->Initialize();

    m_meshRenderer = new DirectusMeshRenderer();
    m_meshRenderer->Initialize();

    m_material = new DirectusMaterial();
    m_material->Initialize();

    this->layout()->addWidget(m_transform);
    this->layout()->addWidget(m_camera);
    this->layout()->addWidget(m_meshRenderer);
    this->layout()->addWidget(m_material);
}

void DirectusInspector::inspect(GameObject* gameobject)
{
    if (gameobject)
    {    
        m_transform->Reflect(gameobject);
        m_camera->Reflect(gameobject);
        m_meshRenderer->Reflect(gameobject);
        m_material->Reflect(gameobject);
    }
    else // NOTE: If no item is selected, the gameobject will be null
    {
        m_transform->hide();
        m_camera->hide();
        m_meshRenderer->hide();
        m_material->hide();
    }
}
