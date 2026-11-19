/*---------------------------------------------------------*\
| ATKNano8KController.h                                     |
|                                                           |
|   Driver for ATK mice on the ATK 8K Nano Dongle           |
|   (373B:1178, Holtek receiver), e.g. the ATK A9 Pro       |
|                                                           |
|   justdimaa                                   20 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <hidapi.h>
#include "RGBController.h"

/*---------------------------------------------------------*\
| The vendor collection (usage page FF10, usage 0001) has   |
| no report IDs: requests go out as feature report 0 with   |
| 8 data bytes, replies come back as 64-byte input reports  |
| whose first byte echoes the command.                      |
\*---------------------------------------------------------*/
#define ATK_NANO8K_FEATURE_SIZE             9
#define ATK_NANO8K_INPUT_SIZE               65
#define ATK_NANO8K_DPI_STAGES               8

enum
{
    ATK_NANO8K_CMD_SET_RGB                  = 0x22,
    ATK_NANO8K_CMD_SET_DPI_COLOR            = 0x27,
    ATK_NANO8K_CMD_GET_MOUSE_INFO           = 0x81,
    ATK_NANO8K_CMD_GET_CONFIG               = 0x82,
    ATK_NANO8K_CMD_GET_DPI_CONFIG           = 0xA5,
    ATK_NANO8K_CMD_GET_DPI_COLOR            = 0xA7,
};

/*---------------------------------------------------------*\
| Light modes as the mouse firmware numbers them           |
\*---------------------------------------------------------*/
enum
{
    ATK_NANO8K_LIGHT_OFF                    = 0,
    ATK_NANO8K_LIGHT_STATIC                 = 1,
    ATK_NANO8K_LIGHT_BREATHING              = 2,
    ATK_NANO8K_LIGHT_NEON                   = 3,
    ATK_NANO8K_LIGHT_REACTIVE               = 4,
    ATK_NANO8K_LIGHT_WITH_BATTERY           = 5,
    ATK_NANO8K_LIGHT_DPI_BREATHING          = 10,
};

/*---------------------------------------------------------*\
| Colour index 0-7 is a fixed palette; index 8 makes the    |
| light step through the DPI stage colours, which can be    |
| any RGB value. Writing the same colour to every stage     |
| therefore gives a steady light in any colour.             |
\*---------------------------------------------------------*/
#define ATK_NANO8K_COLOR_INDEX_DPI          8
#define ATK_NANO8K_PALETTE_SIZE             8

struct ATKNano8KLighting
{
    unsigned char                                   mode;
    unsigned char                                   brightness;     /* raw: 16 / 127 / 255  */
    unsigned char                                   speed;          /* raw: 1 / 3 / 5       */
    unsigned char                                   color_index;
    std::array<RGBColor, ATK_NANO8K_DPI_STAGES>     dpi_colors;
    bool                                            set_dpi_colors;
};

struct ATKNano8KState
{
    bool                                            config_valid;
    bool                                            linked;
    ATKNano8KLighting                               lighting;
    bool                                            dpi_colors_valid;
    unsigned char                                   dpi_count;
    unsigned char                                   battery;
    bool                                            mouse_info_valid;
    std::string                                     mouse_firmware;
    unsigned char                                   mouse_cid;
    unsigned char                                   mouse_mid;
};

class ATKNano8KController
{
public:
    ATKNano8KController(hid_device* dev_handle, const hid_device_info& info, const std::string& dev_name);
    ~ATKNano8KController();

    std::string             GetDeviceLocation();
    std::string             GetNameString();
    std::string             GetSerialString();
    std::string             GetVersionString();

    ATKNano8KState          ReadState();

    /*-----------------------------------------------------*\
    | Queues a lighting change. The mouse keeps its light   |
    | settings across power cycles, so writes are          |
    | coalesced (a dragged colour picker becomes one write) |
    | and only what changed is sent. A change made while    |
    | the mouse is off or asleep is sent once it wakes.     |
    \*-----------------------------------------------------*/
    void                    SetLighting(const ATKNano8KLighting& lighting);

private:
    bool                    Transact(unsigned char command, const unsigned char* data, size_t data_len, unsigned char* reply);
    bool                    Send(unsigned char command, const unsigned char* data, size_t data_len);
    bool                    ReadConfig(ATKNano8KState& state);
    bool                    ReadDpiColors(ATKNano8KState& state);
    bool                    ApplyLighting(const ATKNano8KLighting& lighting);
    void                    WorkerThread();

    hid_device*             dev;
    std::string             location;
    std::string             name;
    std::string             version;
    std::mutex              hid_mutex;

    std::thread             worker;
    std::mutex              pending_mutex;
    std::condition_variable pending_cv;
    bool                    pending_valid;
    ATKNano8KLighting       pending;
    std::atomic<bool>       stop;
};
