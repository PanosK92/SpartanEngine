/*
Copyright(c) 2016-2017 Panos Karabelas

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

//======================================
#include <QWidget>
#include <QGridLayout>
#include "DirectusAdjustLabel.h"
#include <QLineEdit>
#include "Core/GameObject.h"
#include <QDoubleValidator>
#include "Components/MeshRenderer.h"
#include <QCheckBox>
#include "DirectusDropDownButton.h"
#include "DirectusCore.h"
#include "DirectusMaterialDropTarget.h"
#include "DirectusIComponent.h"
//=====================================

class DirectusInspector;
class DirectusMaterial;

class DirectusMeshRenderer : public DirectusIComponent
{
    Q_OBJECT
public:
    DirectusMeshRenderer();

    virtual void Initialize(DirectusInspector* inspector, QWidget* mainWindow);
    virtual void Reflect(GameObject* gameobject);

private:
    //= CAST SHADOWS =====================
    QLabel* m_castShadowsLabel;
    QCheckBox* m_castShadowsCheckBox;
    //====================================

    //= RECEIVE SHADOWS ==================
    QLabel* m_receiveShadowsLabel;
    QCheckBox* m_receiveShadowsCheckBox;
    //====================================

    //= MATERIAL =========================
    QLabel* m_materialLabel;
    DirectusMaterialDropTarget* m_material;
    //====================================

    //= MISC =============================
    QValidator* m_validator;
    MeshRenderer* m_inspectedMeshRenderer;
    DirectusMaterial* m_materialUIComp;
    //====================================

    void ReflectCastShadows();
    void ReflectReceiveShadows();
    void ReflectMaterial();

 public slots:
    void DoMaterialInspCompReflection();

public slots:
    void MapCastShadows();
    void MapReceiveShadows();
    virtual void Remove();
};
