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

//============================================
#include <QWidget>
#include <QGridLayout>
#include <QLineEdit>
#include "Core/GameObject.h"
#include "Graphics/Material.h"
#include "DirectusComboSliderText.h"
#include "DirectusComboLabelText.h"
#include "DirectusColorPicker.h"
#include <QComboBox>
#include "Math/Vector2.h"
#include "DirectusCore.h"
#include "DirectusMaterialTextureDropTarget.h"
#include "DirectusIComponent.h"
//============================================

class DirectusMaterial : public DirectusIComponent
{
    Q_OBJECT
public:
    DirectusMaterial();

    virtual void Initialize(DirectusInspector* inspector, QWidget* mainWindow);
    virtual void Reflect(std::weak_ptr<Directus::GameObject> gameobject);

    void ReflectFile(std::string filepath);
    std::weak_ptr<Directus::Material> GetInspectedMaterial();

private:
    //= SHADER ======================
    QLabel* m_shaderLabel;
    QComboBox* m_shader;
    //===============================

    //= ALBEDO ======================
    DirectusMaterialTextureDropTarget* m_albedoImage;
    QLabel* m_albedoLabel;
    DirectusColorPicker* m_albedoColor;
    //===============================

    //= ROUGHNESS ===================
    DirectusMaterialTextureDropTarget* m_roughnessImage;
    QLabel* m_roughnessLabel;
    DirectusComboSliderText* m_roughness;
    //===============================

    //= METALLIC ====================
    DirectusMaterialTextureDropTarget* m_metallicImage;
    QLabel* m_metallicLabel;
    DirectusComboSliderText* m_metallic;
    //===============================

    //= NORMAL ======================
    DirectusMaterialTextureDropTarget* m_normalImage;
    QLabel* m_normalLabel;
    DirectusComboSliderText* m_normal;
    //===============================

    //= HEIGHT ======================
    DirectusMaterialTextureDropTarget* m_heightImage;
    QLabel* m_heightLabel;
    DirectusComboSliderText* m_height;
    //===============================

    //= OCCLUSION ===================
    DirectusMaterialTextureDropTarget* m_occlusionImage;
    QLabel* m_occlusionLabel;
    DirectusComboSliderText* m_occlusion;
    //===============================

    //= EMISSION ====================
    DirectusMaterialTextureDropTarget* m_emissionImage;
    QLabel* m_emissionLabel;
    //===============================

    //= MASK ========================
    DirectusMaterialTextureDropTarget* m_maskImage;
    QLabel* m_maskLabel;
    //===============================

    //= SPECULAR ====================
    QLabel* m_specularLabel;
    DirectusComboSliderText* m_specular;
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

    //= SAVE BUTTON =================
    QPushButton* m_buttonSave;
    std::shared_ptr<Directus::Material> m_matFromFile;
    //===============================

    //= MISC ========================
    QGridLayout* m_gridLayout;
    std::weak_ptr<Directus::Material> m_inspectedMaterial;
    //===============================

    void SetPropertiesVisible(bool visible);

    void ReflectName();
    void ReflectAlbedo();
    void ReflectRoughness();
    void ReflectMetallic();
    void ReflectNormal();
    void ReflectHeight();
    void ReflectOcclusion();
    void ReflectEmission();
    void ReflectMask();
    void ReflectSpecular();
    void ReflectTiling();
    void ReflectOffset();

public slots:
    void MapAlbedo();
    void MapRoughness();
    void MapMetallic();
    void MapNormal();
    void MapHeight();
    void MapOcclusion();
    void MapEmission();
    void MapMask();
    void MapSpecular();
    void MapTiling();
    void MapOffset();
    void SaveMaterial();
    virtual void Remove(){};
};
