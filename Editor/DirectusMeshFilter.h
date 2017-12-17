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

//==================================
#include <QWidget>
#include <QGridLayout>
#include "DirectusComboSliderText.h"
#include <QPushButton>
#include "Scene/GameObject.h"
#include <QDoubleValidator>
#include "Components/MeshFilter.h"
#include <QComboBox>
#include "DirectusViewport.h"
#include <QLabel>
#include "Math/Vector4.h"
#include "DirectusDropDownButton.h"
#include "DirectusIComponent.h"
//==================================

class DirectusInspector;

class DirectusMeshFilter : public DirectusIComponent
{
    Q_OBJECT
public:
    DirectusMeshFilter();

    virtual void Initialize(DirectusInspector* inspector, QWidget* mainWindow);
    virtual void Reflect(std::weak_ptr<Directus::GameObject> gameobject);

private:
    //= MESH =============================
    QLabel* m_meshLabel;
    QLineEdit* m_mesh;
    //====================================

    //= MISC =========================
    QValidator* m_validator;
    Directus::MeshFilter* m_inspectedMeshFilter;
    //================================

    void ReflectMesh();

public slots:
    void MapMesh();
    virtual void Remove();
};
