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
#include "IO/Log.h"
//===============================

using namespace Directus::Math;

DirectusTransform::DirectusTransform(QWidget *parent) : QWidget(parent)
{
    m_gridLayout = new QGridLayout();
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    m_image = new QWidget(this);
    m_image->setStyleSheet("background-image: url(:/Images/transform.png); background-repeat: no-repeat; background-position: left;");
    m_transTitle = new QLabel("Transform");

    // = POSITION =================================
    m_transPosLabel = new QLabel("Position");
    m_transPosXLabel = new QLabel("X");
    m_transPosX = CreateQLineEdit();
    m_transPosYLabel = new QLabel("Y");
    m_transPosY = CreateQLineEdit();
    m_transPosZLabel = new QLabel("Z");
    m_transPosZ = CreateQLineEdit();
    //=============================================

    //= ROTATION ==================================
    m_transRotLabel = new QLabel("Rotation");
    m_transRotXLabel = new QLabel("X");
    m_transRotX = CreateQLineEdit();
    m_transRotYLabel = new QLabel("Y");
    m_transRotY = CreateQLineEdit();
    m_transRotZLabel = new QLabel("Z");
    m_transRotZ = CreateQLineEdit();
    //=============================================

    //= SCALE =====================================
    m_transScaLabel = new QLabel("Scale");
    m_transScaXLabel = new QLabel("X");
    m_transScaX = CreateQLineEdit();
    m_transScaYLabel = new QLabel("Y");
    m_transScaY = CreateQLineEdit();
    m_transScaZLabel = new QLabel("Z");
    m_transScaZ = CreateQLineEdit();
    //=============================================

    // addWidget(*Widget, row, column, rowspan, colspan)

    // 0th row
    m_gridLayout->addWidget(m_image, 0, 0, 1, 1);
    m_gridLayout->addWidget(m_transTitle, 0, 1, 1, 2);

    // 1st row
    m_gridLayout->addWidget(m_transPosLabel,    1, 0, 1, 1);
    m_gridLayout->addWidget(m_transPosXLabel,   1, 1, 1, 1);
    m_gridLayout->addWidget(m_transPosX,        1, 2, 1, 1);
    m_gridLayout->addWidget(m_transPosYLabel,   1, 3, 1, 1);
    m_gridLayout->addWidget(m_transPosY,        1, 4, 1, 1);
    m_gridLayout->addWidget(m_transPosZLabel,   1, 5, 1, 1);
    m_gridLayout->addWidget(m_transPosZ,        1, 6, 1, 1);

    // 2nd row
    m_gridLayout->addWidget(m_transRotLabel,    2, 0, 1, 1);
    m_gridLayout->addWidget(m_transRotXLabel,   2, 1, 1, 1);
    m_gridLayout->addWidget(m_transRotX,        2, 2, 1, 1);
    m_gridLayout->addWidget(m_transRotYLabel,   2, 3, 1, 1);
    m_gridLayout->addWidget(m_transRotY,        2, 4, 1, 1);
    m_gridLayout->addWidget(m_transRotZLabel,   2, 5, 1, 1);
    m_gridLayout->addWidget(m_transRotZ,        2, 6, 1, 1);

    // 3rd row
    m_gridLayout->addWidget(m_transScaLabel,    3, 0, 1, 1);
    m_gridLayout->addWidget(m_transScaXLabel,   3, 1, 1, 1);
    m_gridLayout->addWidget(m_transScaX,        3, 2, 1, 1);
    m_gridLayout->addWidget(m_transScaYLabel,   3, 3, 1, 1);
    m_gridLayout->addWidget(m_transScaY,        3, 4, 1, 1);
    m_gridLayout->addWidget(m_transScaZLabel,   3, 5, 1, 1);
    m_gridLayout->addWidget(m_transScaZ,        3, 6, 1, 1);

    this->setParent(parent);
    this->setLayout(m_gridLayout);

    // This is not a mistake, it helps get the widget
    // fully initialized
    this->show();
    this->hide();
}

void DirectusTransform::Map(GameObject* gameobject)
{
    // Catch evil case
    if (!gameobject)
    {
        this->hide();
        return;
    }

    // Catch the seed of the evil
    Transform* transform = gameobject->GetTransform();
    if (!transform)
    {
        this->hide();
        return;
    }

    // Do the actual mapping
    SetPosition(transform->GetPositionLocal());
    SetRotation(transform->GetRotationLocal());
    SetScale(transform->GetScaleLocal());

    // Make this widget visible
    this->show();
}

Vector3 DirectusTransform::GetPosition()
{
    float x = m_transPosX->text().toFloat();
    float y = m_transPosY->text().toFloat();
    float z = m_transPosZ->text().toFloat();

    return Vector3(x, y, z);
}

void DirectusTransform::SetPosition(Vector3 pos)
{
    m_transPosX->setText(QString::number(pos.x));
    m_transPosY->setText(QString::number(pos.y));
    m_transPosZ->setText(QString::number(pos.z));
}

Quaternion DirectusTransform::GetRotation()
{
    float x = m_transRotX->text().toFloat();
    float y = m_transRotY->text().toFloat();
    float z = m_transRotZ->text().toFloat();

    return Quaternion::FromEulerAngles(x, y, z);
}

void DirectusTransform::SetRotation(Quaternion rot)
{
    Vector3 rotEuler = rot.ToEulerAngles();
    m_transRotX->setText(QString::number(rotEuler.x));
    m_transRotY->setText(QString::number(rotEuler.y));
    m_transRotZ->setText(QString::number(rotEuler.z));
}

Vector3 DirectusTransform::GetScale()
{
    float x = m_transScaX->text().toFloat();
    float y = m_transScaY->text().toFloat();
    float z = m_transScaZ->text().toFloat();

    return Vector3(x, y, z);
}

void DirectusTransform::SetScale(Vector3 sca)
{
    m_transScaX->setText(QString::number(sca.x));
    m_transScaY->setText(QString::number(sca.y));
    m_transScaZ->setText(QString::number(sca.z));
}

QLineEdit* DirectusTransform::CreateQLineEdit()
{
    QLineEdit* lineEdit = new QLineEdit();
    lineEdit->setValidator(m_validator);

    return lineEdit;
}
