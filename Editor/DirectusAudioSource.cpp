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

DirectusAudioSource::DirectusAudioSource()
{

}

void DirectusAudioSource::Initialize(DirectusInspector* inspector, QWidget* mainWindow)
{
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

    //= AUDIO CLIP ============================================
    m_audioClipLabel = new QLabel("Audio Clip");
    m_audioClip = new DirectusAudioClipDropTarget();
    m_audioClip->Initialize(m_inspector);
    //=========================================================

    //= MUTE ==================================================
    m_muteLabel = new QLabel("Mute");
    m_muteCheckBox = new QCheckBox();
    //=========================================================

    //= PLAY ON AWAKE =========================================
    m_playOnAwakeLabel = new QLabel("Play On Awake");
    m_playOnAwakeCheckBox = new QCheckBox();
    //=========================================================

    //= LOOP ==================================================
    m_loopLabel = new QLabel("Loop");
    m_loopCheckBox = new QCheckBox();
    //=========================================================

    //= PRIORITY ===============================
    m_priorityLabel = new QLabel("Priority");
    m_priority = new DirectusComboSliderText();
    m_priority->Initialize(0, 255);
    //==========================================

    //= VOLUME =================================
    m_volumeLabel = new QLabel("Volume");
    m_volume = new DirectusComboSliderText();
    m_volume->Initialize(0, 1);
    //==========================================

    //= PITCH ==================================
    m_pitchLabel = new QLabel("Pitch");
    m_pitch = new DirectusComboSliderText();
    m_pitch->Initialize(0, 3);
    //==========================================

    //= PAN ====================================
    m_panLabel = new QLabel("Pan");
    m_pan = new DirectusComboSliderText();
    m_pan->Initialize(-1.0f, 1.0f);
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
    m_gridLayout->addWidget(m_optionsButton, row, 2, 1, 1, Qt::AlignRight);
    row++;

    // Row 1 - MATERIAL
    m_gridLayout->addWidget(m_audioClipLabel, row, 0, 1, 1);
    m_gridLayout->addWidget(m_audioClip, row, 1, 1, 2);
    row++;

    // Row 2 - MUTE
    m_gridLayout->addWidget(m_muteLabel, row, 0, 1, 1);
    m_gridLayout->addWidget(m_muteCheckBox, row, 1, 1, 1);
    row++;

    // Row 3 - PLAY ON AWAKE
    m_gridLayout->addWidget(m_playOnAwakeLabel, row, 0, 1, 1);
    m_gridLayout->addWidget(m_playOnAwakeCheckBox, row, 1, 1, 1);
    row++;

    // Row 4 - LOOP
    m_gridLayout->addWidget(m_loopLabel, row, 0, 1, 1);
    m_gridLayout->addWidget(m_loopCheckBox, row, 1, 1, 1);
    row++;

    // Row 5 - PRIORITY
    m_gridLayout->addWidget(m_priorityLabel,               row, 0, 1, 1);
    m_gridLayout->addWidget(m_priority->GetSlider(),       row, 1, 1, 1);
    m_gridLayout->addWidget(m_priority->GetLineEdit(),     row, 2, 1, 1);
    row++;

    // Row 6 - VOLUME
    m_gridLayout->addWidget(m_volumeLabel,               row, 0, 1, 1);
    m_gridLayout->addWidget(m_volume->GetSlider(),       row, 1, 1, 1);
    m_gridLayout->addWidget(m_volume->GetLineEdit(),     row, 2, 1, 1);
    row++;

    // Row 7 - PITCH
    m_gridLayout->addWidget(m_pitchLabel,              row, 0, 1, 1);
    m_gridLayout->addWidget(m_pitch->GetSlider(),       row, 1, 1, 1);
    m_gridLayout->addWidget(m_pitch->GetLineEdit(),     row, 2, 1, 1);
    row++;

    // Row 8 - PAN
    m_gridLayout->addWidget(m_panLabel,                 row, 0, 1, 1);
    m_gridLayout->addWidget(m_pan->GetSlider(),       row, 1, 1, 1);
    m_gridLayout->addWidget(m_pan->GetLineEdit(),     row, 2, 1, 1);
    row++;

    // Row 9 - LINE
    m_gridLayout->addWidget(m_line, row, 0, 1, 3);
    //============================================================

    //= SIGNALS ==========================================================================
    // Gear button on the top left
    connect(m_optionsButton,        SIGNAL(Remove()),       this, SLOT(Remove()));
    connect(m_muteCheckBox,         SIGNAL(clicked(bool)),  this, SLOT(MapMute()));
    connect(m_playOnAwakeCheckBox,  SIGNAL(clicked(bool)),  this, SLOT(MapPlayOnAwake()));
    connect(m_loopCheckBox,         SIGNAL(clicked(bool)),  this, SLOT(MapLoop()));
    connect(m_priority,             SIGNAL(ValueChanged()), this, SLOT(MapPriority()));
    connect(m_volume,               SIGNAL(ValueChanged()), this, SLOT(MapVolume()));
    connect(m_pitch,                SIGNAL(ValueChanged()), this, SLOT(MapPitch()));
    connect(m_pan,                  SIGNAL(ValueChanged()), this, SLOT(MapPan()));
    //====================================================================================

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
    ReflectMute();
    ReflectPlayOnAwake();
    ReflectLoop();
    ReflectPriority();
    ReflectVolume();
    ReflectPitch();
    ReflectPan();

    // Make this widget visible
    this->show();
}

