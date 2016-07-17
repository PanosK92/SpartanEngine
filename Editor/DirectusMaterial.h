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

//==============================
#include <QWidget>
#include <QGridLayout>
#include <QLineEdit>
#include "Core/GameObject.h"
#include "Core/Material.h"
#include <QDoubleValidator>
#include "DirectusComboSliderText.h"
#include "DirectusComboLabelText.h"
#include <QPushButton>
#include <QComboBox>
#include "Math/Vector2.h"
#include "DirectusCore.h"
#include "DirectusTexture.h"
//==============================

class DirectusMaterial : public QWidget
{
    Q_OBJECT
public:
    explicit DirectusMaterial(QWidget *parent = 0);
    void Initialize(DirectusCore* directusCore, DirectusInspector* inspector);
    void Reflect(GameObject* gameobject);
private:

    //= TITLE =======================
    QLabel* m_title;
    //===============================

    //= SHADER ======================
    QLabel* m_shaderLabel;
    QComboBox* m_shader;
    //===============================

    //= ALBEDO ======================
    DirectusTexture* m_albedoImage;
    QLabel* m_albedoLabel;
    QPushButton* m_albedoColor;
    //===============================

    //= ROUGHNESS ===================
    DirectusTexture* m_roughnessImage;
    QLabel* m_roughnessLabel;
    DirectusComboSliderText* m_roughness;
    //===============================

    //= METALLIC ====================
    DirectusTexture* m_metallicImage;
    QLabel* m_metallicLabel;
    DirectusComboSliderText* m_metallic;
    //===============================

    //= NORMAL ======================
    DirectusTexture* m_normalImage;
    QLabel* m_normalLabel;
    DirectusComboSliderText* m_normal;
    //===============================

    //= HEIGHT ======================
    DirectusTexture* m_heightImage;
    QLabel* m_heightLabel;
    DirectusComboSliderText* m_height;
    //===============================

    //= OCCLUSION ===================
    DirectusTexture* m_occlusionImage;
    QLabel* m_occlusionLabel;
    //===============================

    //= EMISSION ====================
    DirectusTexture* m_emissionImage;
    QLabel* m_emissionLabel;
    //===============================

    //= MASK ========================
    DirectusTexture* m_maskImage;
    QLabel* m_maskLabel;
    //===============================

    //= REFLECTIVITY ================
    QLabel* m_reflectivityLabel;
    DirectusComboSliderText* m_reflectivity;
    //===============================

    //= TILING ======================
    QLabel* m_tilingLabel;
    DirectusComboLabelText* m_tilingX;
    DirectusComboLabelText* m_tilingY;
    //===============================

    //= OFFSET ======================
    QLabel* m_offsetLabel;
    DirectusComboLabelText* m_offsetX;
    DirectusComboLabelText* m_offsetY;
    //===============================

    //= LINE ========================
    QWidget* m_line;
    //===============================

    //= MISC ========================
    QGridLayout* m_gridLayout;
    QValidator* m_validator;
    Material* m_inspectedMaterial;
    DirectusCore* m_directusCore;
    DirectusInspector* m_inspector;
    //===============================

    void ReflectName();
    void ReflectAlbedo();
    void ReflectRoughness();
    void ReflectMetallic();
    void ReflectNormal();
    void ReflectHeight();
    void ReflectOcclusion();
    void ReflectEmission();
    void ReflectMask();
    void ReflectReflectivity();
    void ReflectTiling();

public slots:
    void MapAlbedo();
    void MapRoughness();
    void MapMetallic();
    void MapNormal();
    void MapHeight();
    void MapOcclusion();
    void MapEmission();
    void MapMask();
    void MapReflectivity();
    void MapTiling();
};
