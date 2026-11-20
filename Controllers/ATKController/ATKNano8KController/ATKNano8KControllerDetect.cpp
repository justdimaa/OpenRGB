/*---------------------------------------------------------*\
| ATKNano8KControllerDetect.cpp                             |
|                                                           |
|   Detector for ATK mice on the ATK 8K Nano Dongle         |
|                                                           |
|   justdimaa                                   20 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <hidapi.h>
#include "DetectionManager.h"
#include "ATKNano8KController.h"
#include "RGBController_ATKNano8K.h"

/*---------------------------------------------------------*\
| ATK vendor ID                                             |
\*---------------------------------------------------------*/
#define ATK_VID                             0x373B

/*---------------------------------------------------------*\
| Receiver product IDs                                      |
\*---------------------------------------------------------*/
#define ATK_NANO_8K_DONGLE_PID              0x1178

/*---------------------------------------------------------*\
| The lighting commands go to the vendor collection on      |
| interface 2                                               |
\*---------------------------------------------------------*/
#define ATK_NANO_8K_INTERFACE               2
#define ATK_NANO_8K_USAGE_PAGE              0xFF10
#define ATK_NANO_8K_USAGE                   0x0001

DetectedControllers DetectATKNano8KControllers(hid_device_info* info, const std::string& name)
{
    DetectedControllers detected_controllers;
    hid_device*         dev;

    dev = hid_open_path(info->path);

    if(dev)
    {
        ATKNano8KController*     controller     = new ATKNano8KController(dev, *info, name);
        RGBController_ATKNano8K* rgb_controller = new RGBController_ATKNano8K(controller);

        detected_controllers.push_back(rgb_controller);
    }

    return(detected_controllers);
}

REGISTER_HID_DETECTOR_IPU("ATK Mouse (8K Nano Dongle)", DetectATKNano8KControllers, ATK_VID, ATK_NANO_8K_DONGLE_PID, ATK_NANO_8K_INTERFACE, ATK_NANO_8K_USAGE_PAGE, ATK_NANO_8K_USAGE);