void DirectusAudioSource::ReflectMute()
{
    if (!m_inspectedAudioSource)
        return;

    bool mute = m_inspectedAudioSource->GetMute();
    m_muteCheckBox->setChecked(mute);
}

void DirectusAudioSource::ReflectPlayOnAwake()
{
    if (!m_inspectedAudioSource)
        return;

    bool playOnAwake = m_inspectedAudioSource->GetPlayOnAwake();
    m_playOnAwakeCheckBox->setChecked(playOnAwake);
}

void DirectusAudioSource::ReflectLoop()
{
    if (!m_inspectedAudioSource)
        return;

    bool loop = m_inspectedAudioSource->GetLoop();
    m_loopCheckBox->setChecked(loop);
}

void DirectusAudioSource::ReflectPriority()
{
    if (!m_inspectedAudioSource)
        return;

    int priority = m_inspectedAudioSource->GetPriority();
    m_priority->SetValue(priority);
}

void DirectusAudioSource::ReflectVolume()
{
    if (!m_inspectedAudioSource)
        return;

    float volume = m_inspectedAudioSource->GetVolume();
    m_volume->SetValue(volume);
}

void DirectusAudioSource::ReflectPitch()
{
    if (!m_inspectedAudioSource)
        return;

    float pitch = m_inspectedAudioSource->GetPitch();
    m_pitch->SetValue(pitch);
}

void DirectusAudioSource::ReflectPan()
{
    if (!m_inspectedAudioSource)
        return;

    float pan = m_inspectedAudioSource->GetPan();
    m_pan->SetValue(pan);
}

void DirectusAudioSource::MapMute()
{
    if (!m_inspectedAudioSource)
        return;

    bool mute = m_muteCheckBox->isChecked();
    m_inspectedAudioSource->SetMute(mute);
}

void DirectusAudioSource::MapPlayOnAwake()
{
    if (!m_inspectedAudioSource)
        return;

    bool playOnAwake = m_playOnAwakeCheckBox->isChecked();
    m_inspectedAudioSource->SetPlayOnAwake(playOnAwake);
}

void DirectusAudioSource::MapLoop()
{
    if (!m_inspectedAudioSource)
        return;

    bool loop = m_loopCheckBox->isChecked();
    m_inspectedAudioSource->SetLoop(loop);
}

void DirectusAudioSource::MapPriority()
{
    if(!m_inspectedAudioSource)
        return;

    int priority = m_priority->GetValue();
    m_inspectedAudioSource->SetPriority(priority);
}

void DirectusAudioSource::MapVolume()
{
    if(!m_inspectedAudioSource)
        return;

    float volume = m_volume->GetValue();
    m_inspectedAudioSource->SetVolume(volume);
}

void DirectusAudioSource::MapPitch()
{
    if(!m_inspectedAudioSource)
        return;

    float pitch = m_pitch->GetValue();
    m_inspectedAudioSource->SetPitch(pitch);
}

void DirectusAudioSource::MapPan()
{
    if(!m_inspectedAudioSource)
        return;

    float pan = m_pan->GetValue();
    m_inspectedAudioSource->SetPan(pan);
}

void DirectusAudioSource::Remove()
{
    if (!m_inspectedAudioSource)
        return;

    GameObject* gameObject = m_inspectedAudioSource->g_gameObject;
    gameObject->RemoveComponent<AudioSource>();

    m_inspector->Inspect(gameObject);
}
