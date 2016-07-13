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

//=================================
#include "DirectusMaterial.h"
#include "Components/MeshRenderer.h"
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
    m_albedoImage = new QWidget();
    m_albedoColor = new QPushButton("ColorPicker");
    //=========================================================

    //= ROUGHNESS =============================================
    m_roughnessLabel = new QLabel("Roughness");
    m_roughnessImage = new QWidget();
    m_roughnessSlider = new QSlider(Qt::Horizontal);
    m_roughnessLineEdit = CreateQLineEdit();
    //=========================================================

    //= METALLIC ==============================================
    m_metallicLabel = new QLabel("Metallic");
    m_metallicImage = new QWidget();
    m_metallicSlider = new QSlider(Qt::Horizontal);
    m_metallicLineEdit = CreateQLineEdit();
    //=========================================================

    //= NORMAL ================================================
    m_normalLabel = new QLabel("Normal");
    m_normalImage = new QWidget();
    m_normalSlider = new QSlider(Qt::Horizontal);
    m_normalLineEdit = CreateQLineEdit();
    //=========================================================

    //= HEIGHT ================================================
    m_heightLabel = new QLabel("Height");
    m_heightImage = new QWidget();
    m_heightSlider = new QSlider(Qt::Horizontal);
    m_heightLineEdit = CreateQLineEdit();
    //=========================================================

    //= OCCLUSION =============================================
    m_occlusionLabel = new QLabel("Occlusion");
    m_occlusionImage = new QWidget();
    //=========================================================

    //= EMISSION ==============================================
    m_emissionLabel = new QLabel("Emission");
    m_emissionImage = new QWidget();
    //=========================================================

    //= MASK ==================================================
    m_maskLabel = new QLabel("Mask");
    m_maskImage = new QWidget();
    //=========================================================

    //= REFLECTIVITY  =========================================
    m_reflectivityLabel = new QLabel("Reflectivity");
    m_reflectivitySlider = new QSlider(Qt::Horizontal);
    m_reflectivityLineEdit = CreateQLineEdit();
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
    m_gridLayout->addWidget(m_title, 0, 2, 1, 1);

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
    m_gridLayout->addWidget(m_roughnessSlider, 3, 2, 1, 1);
    m_gridLayout->addWidget(m_roughnessLineEdit, 3, 3, 1, 1);

    // Row 4 - METALLIC
    m_gridLayout->addWidget(m_metallicImage, 4, 0, 1, 1);
    m_gridLayout->addWidget(m_metallicLabel, 4, 1, 1, 1);
    m_gridLayout->addWidget(m_metallicSlider, 4, 2, 1, 1);
    m_gridLayout->addWidget(m_metallicLineEdit, 4, 3, 1, 1);

    // Row 5 - NORMAL
    m_gridLayout->addWidget(m_normalImage, 5, 0, 1, 1);
    m_gridLayout->addWidget(m_normalLabel, 5, 1, 1, 1);
    m_gridLayout->addWidget(m_normalSlider, 5, 2, 1, 1);
    m_gridLayout->addWidget(m_normalLineEdit, 5, 3, 1, 1);

    // Row 6 - HEIGHT
    m_gridLayout->addWidget(m_heightImage, 6, 0, 1, 1);
    m_gridLayout->addWidget(m_heightLabel, 6, 1, 1, 1);
    m_gridLayout->addWidget(m_heightSlider, 6, 2, 1, 1);
    m_gridLayout->addWidget(m_heightLineEdit, 6, 3, 1, 1);

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
    m_gridLayout->addWidget(m_reflectivitySlider, 10, 2, 1, 1);
    m_gridLayout->addWidget(m_reflectivityLineEdit, 10, 3, 1, 1);

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

    // Connect textEdit(QString) signal with the appropriate slot
    // NOTE: Unlike textChanged(), this signal is not emitted when the
    // text is changed programmatically, for example, by calling setText().
    //connect(m_posX, SIGNAL(textChanged(QString)), this, SLOT(UpdateEnginePos()));
    //connect(m_posY, SIGNAL(textChanged(QString)), this, SLOT(UpdateEnginePos()));
    //connect(m_posZ, SIGNAL(textChanged(QString)), this, SLOT(UpdateEnginePos()));
    //connect(m_rotX, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineRot()));
    //connect(m_rotY, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineRot()));
    //connect(m_rotZ, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineRot()));
    //connect(m_scaX, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineSca()));
    //connect(m_scaY, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineSca()));
    //connect(m_scaZ, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineSca()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusMaterial::Map(GameObject* gameobject)
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
    //SetProjection(m_inspectedCamera->GetProjection());
    //SetFOV(m_inspectedCamera->GetFieldOfView());
    //SetNearPlane(m_inspectedCamera->GetNearPlane());
    //SetFarPlane( m_inspectedCamera->GetFarPlane());

    // Make this widget visible
    this->show();
}

QLineEdit*DirectusMaterial::CreateQLineEdit()
{
    QLineEdit* lineEdit = new QLineEdit();
    lineEdit->setValidator(m_validator);

    return lineEdit;
}
