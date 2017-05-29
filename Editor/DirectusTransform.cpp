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

//===============================
#include "DirectusTransform.h"
#include "Components/Transform.h"
#include "Logging/Log.h"
#include "Core/Settings.h"
#include "Components/RigidBody.h"
//===============================

//= NAMESPACES ================
using namespace std;
using namespace Directus;
using namespace Directus::Math;
//=============================

DirectusTransform::DirectusTransform()
{

}

void DirectusTransform::Initialize(DirectusInspector* inspector, QWidget* mainWindow)
{
    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);

    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    //= TITLE =====================================
    m_title = new QLabel("Transform");
    m_title->setStyleSheet(
                "background-image: url(:/Images/transform.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 20px;"
                );
    //============================================

    // = POSITION =================================
    m_posLabel = new QLabel("Position");

    m_posX = new DirectusComboLabelText();
    m_posX->Initialize("X");

    m_posY = new DirectusComboLabelText();
    m_posY->Initialize("Y");

    m_posZ = new DirectusComboLabelText();
    m_posZ->Initialize("Z");
    //=============================================

    //= ROTATION ==================================
    m_rotLabel = new QLabel("Rotation");

    m_rotX = new DirectusComboLabelText();
    m_rotX->Initialize("X");

    m_rotY = new DirectusComboLabelText();
    m_rotY->Initialize("Y");

    m_rotZ = new DirectusComboLabelText();
    m_rotZ->Initialize("Z");
    //=============================================

    //= SCALE =====================================
    m_scaLabel = new QLabel("Scale");

    m_scaX = new DirectusComboLabelText();
    m_scaX->Initialize("X");

    m_scaY = new DirectusComboLabelText();
    m_scaY->Initialize("Y");

    m_scaZ = new DirectusComboLabelText();
    m_scaZ->Initialize("Z");
    //=============================================

    //= LINE ======================================
    m_line = new QWidget();
    m_line->setFixedHeight(1);
    m_line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_line->setStyleSheet(QString("background-color: #585858;"));
    //=============================================

    // addWidget(widget, row, column, rowspan, colspan)
    //= GRID ==================================================
    // Row 0 - TITLE
    m_gridLayout->addWidget(m_title,                        0, 0, 1, 7);

    // Row 1 - POSITION
    m_gridLayout->addWidget(m_posLabel,                     1, 0, 1, 1);
    m_gridLayout->addWidget(m_posX->GetLabelWidget(),       1, 1, 1, 1);
    m_gridLayout->addWidget(m_posX->GetTextWidget(),        1, 2, 1, 1);
    m_gridLayout->addWidget(m_posY->GetLabelWidget(),       1, 3, 1, 1);
    m_gridLayout->addWidget(m_posY->GetTextWidget(),        1, 4, 1, 1);
    m_gridLayout->addWidget(m_posZ->GetLabelWidget(),       1, 5, 1, 1);
    m_gridLayout->addWidget(m_posZ->GetTextWidget(),        1, 6, 1, 1);

    // Row 2 - ROTATION
    m_gridLayout->addWidget(m_rotLabel,                     2, 0, 1, 1);
    m_gridLayout->addWidget(m_rotX->GetLabelWidget(),       2, 1, 1, 1);
    m_gridLayout->addWidget(m_rotX->GetTextWidget(),        2, 2, 1, 1);
    m_gridLayout->addWidget(m_rotY->GetLabelWidget(),       2, 3, 1, 1);
    m_gridLayout->addWidget(m_rotY->GetTextWidget(),        2, 4, 1, 1);
    m_gridLayout->addWidget(m_rotZ->GetLabelWidget(),       2, 5, 1, 1);
    m_gridLayout->addWidget(m_rotZ->GetTextWidget(),        2, 6, 1, 1);

    // Row 3 - SCALE
    m_gridLayout->addWidget(m_scaLabel,                     3, 0, 1, 1);
    m_gridLayout->addWidget(m_scaX->GetLabelWidget(),       3, 1, 1, 1);
    m_gridLayout->addWidget(m_scaX->GetTextWidget(),        3, 2, 1, 1);
    m_gridLayout->addWidget(m_scaY->GetLabelWidget(),       3, 3, 1, 1);
    m_gridLayout->addWidget(m_scaY->GetTextWidget(),        3, 4, 1, 1);
    m_gridLayout->addWidget(m_scaZ->GetLabelWidget(),       3, 5, 1, 1);
    m_gridLayout->addWidget(m_scaZ->GetTextWidget(),        3, 6, 1, 1);

    // Row 4 - LINE
    m_gridLayout->addWidget(m_line,                         4, 0, 1, 7);

    // textChanged(QString) -> emits signal when changed through code
    // textEdited(QString) -> doesn't emit signal when changed through code
    connect(m_posX, SIGNAL(ValueChanged()), this, SLOT(MapPosition()));
    connect(m_posY, SIGNAL(ValueChanged()), this, SLOT(MapPosition()));
    connect(m_posZ, SIGNAL(ValueChanged()), this, SLOT(MapPosition()));
    connect(m_rotX, SIGNAL(ValueChanged()), this, SLOT(MapRotation()));
    connect(m_rotY, SIGNAL(ValueChanged()), this, SLOT(MapRotation()));
    connect(m_rotZ, SIGNAL(ValueChanged()), this, SLOT(MapRotation()));
    connect(m_scaX, SIGNAL(ValueChanged()), this, SLOT(MapScale()));
    connect(m_scaY, SIGNAL(ValueChanged()), this, SLOT(MapScale()));
    connect(m_scaZ, SIGNAL(ValueChanged()), this, SLOT(MapScale()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusTransform::Reflect(weak_ptr<GameObject> gameobject)
{
    m_inspectedTransform = nullptr;

    // Catch the evil case
    if (gameobject.expired())
    {
        this->hide();
        return;
    }

    // Catch the seed of the evil case
    m_inspectedTransform = gameobject.lock()->GetTransform();
    if (!m_inspectedTransform)
    {
        this->hide();
        return;
    }

    // Do the actual reflection
    ReflectPosition();
    ReflectRotation();
    ReflectScale();

    // Make this widget visible
    this->show();
}

void DirectusTransform::Refresh()
{
    // Do the actual reflection
    ReflectPosition();
    ReflectRotation();
    ReflectScale();
}

void DirectusTransform::ReflectPosition()
{
    if (!m_inspectedTransform)
        return;

    Vector3 pos = m_inspectedTransform->GetPositionLocal();
    m_posX->SetFromFloat(pos.x);
    m_posY->SetFromFloat(pos.y);
    m_posZ->SetFromFloat(pos.z);
}

void DirectusTransform::ReflectRotation()
{
    if (!m_inspectedTransform)
        return;

    Vector3 rot = m_inspectedTransform->GetRotationLocal().ToEulerAngles();
    m_rotX->SetFromFloat(rot.x);
    m_rotY->SetFromFloat(rot.y);
    m_rotZ->SetFromFloat(rot.z);
}

void DirectusTransform::ReflectScale()
{
    if (!m_inspectedTransform)
        return;

    Vector3 sca = m_inspectedTransform->GetScaleLocal();
    m_scaX->SetFromFloat(sca.x);
    m_scaY->SetFromFloat(sca.y);
    m_scaZ->SetFromFloat(sca.z);
}

void DirectusTransform::MapPosition()
{
    if (!m_inspectedTransform)
        return;

    float x = m_posX->GetAsFloat();
    float y = m_posY->GetAsFloat();
    float z = m_posZ->GetAsFloat();
    Vector3 pos(x,y,z);

    // Update the transform
    m_inspectedTransform->SetPositionLocal(pos);

    // Get the GameObject this transform is attached to
    auto gameObj = m_inspectedTransform->g_gameObject;
    if (gameObj.expired())
        return;

    // Update the rigidBody
    auto rigidBody = gameObj.lock()->GetComponent<RigidBody>();
    if (rigidBody)
    {
        rigidBody->SetPosition(pos);
    }
}

void DirectusTransform::MapRotation()
{
    if (!m_inspectedTransform)
        return;

    float x = m_rotX->GetAsFloat();
    float y = m_rotY->GetAsFloat();
    float z = m_rotZ->GetAsFloat();
    Quaternion rot = Quaternion::FromEulerAngles(Vector3(x,y,z));

    // Update the transform
    m_inspectedTransform->SetRotationLocal(rot);

    // Get the GameObject this transform is attached to
    auto gameObj = m_inspectedTransform->g_gameObject;
    if (gameObj.expired())
        return;

    // Update the rigidBody (if it exists)
    auto rigidBody = gameObj.lock()->GetComponent<RigidBody>();
    if (rigidBody)
    {
        rigidBody->SetRotation(rot);
    }
}

void DirectusTransform::MapScale()
{
    if (!m_inspectedTransform)
        return;

    float x = m_scaX->GetAsFloat();
    float y = m_scaY->GetAsFloat();
    float z = m_scaZ->GetAsFloat();
    m_inspectedTransform->SetScaleLocal(Vector3(x,y,z));
}
