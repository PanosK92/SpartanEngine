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

//=============================
#include "DirectusIComponent.h"
#include <QWidget>
#include <QGridLayout>
#include <QCheckBox>
#include <QComboBox.h>
#include <QLabel>
#include <memory>
//============================

class DirectusInspector;
class DirectusComboLabelText;
namespace Directus
{
    class GameObject;
    class RigidBody;
}

class DirectusRigidBody : public DirectusIComponent
{
    Q_OBJECT
public:
    DirectusRigidBody();

    virtual void Initialize(DirectusInspector* inspector, QWidget* mainWindow);
    virtual void Reflect(std::weak_ptr<Directus::GameObject> gameobject);

private:
    //= MASS =============================
    DirectusComboLabelText* m_mass;
    //====================================

    //= DRAG =============================
    DirectusComboLabelText* m_drag;
    //====================================

    //= ANGULAR DRAG =====================
    DirectusComboLabelText* m_angularDrag;
    //====================================

    //= RESTITUTION ======================
    DirectusComboLabelText* m_restitution;
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

    //= MISC =======================
    QValidator* m_validator;
    Directus::RigidBody* m_inspectedRigidBody;
    //==============================

    //= REFLECTION =======================
    void ReflectMass();
    void ReflectDrag();
    void ReflectAngulaDrag();
    void ReflectRestitution();
    void ReflectUseGravity();
    void ReflectIsKinematic();
    void ReflectFreezePosition();
    void ReflectFreezeRotation();
     //====================================

    //= MISC ================
    void SetSizeMinAlignmentRight(QLabel* label, QCheckBox* checkBox);
    //=======================
public slots:
    //= MAPPING =============
    void MapMass();
    void MapDrag();
    void MapAngularDrag();
    void MapRestitution();
    void MapUseGravity();
    void MapIsKinematic();
    void MapFreezePosition();
    void MapFreezeRotation();
    virtual void Remove();
    //=======================
};
