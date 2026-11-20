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
#include <chrono>
#include <condition_variable>
#include <cstddef>
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
#define ATK_NANO8K_INPUT_SIZE               64
#define ATK_NANO8K_DPI_STAGES               8

enum
{
    ATK_NANO8K_CMD_SET_RGB                  = 0x22,
    ATK_NANO8K_CMD_SET_DPI_COLOR            = 0x27,
    ATK_NANO8K_CMD_GET_CONFIG               = 0x82,
    ATK_NANO8K_CMD_GET_DPI_COLOR            = 0xA7,
};

/*---------------------------------------------------------*\
| Light modes as the mouse firmware numbers them. ATK HUB   |
| also knows 5 ("with battery"), not offered for this mouse |
\*---------------------------------------------------------*/
enum
{
    ATK_NANO8K_LIGHT_OFF                    = 0,
    ATK_NANO8K_LIGHT_STATIC                 = 1,
    ATK_NANO8K_LIGHT_BREATHING              = 2,
    ATK_NANO8K_LIGHT_NEON                   = 3,
    ATK_NANO8K_LIGHT_REACTIVE               = 4,
    ATK_NANO8K_LIGHT_DPI_BREATHING          = 10,
};

/*---------------------------------------------------------*\
| Colour index 0-7 picks a fixed colour; index 8 makes the  |
| light step through all of them by itself. Only DPI        |
| Breathing takes free RGB colours: those of the DPI        |
| stages. Reactive ignores the colour index.                |
\*---------------------------------------------------------*/
#define ATK_NANO8K_PALETTE_SIZE             8
#define ATK_NANO8K_COLOR_INDEX_CYCLE        8

/*---------------------------------------------------------*\
| A lighting change. Only the fields a mode owns are        |
| written; the others keep what the mouse already has, so   |
| e.g. Off does not overwrite the stored brightness.        |
\*---------------------------------------------------------*/
struct ATKNano8KLighting
{
    unsigned char                                   mode;
    unsigned char                                   brightness;     /* 0-255                */
    unsigned char                                   speed;          /* 1 / 3 / 5            */
    unsigned char                                   color_index;
    std::array<RGBColor, ATK_NANO8K_DPI_STAGES>     dpi_colors;
    bool                                            set_brightness;
    bool                                            set_speed;
    bool                                            set_color_index;
    bool                                            set_dpi_colors;
};

struct ATKNano8KState
{
    bool                                            config_valid;
    bool                                            linked;
    bool                                            dpi_colors_valid;
    ATKNano8KLighting                               lighting;
};

enum ATKNano8KResult
{
    ATK_NANO8K_RESULT_OK,                   /* Written and confirmed            */
    ATK_NANO8K_RESULT_NOT_LINKED,           /* Mouse off, asleep or out of range*/
    ATK_NANO8K_RESULT_UNCONFIRMED,          /* Read-back differs or timed out   */
    ATK_NANO8K_RESULT_IO_ERROR,             /* Receiver unplugged or failing    */
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
    | Queues a lighting change. The first change after a    |
    | quiet moment is written at once; changes following    |
    | it closely are coalesced, so a dragged slider becomes |
    | one write. A change made while the mouse is off or    |
    | asleep is written once it wakes, and one that was not |
    | written yet is flushed when the controller is closed. |
    \*-----------------------------------------------------*/
    void                    SetLighting(const ATKNano8KLighting& lighting);

private:
    ATKNano8KResult         Transact(unsigned char command, const unsigned char* data, size_t data_len, unsigned char* reply, int min_reply_len);
    ATKNano8KResult         Send(unsigned char command, const unsigned char* data, size_t data_len);
    ATKNano8KResult         ReadConfig(ATKNano8KState& state);
    ATKNano8KResult         ReadDpiColors(ATKNano8KState& state);
    ATKNano8KResult         ApplyLighting(const ATKNano8KLighting& lighting);
    std::string             LastHidError();
    void                    WorkerThread();

    hid_device*             dev;
    std::string             location;
    std::string             name;
    std::string             serial;
    std::string             version;
    std::mutex              hid_mutex;

    std::mutex              pending_mutex;
    std::condition_variable pending_cv;
    bool                    pending_valid;
    bool                    pending_attempted;
    ATKNano8KLighting       pending;
    std::atomic<bool>       stop;

    /*-----------------------------------------------------*\
    | Declared last: the worker uses every member above, so |
    | it is constructed after and destroyed before them     |
    \*-----------------------------------------------------*/
    std::thread             worker;
};
