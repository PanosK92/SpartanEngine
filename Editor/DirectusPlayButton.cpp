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
#include "DirectusCore.h"
#include "Logging/Log.h"
//=============================

DirectusPlayButton::DirectusPlayButton(QWidget *parent) : QPushButton(parent)
{
    m_directusCore = nullptr;

    // A signal tha will fire each time the button is toggled
    connect(this, SIGNAL(toggled(bool)), this, SLOT(SetPressed(bool)));
}

void DirectusPlayButton::Initialize(DirectusCore* directusCore)
{
    m_directusCore = directusCore;

    // Connect the engine to the button, allowing to control the button.
    connect(m_directusCore, SIGNAL(EngineStarting()), this, SLOT(StartEngine()));
    connect(m_directusCore, SIGNAL(EngineStopping()), this, SLOT(StopEngine()));

    // NOTE: The button can indeed start/stop the engine, but the engine has to also
    // change the state of the button when it has to stop/start (many scenarios).
}

// Starts the engine
void DirectusPlayButton::StartEngine()
{
    if (!m_directusCore->IsRunning())
        m_directusCore->Start();

    MakeButtonLookPressed();
}

// Stops the engine
void DirectusPlayButton::StopEngine()
{
    if (m_directusCore->IsRunning())
        m_directusCore->Stop();

    MakeButtonLookReleased();
}

// Makes the function appear pressed, purely visual.
void DirectusPlayButton::MakeButtonLookPressed()
{
    if (!this->isChecked())
       this->setChecked(true);
}

// Makes the function appear released, purely visual.
void DirectusPlayButton::MakeButtonLookReleased()
{
    if (this->isChecked())
        this->setChecked(false);
}

// This is called when the button actually gets pressed.
// It has full control over the engine flow.
void DirectusPlayButton::SetPressed(bool pressed)
{
    if (pressed)
        StartEngine();
    else
        StopEngine();

    this->clearFocus();
}
