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
#include "DirectusRigidBody.h"
//============================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

DirectusRigidBody::DirectusRigidBody(QWidget* parent): QWidget(parent)
{
    m_directusCore = nullptr;
    m_inspector = nullptr;
}

void DirectusRigidBody::Initialize(DirectusCore* directusCore, DirectusInspector* inspector)
{
    m_directusCore = directusCore;
    m_inspector = inspector;

    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);

    //= TITLE =================================================
    m_title = new QLabel("RigidBody");
    m_title->setStyleSheet(
                "background-image: url(:/Images/rigidBody.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 20px;"
                );
    //=========================================================

    //= LINE ======================================
    m_line = new QWidget();
    m_line->setFixedHeight(1);
    m_line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_line->setStyleSheet(QString("background-color: #585858;"));
    //=============================================

    // addWidget(widget, row, column, rowspan, colspan)
    //= GRID ==================================================
    int row = 0;

    // Row 0
    m_gridLayout->addWidget(m_title, 0, 0, 1, 1);
    row++;

    // Row 4 - LINE
    m_gridLayout->addWidget(m_line, row, 0, 1, 1);
    //==============================================================================

    //connect(m_shapeType,    SIGNAL(currentIndexChanged(int)),   this, SLOT(MapType()));
    //connect(m_centerX,      SIGNAL(ValueChanged()),             this, SLOT(MapCenter()));
    //connect(m_centerY,      SIGNAL(ValueChanged()),             this, SLOT(MapCenter()));
    //connect(m_centerZ,      SIGNAL(ValueChanged()),             this, SLOT(MapCenter()));
    //connect(m_sizeX,        SIGNAL(ValueChanged()),             this, SLOT(MapSize()));
    //connect(m_sizeY,        SIGNAL(ValueChanged()),             this, SLOT(MapSize()));
    //connect(m_sizeZ,        SIGNAL(ValueChanged()),             this, SLOT(MapSize()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusRigidBody::Reflect(GameObject* gameobject)
{
    m_inspectedRigidBody = nullptr;

    // Catch evil case
    if (!gameobject)
    {
        this->hide();
        return;
    }

    m_inspectedRigidBody = gameobject->GetComponent<RigidBody>();
    if (!m_inspectedRigidBody)
    {
        this->hide();
        return;
    }

    // Do the actual reflection
    //ReflectType(m_inspectedCollider->GetShapeType());
    //ReflectCenter(m_inspectedCollider->GetCenter());
    //ReflectSize(m_inspectedCollider->GetBoundingBox());

    // Make this widget visible
    this->show();
}

void DirectusRigidBody::ReflectMass(float mass)
{

}

void DirectusRigidBody::ReflectDrag(float drag)
{

}

void DirectusRigidBody::ReflectAngulaDrag(float angularDrag)
{

}

void DirectusRigidBody::ReflectUseGravity(bool useGravity)
{

}

void DirectusRigidBody::ReflectIsKinematic(bool useGravity)
{

}

void DirectusRigidBody::ReflectFreezePosition(Directus::Math::Vector3 posFreeze)
{

}

void DirectusRigidBody::ReflectFreezeRotation(Directus::Math::Vector3 rotFreeze)
{

}

void DirectusRigidBody::MapMass()
{

}

void DirectusRigidBody::MapDrag()
{

}

void DirectusRigidBody::MapAngularDrag()
{

}

void DirectusRigidBody::MapUseGravity()
{

}

void DirectusRigidBody::MapIsKinematic()
{

}

void DirectusRigidBody::MapFreezePosition()
{

}

void DirectusRigidBody::MapFreezeRotation()
{

}
