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
#include "DirectusAdjustLabel.h"
#include <QLineEdit>
#include "Core/GameObject.h"
#include "Core/Material.h"
#include <QDoubleValidator>
#include "DirectusSliderText.h"
#include <QPushButton>
#include <QComboBox>
#include "Math/Vector2.h"
#include "DirectusCore.h"
#include "DirectusImage.h"
//==============================

class DirectusMaterial : public QWidget
{
    Q_OBJECT
public:
    explicit DirectusMaterial(QWidget *parent = 0);
    void Initialize(DirectusCore* directusCore);
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
    DirectusImage* m_albedoImage;
    QLabel* m_albedoLabel;
    QPushButton* m_albedoColor;
    //===============================

    //= ROUGHNESS ===================
    DirectusImage* m_roughnessImage;
    QLabel* m_roughnessLabel;
    DirectusSliderText* m_roughness;
    //===============================

    //= METALLIC ====================
    DirectusImage* m_metallicImage;
    QLabel* m_metallicLabel;
    DirectusSliderText* m_metallic;
    //===============================

    //= NORMAL ======================
    DirectusImage* m_normalImage;
    QLabel* m_normalLabel;
    DirectusSliderText* m_normal;
    //===============================

    //= HEIGHT ======================
    DirectusImage* m_heightImage;
    QLabel* m_heightLabel;
    DirectusSliderText* m_height;
    //===============================

    //= OCCLUSION ===================
    DirectusImage* m_occlusionImage;
    QLabel* m_occlusionLabel;
    //===============================

    //= EMISSION ====================
    DirectusImage* m_emissionImage;
    QLabel* m_emissionLabel;
    //===============================

    //= MASK ========================
    DirectusImage* m_maskImage;
    QLabel* m_maskLabel;
    //===============================

    //= REFLECTIVITY ================
    QLabel* m_reflectivityLabel;
    DirectusSliderText* m_reflectivity;
    //===============================

    //= TILING ======================
    QLabel* m_tilingLabel;
    DirectusAdjustLabel* m_tilingXLabel;
    DirectusAdjustLabel* m_tilingYLabel;
    QLineEdit* m_tilingX;
    QLineEdit* m_tilingY;
    //===============================

    //= OFFSET ======================
    QLabel* m_offsetLabel;
    DirectusAdjustLabel* m_offsetXLabel;
    DirectusAdjustLabel* m_offsetYLabel;
    QLineEdit* m_offsetX;
    QLineEdit* m_offsetY;
    //===============================

    //= MISC ========================
    QGridLayout* m_gridLayout;
    QValidator* m_validator;
    Material* m_inspectedMaterial;
    DirectusCore* m_directusCore;
    //===============================

    QLineEdit* CreateQLineEdit();

    void SetName(std::string name);
    void SetAlbedo(Directus::Math::Vector4 color);
    void SetRoughness(float roughness);
    void SetMetallic(float metallic);
    void SetNormal(float normal);
    void SetHeight(float height);
    void SetOcclusion();
    void SetEmission();
    void SetMask();
    void SetReflectivity(float reflectivity);
    void SetTiling(Directus::Math::Vector2 tiling);

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
