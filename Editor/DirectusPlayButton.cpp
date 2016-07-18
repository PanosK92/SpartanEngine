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

//= INCLUDES ==================
#include "DirectusPlayButton.h"
#include "IO/Log.h"
//=============================

DirectusPlayButton::DirectusPlayButton(QWidget *parent) : QPushButton(parent)
{
    m_directusCore = nullptr;

    // An a signal tha will fire each time the button is toggled
    connect(this, SIGNAL(toggled(bool)), this, SLOT(SetPressed(bool)));
}

void DirectusPlayButton::SetDirectusCore(DirectusCore* directusCore)
{
    m_directusCore = directusCore;
}

// NOTE: toggled(bool) sets the checked state of the button automatically,
// however in a few functions we also do it manually. This is important as
// sometimes a state change can be invoked directly from other widgets and
// not necessarily from toggled(bool).

void DirectusPlayButton::Play()
{
    if (!m_directusCore)
        return;

    m_directusCore->Play();

    if (!this->isChecked())
       this->setChecked(true);
}

void DirectusPlayButton::Stop()
{
    if (!m_directusCore)
        return;

    m_directusCore->Stop();

    if (this->isChecked())
       this->setChecked(false);
}

void DirectusPlayButton::SetPressed(bool pressed)
{
    if (pressed)
        Play();
    else
        Stop();
}
