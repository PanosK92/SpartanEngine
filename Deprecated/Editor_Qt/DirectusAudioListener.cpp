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

//= INCLUDES =====================
#include "DirectusAudioListener.h"
#include "DirectusInspector.h"
//================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

DirectusAudioListener::DirectusAudioListener()
{

}

void DirectusAudioListener::Initialize(DirectusInspector *inspector, QWidget *mainWindow)
{
    m_inspector = inspector;

    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    //= TITLE =================================================
    m_title = new QLabel("Audio Listener");
    m_title->setStyleSheet(
                "background-image: url(:/Images/audioListener.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 20px;"
                );

    m_optionsButton = new DirectusDropDownButton();
    m_optionsButton->Initialize(mainWindow);
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

    // Row 0 - TITLE
    m_gridLayout->addWidget(m_title, row, 0, 1, 1);
    m_gridLayout->addWidget(m_optionsButton, row, 2, 1, 1, Qt::AlignRight);
    row++;

    // Row 4 - LINE
    m_gridLayout->addWidget(m_line, row, 0, 1, 3);
    //============================================================

    //= SET GRID SPACING ===================================
    m_gridLayout->setHorizontalSpacing(m_horizontalSpacing);
    m_gridLayout->setVerticalSpacing(m_verticalSpacing);
    //======================================================

    // Gear button on the top left
    connect(m_optionsButton,            SIGNAL(Remove()),       this, SLOT(Remove()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusAudioListener::Reflect(weak_ptr<GameObject> gameobject)
{
    m_inspectedAudioListener = nullptr;

    // Catch the evil case
    if (gameobject.expired())
    {
        this->hide();
        return;
    }

    // Catch the seed of the evil
    m_inspectedAudioListener = gameobject.lock()->GetComponent<AudioListener>().lock().get();
    if (!m_inspectedAudioListener)
    {
        this->hide();
        return;
    }

    // Make this widget visible
    this->show();
}

void DirectusAudioListener::Remove()
{
    if (!m_inspectedAudioListener)
        return;

    auto gameObject = m_inspectedAudioListener->GetGameObjectRef();
    if (!gameObject.expired())
    {
        gameObject.lock()->RemoveComponent<AudioListener>();
    }

    m_inspector->Inspect(gameObject);
}
