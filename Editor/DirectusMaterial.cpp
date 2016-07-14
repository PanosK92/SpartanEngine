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
#include "DirectusImageLoader.h"
//==================================

//= NAMESPACES =====================
using namespace Directus::Math;
using namespace std;
//==================================

DirectusMaterial::DirectusMaterial(QWidget *parent) : QWidget(parent)
{

}

void DirectusMaterial::Initialize()
{
    m_gridLayout = new QGridLayout();
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    //= TITLE =================================================
    m_image = new QWidget();
    m_image->setStyleSheet("background-image: url(:/Images/material.png); background-repeat: no-repeat; background-position: left;");
    m_title = new QLabel("Material");
    //=========================================================

    //= SHADER ================================================
    m_shaderLabel = new QLabel("Shader");
    m_shader = new QComboBox();
    m_shader->addItem("Default");
    //=========================================================

    //= ALBEDO ================================================
    m_albedoLabel = new QLabel("Albedo");
    m_albedoImage = new QLabel();
    m_albedoColor = new QPushButton("ColorPicker");
    //=========================================================

    //= ROUGHNESS =============================================
    m_roughnessLabel = new QLabel("Roughness");
    m_roughnessImage = new QLabel();
    m_roughness = new DirectusSliderText();
    m_roughness->Initialize(0, 1);
    //=========================================================

    //= METALLIC ==============================================
    m_metallicLabel = new QLabel("Metallic");
    m_metallicImage = new QLabel();
    m_metallic = new DirectusSliderText();
    m_metallic->Initialize(0, 1);
    //=========================================================

    //= NORMAL ================================================
    m_normalLabel = new QLabel("Normal");
    m_normalImage = new QLabel();
    m_normal = new DirectusSliderText();
    m_normal->Initialize(0, 1);
    //=========================================================

    //= HEIGHT ================================================
    m_heightLabel = new QLabel("Height");
    m_heightImage = new QLabel();
    m_height = new DirectusSliderText();
    m_height->Initialize(0, 1);
    //=========================================================

    //= OCCLUSION =============================================
    m_occlusionLabel = new QLabel("Occlusion");
    m_occlusionImage = new QLabel();
    //=========================================================

    //= EMISSION ==============================================
    m_emissionLabel = new QLabel("Emission");
    m_emissionImage = new QLabel();
    //=========================================================

    //= MASK ==================================================
    m_maskLabel = new QLabel("Mask");
    m_maskImage = new QLabel();
    //=========================================================

    //= REFLECTIVITY  =========================================
    m_reflectivityLabel = new QLabel("Reflectivity");
    m_reflectivity = new DirectusSliderText();
    m_reflectivity->Initialize(0, 1);
    //=========================================================

    //= TILING ================================================
    m_tilingLabel = new QLabel("Tiling");
    m_tilingX = CreateQLineEdit();
    m_tilingY = CreateQLineEdit();
    m_tilingXLabel = new DirectusAdjustLabel();
    m_tilingXLabel->setText("X");
    m_tilingXLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_tilingYLabel = new DirectusAdjustLabel();
    m_tilingYLabel->setText("Y");
    m_tilingYLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    //=========================================================

    //= OFFSET ================================================
    m_offsetLabel = new QLabel("Offset");
    m_offsetX = CreateQLineEdit();
    m_offsetY = CreateQLineEdit();
    m_offsetXLabel = new DirectusAdjustLabel();
    m_offsetXLabel->setText("X");
    m_offsetXLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_offsetYLabel = new DirectusAdjustLabel();
    m_offsetYLabel->setText("Y");
    m_offsetYLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    //=========================================================

    // addWidget(widget, row, column, rowspan, colspan)
    //= GRID ==================================================
    // Row 0
    m_gridLayout->addWidget(m_image, 0, 0, 1, 1);
    m_gridLayout->addWidget(m_title, 0, 1, 1, 2);

    // Row 1 - SHADER
    m_gridLayout->addWidget(m_shaderLabel, 1, 0, 1, 1);
    m_gridLayout->addWidget(m_shader, 1, 1, 1, 1);

    // Row 2 - ALBEDO
    m_gridLayout->addWidget(m_albedoImage, 2, 0, 1, 1);
    m_gridLayout->addWidget(m_albedoLabel, 2, 1, 1, 1);
    m_gridLayout->addWidget(m_albedoColor, 2, 2, 1, 1);

    // Row 3 - ROUGHNESS
    m_gridLayout->addWidget(m_roughnessImage, 3, 0, 1, 1);
    m_gridLayout->addWidget(m_roughnessLabel, 3, 1, 1, 1);
    m_gridLayout->addWidget(m_roughness->GetSlider(), 3, 2, 1, 1);
    m_gridLayout->addWidget(m_roughness->GetLineEdit(), 3, 3, 1, 1);

    // Row 4 - METALLIC
    m_gridLayout->addWidget(m_metallicImage, 4, 0, 1, 1);
    m_gridLayout->addWidget(m_metallicLabel, 4, 1, 1, 1);
    m_gridLayout->addWidget(m_metallic->GetSlider(), 4, 2, 1, 1);
    m_gridLayout->addWidget(m_metallic->GetLineEdit(), 4, 3, 1, 1);

    // Row 5 - NORMAL
    m_gridLayout->addWidget(m_normalImage, 5, 0, 1, 1);
    m_gridLayout->addWidget(m_normalLabel, 5, 1, 1, 1);
    m_gridLayout->addWidget(m_normal->GetSlider(), 5, 2, 1, 1);
    m_gridLayout->addWidget(m_normal->GetLineEdit(), 5, 3, 1, 1);

    // Row 6 - HEIGHT
    m_gridLayout->addWidget(m_heightImage, 6, 0, 1, 1);
    m_gridLayout->addWidget(m_heightLabel, 6, 1, 1, 1);
    m_gridLayout->addWidget(m_height->GetSlider(), 6, 2, 1, 1);
    m_gridLayout->addWidget(m_height->GetLineEdit(), 6, 3, 1, 1);

    // Row 7 - OCCLUSION
    m_gridLayout->addWidget(m_occlusionImage, 7, 0, 1, 1);
    m_gridLayout->addWidget(m_occlusionLabel, 7, 1, 1, 1);

    // Row 8 - EMISSION
    m_gridLayout->addWidget(m_emissionImage, 8, 0, 1, 1);
    m_gridLayout->addWidget(m_emissionLabel, 8, 1, 1, 1);

    // Row 9 - MASK
    m_gridLayout->addWidget(m_maskImage, 9, 0, 1, 1);
    m_gridLayout->addWidget(m_maskLabel, 9, 1, 1, 1);

    // Row 10 - REFLECTIVITY
    m_gridLayout->addWidget(m_reflectivityLabel, 10, 0, 1, 1);
    m_gridLayout->addWidget(m_reflectivity->GetSlider(), 10, 2, 1, 1);
    m_gridLayout->addWidget(m_reflectivity->GetLineEdit(), 10, 3, 1, 1);

    // Row 11 - TILING
    m_gridLayout->addWidget(m_tilingLabel, 11, 0, 1, 1);
    m_gridLayout->addWidget(m_tilingXLabel, 11, 1, 1, 1);
    m_gridLayout->addWidget(m_tilingX, 11, 2, 1, 1);
    m_gridLayout->addWidget(m_tilingYLabel, 11, 3, 1, 1);
    m_gridLayout->addWidget(m_tilingY, 11, 4, 1, 1);

    // Row 12 - OFFSET
    m_gridLayout->addWidget(m_offsetLabel, 12, 0, 1, 1);
    m_gridLayout->addWidget(m_offsetXLabel, 12, 1, 1, 1);
    m_gridLayout->addWidget(m_offsetX, 12, 2, 1, 1);
    m_gridLayout->addWidget(m_offsetYLabel, 12, 3, 1, 1);
    m_gridLayout->addWidget(m_offsetY, 12, 4, 1, 1);
    //=========================================================

    // textChanged(QString) -> emits signal when changed through code
    // textEdited(QString) -> doesn't emits signal when changed through code
    connect(m_roughness, SIGNAL(valueChanged(float)), this, SLOT(MapRoughness()));
    connect(m_metallic, SIGNAL(valueChanged(float)), this, SLOT(MapMetallic()));
    connect(m_normal, SIGNAL(valueChanged(float)), this, SLOT(MapNormal()));
    connect(m_height, SIGNAL(valueChanged(float)), this, SLOT(MapHeight()));
    connect(m_reflectivity, SIGNAL(valueChanged(float)), this, SLOT(MapReflectivity()));
    connect(m_tilingX, SIGNAL(textEdited(QString)), this, SLOT(MapTiling()));
    connect(m_tilingY, SIGNAL(textEdited(QString)), this, SLOT(MapTiling()));

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

    // Do the actual mapping
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

QLineEdit*DirectusMaterial::CreateQLineEdit()
{
    QLineEdit* lineEdit = new QLineEdit();
    lineEdit->setValidator(m_validator);

    return lineEdit;
}

void DirectusMaterial::SetName(string name)
{
    QString text = QString::fromStdString("Material - " + name);
    m_title->setText(text);
}

void DirectusMaterial::SetAlbedo(Vector4 color)
{
   string texPath = m_inspectedMaterial->GetTexturePathByType(TextureType::Albedo);
   QPixmap pix = DirectusImageLoader::LoadFromFile(texPath, 20, 20);
   m_albedoLabel->setPixmap(pix);
}

void DirectusMaterial::SetRoughness(float roughness)
{
    m_roughness->SetValue(roughness);
}

void DirectusMaterial::SetMetallic(float metallic)
{
    m_metallic->SetValue(metallic);
}

void DirectusMaterial::SetNormal(float normal)
{
    m_normal->SetValue(normal);
}

void DirectusMaterial::SetHeight(float height)
{
    m_height->SetValue(height);
}

void DirectusMaterial::SetReflectivity(float reflectivity)
{
    m_reflectivity->SetValue(reflectivity);
}

void DirectusMaterial::SetTiling(Vector2 tiling)
{
    m_tilingX->setText(QString::number(tiling.x));
    m_tilingY->setText(QString::number(tiling.y));
}

void DirectusMaterial::MapAlbedo()
{

}

void DirectusMaterial::MapRoughness()
{
    if (!m_inspectedMaterial)
        return;

    float roughness =  m_roughness->GetValue();
    m_inspectedMaterial->SetRoughness(roughness);
}


void DirectusMaterial::MapMetallic()
{
    if (!m_inspectedMaterial)
        return;

    float metallic =  m_metallic->GetValue();
    m_inspectedMaterial->SetMetallic(metallic);
}

void DirectusMaterial::MapNormal()
{
    if (!m_inspectedMaterial)
        return;

    float normal =  m_normal->GetValue();
    m_inspectedMaterial->SetNormalStrength(normal);
}

void DirectusMaterial::MapHeight()
{
    if (!m_inspectedMaterial)
        return;

    //float height =  m_height->GetValue();
    //m_inspectedMaterial->Seth(height);
}

void DirectusMaterial::MapReflectivity()
{
    if (!m_inspectedMaterial)
        return;

    float reflectivity = m_reflectivity->GetValue();
    m_inspectedMaterial->SetReflectivity(reflectivity);
}

void DirectusMaterial::MapTiling()
{
    if (!m_inspectedMaterial)
        return;

    Vector2 tiling;
    tiling.x = m_tilingX->text().toFloat();
    tiling.y = m_tilingY->text().toFloat();

    m_inspectedMaterial->SetTiling(tiling);
}
