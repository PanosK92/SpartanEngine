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

//== INCLUDES ======================
#include "DirectusMaterial.h"
#include "Components/MeshRenderer.h"
#include "IO/Log.h"
#include <QByteArray>
//==================================

//= NAMESPACES =====================
using namespace Directus::Math;
using namespace std;
//==================================

DirectusMaterial::DirectusMaterial(QWidget *parent) : QWidget(parent)
{
    m_directusCore = nullptr;
}

void DirectusMaterial::Initialize(DirectusCore* directusCore, DirectusInspector* inspector)
{
    m_directusCore = directusCore;
    m_inspector = inspector;

    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    //= TITLE =================================================
    m_title = new QLabel("Material");
    m_title->setStyleSheet(
                "background-image: url(:/Images/material.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 40px;"
                );
    //=========================================================

    //= SHADER ================================================
    m_shaderLabel = new QLabel("Shader");
    m_shader = new QComboBox();
    m_shader->addItem("Default");
    //=========================================================

    //= ALBEDO ================================================
    m_albedoLabel = new QLabel("Albedo");
    m_albedoImage = new DirectusTexture();
    m_albedoImage->Initialize(m_directusCore, inspector, Albedo);
    m_albedoColor = new QPushButton("ColorPicker");
    //=========================================================

    //= ROUGHNESS =============================================
    m_roughnessLabel = new QLabel("Roughness");
    m_roughnessImage = new DirectusTexture();
    m_roughnessImage->Initialize(m_directusCore, inspector, Roughness);
    m_roughness = new DirectusComboSliderText();
    m_roughness->Initialize(0, 1);
    //=========================================================

    //= METALLIC ==============================================
    m_metallicLabel = new QLabel("Metallic");
    m_metallicImage = new DirectusTexture();
    m_metallicImage->Initialize(m_directusCore, inspector, Metallic);
    m_metallic = new DirectusComboSliderText();
    m_metallic->Initialize(0, 1);
    //=========================================================

    //= NORMAL ================================================
    m_normalLabel = new QLabel("Normal");
    m_normalImage = new DirectusTexture();
    m_normalImage->Initialize(m_directusCore, inspector, Normal);
    m_normal = new DirectusComboSliderText();
    m_normal->Initialize(0, 1);
    //=========================================================

    //= HEIGHT ================================================
    m_heightLabel = new QLabel("Height");
    m_heightImage = new DirectusTexture();
    m_heightImage->Initialize(m_directusCore, inspector, Height);
    m_height = new DirectusComboSliderText();
    m_height->Initialize(0, 1);
    //=========================================================

    //= OCCLUSION =============================================
    m_occlusionLabel = new QLabel("Occlusion");
    m_occlusionImage = new DirectusTexture();
    m_occlusionImage->Initialize(m_directusCore, inspector, Occlusion);
    //=========================================================

    //= EMISSION ==============================================
    m_emissionLabel = new QLabel("Emission");
    m_emissionImage = new DirectusTexture();
    m_emissionImage->Initialize(m_directusCore, inspector, Emission);
    //=========================================================

    //= MASK ==================================================
    m_maskLabel = new QLabel("Mask");
    m_maskImage = new DirectusTexture();
    m_maskImage->Initialize(m_directusCore, inspector, Mask);
    //=========================================================

    //= REFLECTIVITY  =========================================
    m_reflectivityLabel = new QLabel("Reflectivity");
    m_reflectivity = new DirectusComboSliderText();
    m_reflectivity->Initialize(0, 1);
    //=========================================================

    //= TILING ================================================
    m_tilingLabel = new QLabel("Tiling");

    m_tilingX = new DirectusComboLabelText();
    m_tilingX->Initialize("X");

    m_tilingY = new DirectusComboLabelText();
    m_tilingY->Initialize("X");
    //=========================================================

    //= OFFSET ================================================
    m_offsetLabel = new QLabel("Offset");

    m_offsetX = new DirectusComboLabelText();
    m_offsetX->Initialize("X");

    m_offsetY = new DirectusComboLabelText();
    m_offsetY->Initialize("X");
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
    m_gridLayout->addWidget(m_title, 0, 0, 1, 5);
    row++;

    // Row 5 - SHADER
    m_gridLayout->addWidget(m_shaderLabel,  row, 0, 1, 1);
    m_gridLayout->addWidget(m_shader,       row, 1, 1, 5);
    row++;

    // Row 6 - ALBEDO
    m_gridLayout->addWidget(m_albedoImage, row, 0, 1, 1);
    m_gridLayout->addWidget(m_albedoLabel, row, 1, 1, 1);
    m_gridLayout->addWidget(m_albedoColor, row, 2, 1, 3);
    row++;

    // Row 7 - ROUGHNESS
    m_gridLayout->addWidget(m_roughnessImage,           row, 0, 1, 1);
    m_gridLayout->addWidget(m_roughnessLabel,           row, 1, 1, 1);
    m_gridLayout->addWidget(m_roughness->GetSlider(),   row, 2, 1, 2);
    m_gridLayout->addWidget(m_roughness->GetLineEdit(), row, 4, 1, 1);
    row++;

    // Row 8 - METALLIC
    m_gridLayout->addWidget(m_metallicImage,            row, 0, 1, 1);
    m_gridLayout->addWidget(m_metallicLabel,            row, 1, 1, 1);
    m_gridLayout->addWidget(m_metallic->GetSlider(),    row, 2, 1, 2);
    m_gridLayout->addWidget(m_metallic->GetLineEdit(),  row, 4, 1, 1);
    row++;

    // Row 9 - NORMAL
    m_gridLayout->addWidget(m_normalImage,              row, 0, 1, 1);
    m_gridLayout->addWidget(m_normalLabel,              row, 1, 1, 1);
    m_gridLayout->addWidget(m_normal->GetSlider(),      row, 2, 1, 2);
    m_gridLayout->addWidget(m_normal->GetLineEdit(),    row, 4, 1, 1);
    row++;

    // Row 10 - HEIGHT
    m_gridLayout->addWidget(m_heightImage,              row, 0, 1, 1);
    m_gridLayout->addWidget(m_heightLabel,              row, 1, 1, 1);
    m_gridLayout->addWidget(m_height->GetSlider(),      row, 2, 1, 2);
    m_gridLayout->addWidget(m_height->GetLineEdit(),    row, 4, 1, 1);
    row++;

    // Row 11 - OCCLUSION
    m_gridLayout->addWidget(m_occlusionImage, row, 0, 1, 1);
    m_gridLayout->addWidget(m_occlusionLabel, row, 1, 1, 1);
    row++;

    // Row 12 - EMISSION
    m_gridLayout->addWidget(m_emissionImage, row, 0, 1, 1);
    m_gridLayout->addWidget(m_emissionLabel, row, 1, 1, 1);
    row++;

    // Row 13 - MASK
    m_gridLayout->addWidget(m_maskImage, row, 0, 1, 1);
    m_gridLayout->addWidget(m_maskLabel, row, 1, 1, 1);
    row++;

    // Row 14 - REFLECTIVITY
    m_gridLayout->addWidget(m_reflectivityLabel,            row, 0, 1, 1);
    m_gridLayout->addWidget(m_reflectivity->GetSlider(),    row, 2, 1, 2);
    m_gridLayout->addWidget(m_reflectivity->GetLineEdit(),  row, 4, 1, 1);
    row++;

    // Row 15 - TILING
    m_gridLayout->addWidget(m_tilingLabel,      row, 0, 1, 1);
    m_gridLayout->addWidget(m_tilingX->GetLabelWidget(),     row, 1, 1, 1);
    m_gridLayout->addWidget(m_tilingX->GetTextWidget(),          row, 2, 1, 1);
    m_gridLayout->addWidget(m_tilingY->GetLabelWidget(),     row, 3, 1, 1);
    m_gridLayout->addWidget(m_tilingY->GetTextWidget(),          row, 4, 1, 1);
    row++;

    // Row 16 - OFFSET
    m_gridLayout->addWidget(m_offsetLabel,  row, 0, 1, 1);
    m_gridLayout->addWidget(m_offsetX->GetLabelWidget(), row, 1, 1, 1);
    m_gridLayout->addWidget(m_offsetX->GetTextWidget(),      row, 2, 1, 1);
    m_gridLayout->addWidget(m_offsetY->GetLabelWidget(), row, 3, 1, 1);
    m_gridLayout->addWidget(m_offsetY->GetTextWidget(),      row, 4, 1, 1);
    row++;

    // Row 17 - LINE
    m_gridLayout->addWidget(m_line, row, 0, 1, 5);
    //=========================================================

    connect(m_roughness,    SIGNAL(ValueChanged()), this, SLOT(MapRoughness()));
    connect(m_metallic,     SIGNAL(ValueChanged()), this, SLOT(MapMetallic()));
    connect(m_normal,       SIGNAL(ValueChanged()), this, SLOT(MapNormal()));
    connect(m_height,       SIGNAL(ValueChanged()), this, SLOT(MapHeight()));
    connect(m_reflectivity, SIGNAL(ValueChanged()), this, SLOT(MapReflectivity()));
    connect(m_tilingX,      SIGNAL(ValueChanged()), this, SLOT(MapTiling()));
    connect(m_tilingY,      SIGNAL(ValueChanged()), this, SLOT(MapTiling()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusMaterial::Reflect(GameObject* gameobject)
{
    m_inspectedMaterial = nullptr;

    // Catch evil case
    if (!gameobject)
    {
        this->hide();
        return;
    }

    MeshRenderer* meshRenderer = gameobject->GetComponent<MeshRenderer>();
    if (!meshRenderer)
    {
        this->hide();
        return;
    }

    m_inspectedMaterial = meshRenderer->GetMaterial();
    if (!m_inspectedMaterial)
    {
        this->hide();
        return;
    }

    // Do the actual reflection
    SetName(m_inspectedMaterial->GetName());
    SetAlbedo(m_inspectedMaterial->GetColorAlbedo());
    SetRoughness(m_inspectedMaterial->GetRoughness());
    SetMetallic(m_inspectedMaterial->GetMetallic());
    SetNormal(m_inspectedMaterial->GetNormalStrength());
    SetHeight(0);
    SetTiling(m_inspectedMaterial->GetTiling());
    SetReflectivity(m_inspectedMaterial->GetReflectivity());

    // Make this widget visible
    this->show();
}

void DirectusMaterial::SetName(string name)
{
    QString text = QString::fromStdString("Material - " + name);
    m_title->setText(text);
}

void DirectusMaterial::SetAlbedo(Vector4 color)
{
    // Load the albedo texture preview
    string texPath = m_inspectedMaterial->GetTexturePathByType(TextureType::Albedo);
    m_albedoImage->LoadImageAsync(texPath);
}

void DirectusMaterial::SetRoughness(float roughness)
{
    m_roughness->SetValue(roughness);

    // Load the roughness texture preview
    string texPath = m_inspectedMaterial->GetTexturePathByType(TextureType::Roughness);
    m_roughnessImage->LoadImageAsync(texPath);
}

void DirectusMaterial::SetMetallic(float metallic)
{
    m_metallic->SetValue(metallic);

    // Load the metallic texture preview
    string texPath = m_inspectedMaterial->GetTexturePathByType(TextureType::Metallic);
    m_metallicImage->LoadImageAsync(texPath);
}

void DirectusMaterial::SetNormal(float normal)
{
    m_normal->SetValue(normal);

    // Load the normal texture preview
    string texPath = m_inspectedMaterial->GetTexturePathByType(TextureType::Normal);
    m_normalImage->LoadImageAsync(texPath);
}

void DirectusMaterial::SetHeight(float height)
{
    m_height->SetValue(height);

    // Load the height texture preview
    string texPath = m_inspectedMaterial->GetTexturePathByType(TextureType::Height);
    m_heightImage->LoadImageAsync(texPath);
}

void DirectusMaterial::SetOcclusion()
{
    // Load the occlusion texture preview
    string texPath = m_inspectedMaterial->GetTexturePathByType(TextureType::Occlusion);
    m_occlusionImage->LoadImageAsync(texPath);
}

void DirectusMaterial::SetEmission()
{
    // Load the emission texture preview
    string texPath = m_inspectedMaterial->GetTexturePathByType(TextureType::Emission);
    m_emissionImage->LoadImageAsync(texPath);
}

void DirectusMaterial::SetMask()
{
    // Load the mask texture preview
    string texPath = m_inspectedMaterial->GetTexturePathByType(TextureType::Mask);
    m_maskImage->LoadImageAsync(texPath);
}

void DirectusMaterial::SetReflectivity(float reflectivity)
{
    m_reflectivity->SetValue(reflectivity);
}

void DirectusMaterial::SetTiling(Vector2 tiling)
{
    m_tilingX->SetFromFloat(tiling.x);
    m_tilingY->SetFromFloat(tiling.y);
}

void DirectusMaterial::MapAlbedo()
{

}

void DirectusMaterial::MapRoughness()
{
    if (!m_inspectedMaterial || !m_directusCore)
        return;

    float roughness =  m_roughness->GetValue();
    m_inspectedMaterial->SetRoughness(roughness);
    m_directusCore->Update();
}


void DirectusMaterial::MapMetallic()
{
    if (!m_inspectedMaterial || !m_directusCore)
        return;

    float metallic =  m_metallic->GetValue();
    m_inspectedMaterial->SetMetallic(metallic);
    m_directusCore->Update();
}

void DirectusMaterial::MapNormal()
{
    if (!m_inspectedMaterial || !m_directusCore)
        return;

    float normal =  m_normal->GetValue();
    m_inspectedMaterial->SetNormalStrength(normal);
    m_directusCore->Update();
}

void DirectusMaterial::MapHeight()
{
    if (!m_inspectedMaterial || !m_directusCore)
        return;

    //float height =  m_height->GetValue();
    //m_inspectedMaterial->Seth(height);
    //m_directusCore->Update();
}

void DirectusMaterial::MapOcclusion()
{

}

void DirectusMaterial::MapEmission()
{

}

void DirectusMaterial::MapMask()
{

}

void DirectusMaterial::MapReflectivity()
{
    if (!m_inspectedMaterial || !m_directusCore)
        return;

    float reflectivity = m_reflectivity->GetValue();
    m_inspectedMaterial->SetReflectivity(reflectivity);
    m_directusCore->Update();
}

void DirectusMaterial::MapTiling()
{
    if (!m_inspectedMaterial || !m_directusCore)
        return;

    Vector2 tiling;
    tiling.x = m_tilingX->GetAsFloat();
    tiling.y = m_tilingY->GetAsFloat();

    m_inspectedMaterial->SetTiling(tiling);
    m_directusCore->Update();
}
