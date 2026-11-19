/*---------------------------------------------------------*\
| RGBController_ATKNano8K.h                                 |
|                                                           |
|   RGBController for ATK mice on the ATK 8K Nano Dongle    |
|                                                           |
|   justdimaa                                   20 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "ATKNano8KController.h"
#include "RGBController.h"

enum
{
    ATK_NANO8K_MODE_STATIC,
    ATK_NANO8K_MODE_COLOR_STEPS,
    ATK_NANO8K_MODE_BREATHING,
    ATK_NANO8K_MODE_SPECTRUM_CYCLE,
    ATK_NANO8K_MODE_REACTIVE,
    ATK_NANO8K_MODE_DPI_BREATHING,
    ATK_NANO8K_MODE_OFF,
};

class RGBController_ATKNano8K : public RGBController
{
public:
    RGBController_ATKNano8K(ATKNano8KController* controller_ptr);
    ~RGBController_ATKNano8K();

    void DeviceUpdateLEDs();
    void DeviceUpdateZoneLEDs(int zone);
    void DeviceUpdateSingleLED(int led);
    void DeviceUpdateMode();

private:
    void SetupZones();
    void LoadDeviceState(const ATKNano8KState& state);

    ATKNano8KController*    controller;
    ATKNano8KLighting       last_lighting;
};
