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

//============================
#include "DirectusScript.h"
#include "DirectusInspector.h"
//============================

DirectusScript::DirectusScript(QWidget *parent) : QWidget(parent)
{

}

void DirectusScript::Initialize(DirectusCore* directusCore, DirectusInspector* inspector, QWidget* mainWindow)
{
    m_directusCore = directusCore;
    m_inspector = inspector;

    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    //= TITLE =================================================
    m_title = new QLabel("Script");
    m_title->setStyleSheet(
                "background-image: url(:/Images/script.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 20px;"
                );

    m_optionsButton = new DirectusDropDownButton();
    m_optionsButton->Initialize();
    //=========================================================

    //= LINE ======================================
    m_line = new QWidget();
    m_line->setFixedHeight(1);
    m_line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_line->setStyleSheet(QString("background-color: #585858;"));
    //=============================================

    // addWidget(widget, row, column, rowspan, colspan)
    //= GRID ======================================================================
    // Row 0 - TITLE
    m_gridLayout->addWidget(m_title, 0, 0, 1, 1);
    m_gridLayout->addWidget(m_optionsButton, 0, 1, 1, 1);

    // Row 1 - LINE
    m_gridLayout->addWidget(m_line, 1, 0, 1, 1);
    //==============================================================================

    connect(m_optionsButton,        SIGNAL(Remove()),                   this, SLOT(Remove()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusScript::Reflect(GameObject* gameobject)
{
    m_inspectedScript = nullptr;

    // Catch evil case
    if (!gameobject)
    {
        this->hide();
        return;
    }

    // Catch the seed of the evil
    m_inspectedScript = gameobject->GetComponent<Script>();
    if (!m_inspectedScript)
    {
        this->hide();
        return;
    }

    // Do the actual reflection
    ReflectName();

    // Make this widget visible
    this->show();
}

void DirectusScript::ReflectName()
{
    if (!m_inspectedScript)
        return;

    QString name = QString::fromStdString(m_inspectedScript->GetName());
    m_title->setText(name + " (Script)");
}

void DirectusScript::Map()
{

}

void DirectusScript::Remove()
{
    if (!m_inspectedScript)
        return;

    GameObject* gameObject = m_inspectedScript->g_gameObject;
    gameObject->RemoveComponent<Script>();
    m_directusCore->Update();

    m_inspector->Inspect(gameObject);
}
