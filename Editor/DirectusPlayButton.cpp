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
    m_d3dWidget = nullptr;

    connect(this, SIGNAL(toggled(bool)), this, SLOT(AdjustEngine(bool)));
}

void DirectusPlayButton::SetDirectus3DWidget(Directus3D* directus3DWidget)
{
    m_d3dWidget = directus3DWidget;
}

void DirectusPlayButton::AdjustEngine(bool play)
{
    if (play)
        Play();
    else
        Stop();
}

void DirectusPlayButton::Play()
{
    if (!m_d3dWidget)
        return;

    m_d3dWidget->Play();
}

void DirectusPlayButton::Stop()
{
    if (!m_d3dWidget)
        return;

    m_d3dWidget->Stop();
}
