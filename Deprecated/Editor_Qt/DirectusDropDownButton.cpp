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

//= INLUCDES ======================
#include "DirectusDropDownButton.h"
#include <QMenu>
#include <QAction>
//=================================

DirectusDropDownButton::DirectusDropDownButton(QWidget *parent) : QPushButton(parent)
{

}

void DirectusDropDownButton::Initialize(QWidget* mainWindow)
{
    m_mainWindow = mainWindow;
    this->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    this->setStyleSheet(
                "background-image: url(:/Images/componentOptions.png);"
                "background-repeat: no-repeat;"
                "background-position: center;"
                "background-color: rgba(0,0,0,0);"
                );

    connect(this, SIGNAL(pressed()), this, SLOT(ShowContextMenu()));
}

void DirectusDropDownButton::ShowContextMenu()
{
    QAction actionReset("Reset", m_mainWindow);
    actionReset.setEnabled(false);
    QAction actionRemove("Remove Component", m_mainWindow);

    //= CONNECT ===================================================================
    connect(&actionReset,  SIGNAL(triggered()), this,  SLOT(ResetTransponder()));
    connect(&actionRemove,  SIGNAL(triggered()), this,  SLOT(RemoveTransponder()));
    //=============================================================================

    QMenu contextMenu(tr("Context menu"), m_mainWindow);
    contextMenu.addAction(&actionReset);
    contextMenu.addSeparator();
    contextMenu.addAction(&actionRemove);

    contextMenu.exec(QCursor::pos());
}

void DirectusDropDownButton::ResetTransponder()
{
    emit Reset();
}

void DirectusDropDownButton::RemoveTransponder()
{
    emit Remove();
}
