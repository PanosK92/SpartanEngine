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

//== INCLUDES =======================================
#include "DirectusMaterial.h"
#include "DirectusInspector.h"
#include "DirectusAdjustLabel.h"
#include "DirectusComboSliderText.h"
#include "DirectusComboLabelText.h"
#include "DirectusColorPicker.h"
#include <QByteArray>
#include "Core/GameObject.h"
#include "Logging/Log.h"
#include "Components/MeshRenderer.h"
#include "Math/Vector2.h"
#include "DirectusViewport.h"
#include "DirectusMaterialTextureDropTarget.h"
#include "Resource/ResourceManager.h"
#include "Graphics/DeferredShaders/ShaderVariation.h"
//===================================================

//= NAMESPACES =====================
using namespace Directus;
using namespace Directus::Math;
using namespace std;
//==================================

DirectusMaterial::DirectusMaterial()
{

}

void DirectusMaterial::Initialize(DirectusInspector* inspector, QWidget* mainWindow)
{
    m_inspector = inspector;

    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);

    //= TITLE =================================================
    m_title = new QLabel("Material");
    m_title->setStyleSheet(
                "background-image: url(:/Images/material.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 20px;"
                );
    //=========================================================

    //= SHADER ==========================
    m_shaderLabel = new QLabel("Shader");
    //===================================

    //= ALBEDO ================================================
    m_albedoLabel = new QLabel("Albedo");
    m_albedoImage = new DirectusMaterialTextureDropTarget();
    m_albedoImage->Initialize(inspector, TextureType_Albedo);
    m_albedoColor = new DirectusColorPicker();
    m_albedoColor->Initialize(mainWindow);
    //=========================================================

    //= ROUGHNESS =============================================
    m_roughnessLabel = new QLabel("Roughness");
    m_roughnessImage = new DirectusMaterialTextureDropTarget();
    m_roughnessImage->Initialize(inspector, TextureType_Roughness);
    m_roughness = new DirectusComboSliderText();
    m_roughness->Initialize(0, 1);
    //=========================================================

    //= METALLIC ==============================================
    m_metallicLabel = new QLabel("Metallic");
    m_metallicImage = new DirectusMaterialTextureDropTarget();
    m_metallicImage->Initialize(inspector, TextureType_Metallic);
    m_metallic = new DirectusComboSliderText();
    m_metallic->Initialize(0, 1);
    //=========================================================

    //= NORMAL ================================================
    m_normalLabel = new QLabel("Normal");
    m_normalImage = new DirectusMaterialTextureDropTarget();
    m_normalImage->Initialize(inspector, TextureType_Normal);
    m_normal = new DirectusComboSliderText();
    m_normal->Initialize(0, 1);
    //=========================================================

    //= HEIGHT ================================================
    m_heightLabel = new QLabel("Height");
    m_heightImage = new DirectusMaterialTextureDropTarget();
    m_heightImage->Initialize(inspector, TextureType_Height);
    m_height = new DirectusComboSliderText();
    m_height->Initialize(0, 1);
    //=========================================================

    //= OCCLUSION =============================================
    m_occlusionLabel = new QLabel("Occlusion");
    m_occlusionImage = new DirectusMaterialTextureDropTarget();
    m_occlusionImage->Initialize(inspector, TextureType_Occlusion);
    //=========================================================

    //= EMISSION ==============================================
    m_emissionLabel = new QLabel("Emission");
    m_emissionImage = new DirectusMaterialTextureDropTarget();
    m_emissionImage->Initialize(inspector, TextureType_Emission);
    //=========================================================

    //= MASK ==================================================
    m_maskLabel = new QLabel("Mask");
    m_maskImage = new DirectusMaterialTextureDropTarget();
    m_maskImage->Initialize(inspector, TextureType_Mask);
    //=========================================================

    //= TILING ================================================
    m_tilingLabel = new QLabel("Tiling");

    m_tilingX = new DirectusComboLabelText();
    m_tilingX->Initialize("X");

    m_tilingY = new DirectusComboLabelText();
    m_tilingY->Initialize("Y");
    //=========================================================

    //= OFFSET ================================================
    m_offsetLabel = new QLabel("Offset");

    m_offsetX = new DirectusComboLabelText();
    m_offsetX->Initialize("X");

    m_offsetY = new DirectusComboLabelText();
    m_offsetY->Initialize("Y");
    //=========================================================

    //= LINE ======================================
    m_line = new QWidget();
    m_line->setFixedHeight(1);
    m_line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_line->setStyleSheet(QString("background-color: #585858;"));
    //=============================================

    //= GRID ==================================================
    int row = 0;

    // addWidget(widget, row, column, rowspan, colspan)
    // Row 0
    m_gridLayout->addWidget(m_title,        row, 0, 1, 5);
    row++;

    // Row 2 - SHADER
    m_gridLayout->addWidget(m_shaderLabel,  row, 0, 1, 5);
    row++;

    // Row 3 - ALBEDO
    m_gridLayout->addWidget(m_albedoImage, row, 0, 1, 1);
    m_gridLayout->addWidget(m_albedoLabel, row, 1, 1, 1);
    m_gridLayout->addWidget(m_albedoColor->GetWidget(), row, 2, 1, 3);
    row++;

    // Row 4 - ROUGHNESS
    m_gridLayout->addWidget(m_roughnessImage,           row, 0, 1, 1);
    m_gridLayout->addWidget(m_roughnessLabel,           row, 1, 1, 1);
    m_gridLayout->addWidget(m_roughness->GetSlider(),   row, 2, 1, 2);
    m_gridLayout->addWidget(m_roughness->GetLineEdit(), row, 4, 1, 1);
    row++;

    // Row 5 - METALLIC
    m_gridLayout->addWidget(m_metallicImage,            row, 0, 1, 1);
    m_gridLayout->addWidget(m_metallicLabel,            row, 1, 1, 1);
    m_gridLayout->addWidget(m_metallic->GetSlider(),    row, 2, 1, 2);
    m_gridLayout->addWidget(m_metallic->GetLineEdit(),  row, 4, 1, 1);
    row++;

    // Row 6 - NORMAL
    m_gridLayout->addWidget(m_normalImage,              row, 0, 1, 1);
    m_gridLayout->addWidget(m_normalLabel,              row, 1, 1, 1);
    m_gridLayout->addWidget(m_normal->GetSlider(),      row, 2, 1, 2);
    m_gridLayout->addWidget(m_normal->GetLineEdit(),    row, 4, 1, 1);
    row++;

    // Row 7 - HEIGHT
    m_gridLayout->addWidget(m_heightImage,              row, 0, 1, 1);
    m_gridLayout->addWidget(m_heightLabel,              row, 1, 1, 1);
    m_gridLayout->addWidget(m_height->GetSlider(),      row, 2, 1, 2);
    m_gridLayout->addWidget(m_height->GetLineEdit(),    row, 4, 1, 1);
    row++;

    // Row 8 - OCCLUSION
    m_gridLayout->addWidget(m_occlusionImage,           row, 0, 1, 1);
    m_gridLayout->addWidget(m_occlusionLabel,           row, 1, 1, 1);
    row++;

    // Row 9 - EMISSION
    m_gridLayout->addWidget(m_emissionImage, row, 0, 1, 1);
    m_gridLayout->addWidget(m_emissionLabel, row, 1, 1, 1);
    row++;

    // Row 10 - MASK
    m_gridLayout->addWidget(m_maskImage, row, 0, 1, 1);
    m_gridLayout->addWidget(m_maskLabel, row, 1, 1, 1);
    row++;

    // Row 11 - TILING
    m_gridLayout->addWidget(m_tilingLabel,                  row, 0, 1, 1);
    m_gridLayout->addWidget(m_tilingX->GetLabelWidget(),    row, 1, 1, 1, Qt::AlignRight);
    m_gridLayout->addWidget(m_tilingX->GetTextWidget(),     row, 2, 1, 1);
    m_gridLayout->addWidget(m_tilingY->GetLabelWidget(),    row, 3, 1, 1);
    m_gridLayout->addWidget(m_tilingY->GetTextWidget(),     row, 4, 1, 1);
    row++;

    // Row 12 - OFFSET
    m_gridLayout->addWidget(m_offsetLabel,                  row, 0, 1, 1);
    m_gridLayout->addWidget(m_offsetX->GetLabelWidget(),    row, 1, 1, 1, Qt::AlignRight);
    m_gridLayout->addWidget(m_offsetX->GetTextWidget(),     row, 2, 1, 1);
    m_gridLayout->addWidget(m_offsetY->GetLabelWidget(),    row, 3, 1, 1);
    m_gridLayout->addWidget(m_offsetY->GetTextWidget(),     row, 4, 1, 1);
    row++;

    // Row 13 - LINE
    m_gridLayout->addWidget(m_line, row, 0, 1, 5);
    //=========================================================

    connect(m_albedoColor,  SIGNAL(ColorPickingCompleted()),    this, SLOT(MapAlbedo()));
    connect(m_roughness,    SIGNAL(ValueChanged()),             this, SLOT(MapRoughness()));
    connect(m_metallic,     SIGNAL(ValueChanged()),             this, SLOT(MapMetallic()));
    connect(m_normal,       SIGNAL(ValueChanged()),             this, SLOT(MapNormal()));
    connect(m_height,       SIGNAL(ValueChanged()),             this, SLOT(MapHeight()));
    connect(m_tilingX,      SIGNAL(ValueChanged()),             this, SLOT(MapTiling()));
    connect(m_tilingY,      SIGNAL(ValueChanged()),             this, SLOT(MapTiling()));
    connect(m_offsetX,      SIGNAL(ValueChanged()),             this, SLOT(MapOffset()));
    connect(m_offsetY,      SIGNAL(ValueChanged()),             this, SLOT(MapOffset()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusMaterial::Reflect(weak_ptr<GameObject> gameobject)
{
    m_inspectedMaterial = weak_ptr<Material>();

    // Catch the evil case
    if (gameobject.expired())
    {
        this->hide();
        return;
    }

    MeshRenderer* meshRenderer = gameobject._Get()->GetComponent<MeshRenderer>();
    if (!meshRenderer)
    {
        this->hide();
        return;
    }



    m_inspectedMaterial = meshRenderer->GetMaterial();
    if (m_inspectedMaterial.expired())
    {
        this->hide();
        return;
    }

    // Do the actual reflection
    ReflectName();
    ReflectShader();
    ReflectAlbedo();
    ReflectRoughness();
    ReflectMetallic();
    ReflectNormal();
    ReflectHeight();
    ReflectOcclusion();
    ReflectEmission();
    ReflectMask();
    ReflectTiling();
    ReflectOffset();

    SetPropertiesVisible(m_inspectedMaterial._Get()->IsEditable() ? true : false);

    // Make this widget visible
    this->show();
}

void DirectusMaterial::ReflectFile(string filePath)
{
    // Load the material (won't be loaded again if it's already loaded)
    m_inspectedMaterial = m_inspector->GetContext()->GetSubsystem<ResourceManager>()->Load<Material>(filePath);

    if (m_inspectedMaterial.expired())
        return;

    // Do the actual reflection
    ReflectName();
    ReflectShader();
    ReflectAlbedo();
    ReflectRoughness();
    ReflectMetallic();
    ReflectNormal();
    ReflectHeight();
    ReflectOcclusion();
    ReflectEmission();
    ReflectMask();
    ReflectTiling();
    ReflectOffset();

    SetPropertiesVisible(m_inspectedMaterial._Get()->IsEditable());

    // Make this widget visible
    this->show();
}

std::weak_ptr<Material> DirectusMaterial::GetInspectedMaterial()
{
    return m_inspectedMaterial;
}

void DirectusMaterial::SetPropertiesVisible(bool visible)
{
    m_shaderLabel->setVisible(visible);

    m_albedoImage->setVisible(visible);
    m_albedoLabel->setVisible(visible);
    m_albedoColor->GetWidget()->setVisible(visible);

    m_roughnessImage->setVisible(visible);
    m_roughnessLabel->setVisible(visible);
    m_roughness->GetSlider()->setVisible(visible);
    m_roughness->GetLineEdit()->setVisible(visible);

    m_metallicImage->setVisible(visible);
    m_metallicLabel->setVisible(visible);
    m_metallic->GetSlider()->setVisible(visible);
    m_metallic->GetLineEdit()->setVisible(visible);

    m_normalImage->setVisible(visible);
    m_normalLabel->setVisible(visible);
    m_normal->GetSlider()->setVisible(visible);
    m_normal->GetLineEdit()->setVisible(visible);

    m_heightImage->setVisible(visible);
    m_heightLabel->setVisible(visible);
    m_height->GetSlider()->setVisible(visible);
    m_height->GetLineEdit()->setVisible(visible);

    m_occlusionImage->setVisible(visible);
    m_occlusionLabel->setVisible(visible);

    m_emissionImage->setVisible(visible);
    m_emissionLabel->setVisible(visible);

    m_maskImage->setVisible(visible);
    m_maskLabel->setVisible(visible);

    m_tilingLabel->setVisible(visible);
    m_tilingX->GetLabelWidget()->setVisible(visible);
    m_tilingX->GetTextWidget()->setVisible(visible);
    m_tilingY->GetLabelWidget()->setVisible(visible);
    m_tilingY->GetTextWidget()->setVisible(visible);

    m_offsetLabel->setVisible(visible);
    m_offsetX->GetLabelWidget()->setVisible(visible);
    m_offsetX->GetTextWidget()->setVisible(visible);
    m_offsetY->GetLabelWidget()->setVisible(visible);
    m_offsetY->GetTextWidget()->setVisible(visible);
}

void DirectusMaterial::ReflectName()
{
    std::string name = m_inspectedMaterial._Get()->GetResourceName();
    QString text = QString::fromStdString("Material - " + name);
    m_title->setText(text);
}

void DirectusMaterial::ReflectShader()
{
    weak_ptr<ShaderVariation> shader = m_inspectedMaterial._Get()->GetShader();
    std::string name = !shader.expired() ? shader._Get()->GetResourceName() : NOT_ASSIGNED;
    QString text = QString::fromStdString("Shader - " + name);
    m_shaderLabel->setText(text);
}

void DirectusMaterial::ReflectAlbedo()
{
    // Load the albedo texture preview
    string texPath = m_inspectedMaterial._Get()->GetTexturePathByType(TextureType_Albedo);
    m_albedoImage->LoadImageAsync(texPath);

    Vector4 color = m_inspectedMaterial._Get()->GetColorAlbedo();
    m_albedoColor->SetColor(color);
}

void DirectusMaterial::ReflectRoughness()
{
    float roughness = m_inspectedMaterial._Get()->GetRoughnessMultiplier();
    m_roughness->SetValue(roughness);

    // Load the roughness texture preview
    string texPath = m_inspectedMaterial._Get()->GetTexturePathByType(TextureType_Roughness);
    m_roughnessImage->LoadImageAsync(texPath);
}

void DirectusMaterial::ReflectMetallic()
{
    float metallic = m_inspectedMaterial._Get()->GetMetallicMultiplier();
    m_metallic->SetValue(metallic);

    // Load the metallic texture preview
    string texPath = m_inspectedMaterial._Get()->GetTexturePathByType(TextureType_Metallic);
    m_metallicImage->LoadImageAsync(texPath);
}

void DirectusMaterial::ReflectNormal()
{
    float normal = m_inspectedMaterial._Get()->GetNormalMultiplier();
    m_normal->SetValue(normal);

    // Load the normal texture preview
    string texPath = m_inspectedMaterial._Get()->GetTexturePathByType(TextureType_Normal);
    m_normalImage->LoadImageAsync(texPath);
}

void DirectusMaterial::ReflectHeight()
{
    float height = m_inspectedMaterial._Get()->GetHeightMultiplier();
    m_height->SetValue(height);

    // Load the height texture preview
    string texPath = m_inspectedMaterial._Get()->GetTexturePathByType(TextureType_Height);
    m_heightImage->LoadImageAsync(texPath);
}

void DirectusMaterial::ReflectOcclusion()
{
    // Load the occlusion texture preview
    string texPath = m_inspectedMaterial._Get()->GetTexturePathByType(TextureType_Occlusion);
    m_occlusionImage->LoadImageAsync(texPath);
}

void DirectusMaterial::ReflectEmission()
{
    // Load the emission texture preview
    string texPath = m_inspectedMaterial._Get()->GetTexturePathByType(TextureType_Emission);
    m_emissionImage->LoadImageAsync(texPath);
}

void DirectusMaterial::ReflectMask()
{
    // Load the mask texture preview
    string texPath = m_inspectedMaterial._Get()->GetTexturePathByType(TextureType_Mask);
    m_maskImage->LoadImageAsync(texPath);
}

void DirectusMaterial::ReflectTiling()
{
    Vector2 tiling = m_inspectedMaterial._Get()->GetTilingUV();
    m_tilingX->SetFromFloat(tiling.x);
    m_tilingY->SetFromFloat(tiling.y);

    m_inspectedMaterial._Get()->SaveToFile(m_inspectedMaterial._Get()->GetResourceFilePath());
}

void DirectusMaterial::ReflectOffset()
{
    Vector2 offset = m_inspectedMaterial._Get()->GetOffsetUV();
    m_offsetX->SetFromFloat(offset.x);
    m_offsetY->SetFromFloat(offset.y);

    m_inspectedMaterial._Get()->SaveToFile(m_inspectedMaterial._Get()->GetResourceFilePath());
}

void DirectusMaterial::MapAlbedo()
{
    if (m_inspectedMaterial.expired())
        return;

    Vector4 color =  m_albedoColor->GetColor();
    m_inspectedMaterial._Get()->SetColorAlbedo(color);
    m_inspectedMaterial._Get()->SaveToFile(m_inspectedMaterial._Get()->GetResourceFilePath());
}

void DirectusMaterial::MapRoughness()
{
    if (m_inspectedMaterial.expired())
        return;

    float roughness =  m_roughness->GetValue();
    m_inspectedMaterial._Get()->SetRoughnessMultiplier(roughness);
    m_inspectedMaterial._Get()->SaveToFile(m_inspectedMaterial._Get()->GetResourceFilePath());
}

void DirectusMaterial::MapMetallic()
{
    if (m_inspectedMaterial.expired())
        return;

    float metallic =  m_metallic->GetValue();
    m_inspectedMaterial._Get()->SetMetallicMultiplier(metallic);
    m_inspectedMaterial._Get()->SaveToFile(m_inspectedMaterial._Get()->GetResourceFilePath());
}

void DirectusMaterial::MapNormal()
{
    if (m_inspectedMaterial.expired())
        return;

    float normal =  m_normal->GetValue();
    m_inspectedMaterial._Get()->SetNormalMultiplier(normal);
    m_inspectedMaterial._Get()->SaveToFile(m_inspectedMaterial._Get()->GetResourceFilePath());
}

void DirectusMaterial::MapHeight()
{
    if (m_inspectedMaterial.expired())
        return;

    float height =  m_height->GetValue();
    m_inspectedMaterial._Get()->SetHeightMultiplier(height);
    m_inspectedMaterial._Get()->SaveToFile(m_inspectedMaterial._Get()->GetResourceFilePath());
}

void DirectusMaterial::MapOcclusion()
{
    if (m_inspectedMaterial.expired())
        return;

    m_inspectedMaterial._Get()->SaveToFile(m_inspectedMaterial._Get()->GetResourceFilePath());
}

void DirectusMaterial::MapEmission()
{
    if (m_inspectedMaterial.expired())
        return;

    m_inspectedMaterial._Get()->SaveToFile(m_inspectedMaterial._Get()->GetResourceFilePath());
}

void DirectusMaterial::MapMask()
{
    if (m_inspectedMaterial.expired())
        return;

    m_inspectedMaterial._Get()->SaveToFile(m_inspectedMaterial._Get()->GetResourceFilePath());
}

void DirectusMaterial::MapTiling()
{
    if (m_inspectedMaterial.expired())
        return;

    Vector2 tiling;
    tiling.x = m_tilingX->GetAsFloat();
    tiling.y = m_tilingY->GetAsFloat();
    m_inspectedMaterial._Get()->SetTilingUV(tiling);
    m_inspectedMaterial._Get()->SaveToFile(m_inspectedMaterial._Get()->GetResourceFilePath());
}

void DirectusMaterial::MapOffset()
{
    if (m_inspectedMaterial.expired())
        return;

    Vector2 offset;
    offset.x = m_offsetX->GetAsFloat();
    offset.y = m_offsetY->GetAsFloat();
    m_inspectedMaterial._Get()->SetOffsetUV(offset);
    m_inspectedMaterial._Get()->SaveToFile(m_inspectedMaterial._Get()->GetResourceFilePath());
}
