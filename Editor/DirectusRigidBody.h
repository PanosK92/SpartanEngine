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

//=================================
#include <QWidget>
#include <QGridLayout>
#include "DirectusComboLabelText.h"
#include "Core/GameObject.h"
#include "Components/RigidBody.h"
#include <QCheckBox>
#include "DirectusCore.h"
#include "QComboBox.h"
//==================================

class DirectusInspector;

class DirectusRigidBody : public QWidget
{
    Q_OBJECT
public:
    explicit DirectusRigidBody(QWidget *parent = 0);
    void Initialize(DirectusCore* directusCore, DirectusInspector* inspector);
    void Reflect(GameObject* gameobject);
private:

    //= TITLE ============================
    QWidget* m_image;
    QLabel* m_title;
    //====================================

    //= MASS =============================
    DirectusComboLabelText* m_mass;
    //====================================

    //= DRAG =============================
    DirectusComboLabelText* m_drag;
    //====================================

    //= ANGULAR DRAG =====================
    DirectusComboLabelText* m_angularDrag;
    //====================================

    //= USE GRAVITY  =====================
    QLabel* m_useGravityLabel;
    QCheckBox* m_useGravity;
    //====================================

    //= IS KINEMATIC  ====================
    QLabel* m_isKinematicLabel;
    QCheckBox* m_isKinematic;
    //====================================

    //= CONSTRAINTS  =====================
    QLabel* m_freezePosLabel;
    QLabel* m_freezePosXLabel;
    QLabel* m_freezePosYLabel;
    QLabel* m_freezePosZLabel;
    QCheckBox* m_freezePosX;
    QCheckBox* m_freezePosY;
    QCheckBox* m_freezePosZ;

    QLabel* m_freezeRotLabel;
    QLabel* m_freezeRotXLabel;
    QLabel* m_freezeRotYLabel;
    QLabel* m_freezeRotZLabel;
    QCheckBox* m_freezeRotX;
    QCheckBox* m_freezeRotY;
    QCheckBox* m_freezeRotZ;
    //====================================

    //= LINE =============================
    QWidget* m_line;
    //====================================

    //= MISC =============================
    QGridLayout* m_gridLayout;
    QValidator* m_validator;
    RigidBody* m_inspectedRigidBody;
    DirectusCore* m_directusCore;
    DirectusInspector* m_inspector;
    //====================================

    void ReflectMass();
    void ReflectDrag();
    void ReflectAngulaDrag();
    void ReflectUseGravity();
    void ReflectIsKinematic();
    void ReflectFreezePosition();
    void ReflectFreezeRotation();

public slots:
    void MapMass();
    void MapDrag();
    void MapAngularDrag();
    void MapUseGravity();
    void MapIsKinematic();
    void MapFreezePosition();
    void MapFreezeRotation();
};
