/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES ==================
#include "SpartanDefinitions.h"
//=============================

namespace Spartan
{
    class SPARTAN_CLASS Color
    {
    public:
        Color() = default;
        Color(const float r, const float g, const float b, const float a = 1.0f);
        ~Color() = default;

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;

        // Physically based values: https://physicallybased.info/

        // Materials
        static const Color aluminum;
        static const Color blood;
        static const Color bone;
        static const Color brass;
        static const Color brick;
        static const Color charcoal;
        static const Color chocolate;
        static const Color chromium;
        static const Color cobalt;
        static const Color concrete;
        static const Color cooking_oil;
        static const Color copper;
        static const Color diamond;
        static const Color egg_shell;
        static const Color eye_cornea;
        static const Color eye_lens;
        static const Color eye_sclera;
        static const Color glass;
        static const Color gold;
        static const Color gray_card;
        static const Color honey;
        static const Color ice;
        static const Color iron;
        static const Color ketchup;
        static const Color lead;
        static const Color mercury;
        static const Color milk;
        static const Color nickel;
        static const Color office_paper;
        static const Color plastic_pc;
        static const Color plastic_pet;
        static const Color plastic_acrylic;
        static const Color plastic_pp;
        static const Color plastic_pvc;
        static const Color platinum;
        static const Color salt;
        static const Color sand;
        static const Color sapphire;
        static const Color silver;
        static const Color skin_1;
        static const Color skin_2;
        static const Color skin_3;
        static const Color skin_4;
        static const Color skin_5;
        static const Color skin_6;
        static const Color snow;
        static const Color tire;
        static const Color titanium;
        static const Color tungsten;
        static const Color vanadium;
        static const Color water;
        static const Color zinc;
    };
}
