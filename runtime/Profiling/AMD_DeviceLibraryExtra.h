/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ====================================
//#include <adlx/ADLXHelper/Windows/C/ADLXHelper.h>
#include <adlx/Include/I3DSettings.h>
//===============================================

namespace Spartan
{
    class AMD_DeviceLibraryExtra
    {
    public:
        static void Initialize()
        {
            //if (ADLX_SUCCEEDED(helper.Initialize()))
            //{
            //    // get gpu list
            //    SP_ASSERT(ADLX_SUCCEEDED(helper.GetSystemServices()->GetGPUs(&gpus)));

            //    // get 3d settings service
            //    SP_ASSERT(ADLX_SUCCEEDED(helper.GetSystemServices()->Get3DSettingsServices(&service_3d_settings)));

            //    // get gpu interface
            //    IADLXGPUPtr gpu_info;
            //    adlx_uint index = 0;
            //    SP_ASSERT(gpus->At(index, &gpu_info));

            //    // get anti-lag interface
            //    IADLX3DAntiLagPtr interface_antilag;
            //    SP_ASSERT(service_3d_settings->GetAntiLag(gpu_info, &interface_antilag));
            //    IADLX3DAntiLag1Ptr interface_antilag_1(interface_antilag);
            //    SetAntiLagLevel(interface_antilag_1, ADLX_ANTILAG_STATE::ANTILAG);
            //}
        }

        static void Shutdown()
        {
            //if (service_3d_settings != nullptr)
            //{
            //    service_3d_settings->pVtbl->Release(service_3d_settings);
            //    service_3d_settings = nullptr;
            //}

            //if (gpus != nullptr)
            //{
            //    gpus->pVtbl->Release(gpus);
            //    gpus = nullptr;
            //}

            //SP_ASSERT(ADLX_SUCCEEDED(helper.Terminate()));
        }

    private:
        //static ADLXHelper helper;
        //static IADLXGPUListPtr gpus;
        //static IADLX3DSettingsServicesPtr service_3d_settings;
    };
}
