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

//==========================
#include <QWidget>
#include <QGridLayout>
#include <QLineEdit>
#include <QLabel>
#include "Math/Vector3.h"
#include "Math/Quaternion.h"
#include "Core/GameObject.h"
//==========================

class DirectusTransform : public QWidget
{
    Q_OBJECT
public:
    explicit DirectusTransform(QWidget *parent = 0);

    void Map(GameObject* gameobject);
    Directus::Math::Vector3 GetPosition();
    void SetPosition(Directus::Math::Vector3 pos);

    Directus::Math::Quaternion GetRotation();
    void SetRotation(Directus::Math::Quaternion rot);

    Directus::Math::Vector3 GetScale();
    void SetScale(Directus::Math::Vector3 sca);

private:
    QGridLayout* m_gridLayout;

    QWidget* m_image;
    QLabel* m_transTitle;

    // = POSITION ===========
    QLabel* m_transPosLabel;
    QLabel* m_transPosXLabel;
    QLineEdit* m_transPosX;
    QLabel* m_transPosYLabel;
    QLineEdit* m_transPosY;
    QLabel* m_transPosZLabel;
    QLineEdit* m_transPosZ;
    //=======================

    //= ROTATION ============
    QLabel* m_transRotLabel;
    QLabel* m_transRotXLabel;
    QLineEdit* m_transRotX;
    QLabel* m_transRotYLabel;
    QLineEdit* m_transRotY;
    QLabel* m_transRotZLabel;
    QLineEdit* m_transRotZ;
    //=======================

    //= SCALE ===============
    QLabel* m_transScaLabel;
    QLabel* m_transScaXLabel;
    QLineEdit* m_transScaX;
    QLabel* m_transScaYLabel;
    QLineEdit* m_transScaY;
    QLabel* m_transScaZLabel;
    QLineEdit* m_transScaZ;
    //=======================
};
