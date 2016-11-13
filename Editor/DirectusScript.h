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

//==================================
#include <QWidget>
#include <QGridLayout>
#include "DirectusAdjustLabel.h"
#include <QLineEdit>
#include "Core/GameObject.h"
#include <QDoubleValidator>
#include "Components/Script.h"
#include <QCheckBox>
#include "DirectusCore.h"
#include "DirectusDropDownButton.h"
#include "DirectusIComponent.h"
//==================================

class DirectusScript : public DirectusIComponent
{
    Q_OBJECT
public:
    DirectusScript();

    virtual void Initialize(DirectusInspector* inspector, QWidget* mainWindow);
    virtual void Reflect(GameObject* gameObject);

    void Reflect(Script* script);

private:
    //= MISC ===============================
    QValidator* m_validator;
    Script* m_inspectedScript;
    //======================================

    void ReflectName();

public slots:
    void Map();
    virtual void Remove();
};
