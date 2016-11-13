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

//= INCLUDES ===================
#include "DirectusAudioSource.h"
#include "DirectusInspector.h"
//==============================

DirectusAudioSource::DirectusAudioSource(QWidget *parent) : QWidget(parent)
{

}

void DirectusAudioSource::Initialize(DirectusCore* directusCore, DirectusInspector* inspector, QWidget* mainWindow)
{
    m_directusCore = directusCore;
    m_inspector = inspector;

    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    //= TITLE =================================================
    m_title = new QLabel("Audio Source");
    m_title->setStyleSheet(
                "background-image: url(:/Images/audioSource.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 20px;"
                );

    m_optionsButton = new DirectusDropDownButton();
    m_optionsButton->Initialize(mainWindow);
    //=========================================================

    //= VOLUME =================================
    m_volumeLabel = new QLabel("Volume");
    m_volume = new DirectusComboSliderText();
    m_volume->Initialize(0, 1);
    //==========================================

    //= LINE ======================================
    m_line = new QWidget();
    m_line->setFixedHeight(1);
    m_line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_line->setStyleSheet(QString("background-color: #585858;"));
    //=============================================

    // addWidget(widget, row, column, rowspan, colspan)
    //= GRID ==================================================
    int row = 0;
    // Row 0 - TITLE
    m_gridLayout->addWidget(m_title, row, 0, 1, 1);
    m_gridLayout->addWidget(m_optionsButton, row, 1, 1, 1, Qt::AlignRight);
    row++;

    // Row 1 - VOLUME
    m_gridLayout->addWidget(m_volumeLabel,               4, 0, 1, 1);
    m_gridLayout->addWidget(m_volume->GetSlider(),       4, 1, 1, 1);
    m_gridLayout->addWidget(m_volume->GetLineEdit(),     4, 2, 1, 1);
    row++;

    // Row 2 - LINE
    m_gridLayout->addWidget(m_line, row, 0, 1, 3);
    //============================================================

    //= SIGNALS ==================================================
    // Gear button on the top left
    connect(m_optionsButton,            SIGNAL(Remove()),                   this, SLOT(Remove()));
    // Connect volume control
    connect(m_volume,                   SIGNAL(ValueChanged()),             this, SLOT(MapVolume()));
    //============================================================

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusAudioSource::Reflect(GameObject* gameobject)
{
    m_inspectedAudioSource = nullptr;

    // Catch evil case
    if (!gameobject)
    {
        this->hide();
        return;
    }

    // Catch the seed of the evil
    m_inspectedAudioSource = gameobject->GetComponent<AudioSource>();
    if (!m_inspectedAudioSource)
    {
        this->hide();
        return;
    }

    // Do the actual reflection
    ReflectVolume();

    // Make this widget visible
    this->show();
}

void DirectusAudioSource::ReflectVolume()
{
    if (!m_inspectedAudioSource)
        return;

    float volume = m_inspectedAudioSource->GetVolume();
    m_volume->SetValue(volume);
}

void DirectusAudioSource::MapVolume()
{
    if(!m_inspectedAudioSource || !m_directusCore)
        return;

    float volume = m_volume->GetValue();
    m_inspectedAudioSource->SetVolume(volume);
}

void DirectusAudioSource::Remove()
{
    if (!m_inspectedAudioSource)
        return;

    GameObject* gameObject = m_inspectedAudioSource->g_gameObject;
    gameObject->RemoveComponent<AudioSource>();

    m_inspector->Inspect(gameObject);
}
