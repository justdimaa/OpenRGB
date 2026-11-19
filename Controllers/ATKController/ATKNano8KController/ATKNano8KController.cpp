/*---------------------------------------------------------*\
| ATKNano8KController.cpp                                   |
|                                                           |
|   Driver for ATK mice on the ATK 8K Nano Dongle           |
|   (373B:1178, Holtek receiver), e.g. the ATK A9 Pro       |
|                                                           |
|   justdimaa                                   20 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <chrono>
#include <cstdio>
#include <cstring>
#include "ATKNano8KController.h"
#include "LogManager.h"
#include "StringUtils.h"

using namespace std::chrono_literals;

static bool SameLighting(const ATKNano8KLighting& a, const ATKNano8KLighting& b)
{
    return((a.mode           == b.mode)
        && (a.brightness     == b.brightness)
        && (a.speed          == b.speed)
        && (a.color_index    == b.color_index)
        && (a.set_dpi_colors == b.set_dpi_colors)
        && (!a.set_dpi_colors || (a.dpi_colors == b.dpi_colors)));
}

/*---------------------------------------------------------*\
| The receiver relays each request to the mouse over the    |
| air; a short gap between writes keeps it from dropping    |
| them.                                                     |
\*---------------------------------------------------------*/
#define ATK_NANO8K_REPLY_TIMEOUT_MS         700
#define ATK_NANO8K_WRITE_GAP                40ms
#define ATK_NANO8K_DEBOUNCE                 300ms
#define ATK_NANO8K_RETRY_INTERVAL           2s
#define ATK_NANO8K_APPLY_ATTEMPTS           2

ATKNano8KController::ATKNano8KController(hid_device* dev_handle, const hid_device_info& info, const std::string& dev_name)
{
    dev                 = dev_handle;
    location            = info.path;
    name                = dev_name;
    pending_valid       = false;
    stop                = false;

    /*-----------------------------------------------------*\
    | Report the receiver's USB release as the version: it  |
    | is known even while the mouse sleeps, so it stays the |
    | same between runs (profiles compare the version).     |
    \*-----------------------------------------------------*/
    char version_string[16];
    snprintf(version_string, sizeof(version_string), "%X.%02X", (info.release_number >> 8) & 0xFF, info.release_number & 0xFF);
    version             = version_string;

    worker              = std::thread(&ATKNano8KController::WorkerThread, this);
}

ATKNano8KController::~ATKNano8KController()
{
    {
        std::lock_guard<std::mutex> lock(pending_mutex);
        stop = true;
    }
    pending_cv.notify_all();

    if(worker.joinable())
    {
        worker.join();
    }

    hid_close(dev);
}

std::string ATKNano8KController::GetDeviceLocation()
{
    return("HID: " + location);
}

std::string ATKNano8KController::GetNameString()
{
    return(name);
}

std::string ATKNano8KController::GetSerialString()
{
    wchar_t serial_string[128];
    int ret = hid_get_serial_number_string(dev, serial_string, 128);

    if(ret != 0)
    {
        return("");
    }

    return(StringUtils::wstring_to_string(serial_string));
}

std::string ATKNano8KController::GetVersionString()
{
    return(version);
}

/*---------------------------------------------------------*\
| Send a request and wait for the input report answering   |
| it. Other input reports (DPI / rate notifications) that  |
| arrive in between are skipped.                           |
\*---------------------------------------------------------*/
bool ATKNano8KController::Transact(unsigned char command, const unsigned char* data, size_t data_len, unsigned char* reply)
{
    std::lock_guard<std::mutex> lock(hid_mutex);

    unsigned char buf[ATK_NANO8K_INPUT_SIZE];

    hid_set_nonblocking(dev, 1);
    while(hid_read(dev, buf, sizeof(buf)) > 0)
    {
    }
    hid_set_nonblocking(dev, 0);

    unsigned char request[ATK_NANO8K_FEATURE_SIZE] = {};
    request[1] = command;
    if(data_len > ATK_NANO8K_FEATURE_SIZE - 2)
    {
        data_len = ATK_NANO8K_FEATURE_SIZE - 2;
    }
    if(data_len > 0)
    {
        memcpy(&request[2], data, data_len);
    }

    if(hid_send_feature_report(dev, request, sizeof(request)) < 0)
    {
        LOG_DEBUG("[%s] Error sending command %02X: %ls", name.c_str(), command, hid_error(dev));
        return(false);
    }

    if(reply == nullptr)
    {
        std::this_thread::sleep_for(ATK_NANO8K_WRITE_GAP);
        return(true);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ATK_NANO8K_REPLY_TIMEOUT_MS);

    while(std::chrono::steady_clock::now() < deadline)
    {
        int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        int len  = hid_read_timeout(dev, buf, sizeof(buf), left > 0 ? left : 1);

        if(len <= 0)
        {
            break;
        }

        if(buf[0] == command)
        {
            memset(reply, 0, ATK_NANO8K_INPUT_SIZE);
            memcpy(reply, buf, len);
            return(true);
        }
    }

    LOG_DEBUG("[%s] No reply to command %02X", name.c_str(), command);
    return(false);
}

bool ATKNano8KController::Send(unsigned char command, const unsigned char* data, size_t data_len)
{
    return(Transact(command, data, data_len, nullptr));
}

/*---------------------------------------------------------*\
| GetConfigData reply:                                      |
|   [82][rate][dpi stage][light mode][brightness][speed]    |
|   [color index][-][battery][link]                         |
| battery: bit 7 charging, bits 0-6 percent                 |
| link: 0 when no mouse is reachable (off or asleep)        |
\*---------------------------------------------------------*/
bool ATKNano8KController::ReadConfig(ATKNano8KState& state)
{
    unsigned char reply[ATK_NANO8K_INPUT_SIZE];

    state.config_valid = Transact(ATK_NANO8K_CMD_GET_CONFIG, nullptr, 0, reply);

    if(state.config_valid)
    {
        state.lighting.mode         = reply[3];
        state.lighting.brightness   = reply[4];
        state.lighting.speed        = reply[5];
        state.lighting.color_index  = reply[6];
        state.battery               = reply[8] & 0x7F;
        state.linked                = reply[9] != 0;
    }

    return(state.config_valid && state.linked);
}

/*---------------------------------------------------------*\
| GetDpiColor (A7 00 FF) reply: [A7] then 8 x [R][G][B]     |
\*---------------------------------------------------------*/
bool ATKNano8KController::ReadDpiColors(ATKNano8KState& state)
{
    unsigned char reply[ATK_NANO8K_INPUT_SIZE];
    const unsigned char request[] = { 0x00, 0xFF };

    state.dpi_colors_valid = Transact(ATK_NANO8K_CMD_GET_DPI_COLOR, request, sizeof(request), reply);

    if(state.dpi_colors_valid)
    {
        for(unsigned int stage = 0; stage < ATK_NANO8K_DPI_STAGES; stage++)
        {
            state.lighting.dpi_colors[stage] = ToRGBColor(reply[1 + stage * 3], reply[2 + stage * 3], reply[3 + stage * 3]);
        }
    }

    return(state.dpi_colors_valid);
}

ATKNano8KState ATKNano8KController::ReadState()
{
    ATKNano8KState state = {};

    ReadConfig(state);

    if(state.config_valid && state.linked)
    {
        ReadDpiColors(state);

        unsigned char reply[ATK_NANO8K_INPUT_SIZE];

        /*-------------------------------------------------*\
        | GetDpiConfig: [A5][stage count]...                |
        \*-------------------------------------------------*/
        if(Transact(ATK_NANO8K_CMD_GET_DPI_CONFIG, nullptr, 0, reply))
        {
            state.dpi_count = reply[1];
        }

        /*-------------------------------------------------*\
        | GetMouseInfo: [81][ver lo][ver hi][profile][cid]  |
        | [mid]; the A9 Pro reports cid 2, mid 2            |
        \*-------------------------------------------------*/
        if(Transact(ATK_NANO8K_CMD_GET_MOUSE_INFO, nullptr, 0, reply))
        {
            char firmware[16];
            snprintf(firmware, sizeof(firmware), "%X.%02X", reply[2], reply[1]);
            state.mouse_firmware   = firmware;
            state.mouse_cid        = reply[4];
            state.mouse_mid        = reply[5];
            state.mouse_info_valid = true;
        }
    }

    return(state);
}

void ATKNano8KController::SetLighting(const ATKNano8KLighting& lighting)
{
    {
        std::lock_guard<std::mutex> lock(pending_mutex);
        pending       = lighting;
        pending_valid = true;
    }
    pending_cv.notify_all();
}

/*---------------------------------------------------------*\
| Write only what differs from what the mouse reports, then |
| read back to confirm the mouse took it.                   |
\*---------------------------------------------------------*/
bool ATKNano8KController::ApplyLighting(const ATKNano8KLighting& lighting)
{
    ATKNano8KState current = {};

    if(!ReadConfig(current))
    {
        return(false);
    }

    if(lighting.set_dpi_colors)
    {
        if(!ReadDpiColors(current))
        {
            return(false);
        }

        for(unsigned int stage = 0; stage < ATK_NANO8K_DPI_STAGES; stage++)
        {
            if(current.lighting.dpi_colors[stage] != lighting.dpi_colors[stage])
            {
                const unsigned char data[] =
                {
                    0x00,
                    (unsigned char)stage,
                    (unsigned char)RGBGetRValue(lighting.dpi_colors[stage]),
                    (unsigned char)RGBGetGValue(lighting.dpi_colors[stage]),
                    (unsigned char)RGBGetBValue(lighting.dpi_colors[stage]),
                };

                Send(ATK_NANO8K_CMD_SET_DPI_COLOR, data, sizeof(data));
            }
        }
    }

    if((current.lighting.mode        != lighting.mode)
    || (current.lighting.brightness  != lighting.brightness)
    || (current.lighting.speed       != lighting.speed)
    || (current.lighting.color_index != lighting.color_index))
    {
        const unsigned char data[] =
        {
            0x00,
            0x00,
            lighting.mode,
            lighting.brightness,
            lighting.speed,
            lighting.color_index,
        };

        Send(ATK_NANO8K_CMD_SET_RGB, data, sizeof(data));
    }

    ATKNano8KState check = {};

    if(!ReadConfig(check))
    {
        return(false);
    }

    if((check.lighting.mode        != lighting.mode)
    || (check.lighting.brightness  != lighting.brightness)
    || (check.lighting.speed       != lighting.speed)
    || (check.lighting.color_index != lighting.color_index))
    {
        return(false);
    }

    if(lighting.set_dpi_colors)
    {
        if(!ReadDpiColors(check) || check.lighting.dpi_colors != lighting.dpi_colors)
        {
            return(false);
        }
    }

    return(true);
}

void ATKNano8KController::WorkerThread()
{
    std::unique_lock<std::mutex> lock(pending_mutex);

    while(!stop)
    {
        pending_cv.wait(lock, [this]{ return(stop || pending_valid); });

        if(stop)
        {
            break;
        }

        /*-------------------------------------------------*\
        | Wait until changes stop coming in for a moment    |
        \*-------------------------------------------------*/
        ATKNano8KLighting target = pending;

        while(!stop)
        {
            pending_cv.wait_for(lock, ATK_NANO8K_DEBOUNCE);

            if(SameLighting(target, pending))
            {
                break;
            }

            target = pending;
        }

        if(stop)
        {
            break;
        }

        pending_valid = false;
        lock.unlock();

        bool ok = false;

        for(unsigned int attempt = 0; attempt < ATK_NANO8K_APPLY_ATTEMPTS && !ok; attempt++)
        {
            ok = ApplyLighting(target);
        }

        lock.lock();

        /*-------------------------------------------------*\
        | Mouse off, asleep or out of range: keep the change|
        | and try again later, unless a newer one arrived.  |
        \*-------------------------------------------------*/
        if(!ok && !pending_valid)
        {
            LOG_DEBUG("[%s] Mouse not reachable, lighting change kept for later", name.c_str());

            pending       = target;
            pending_valid = true;

            pending_cv.wait_for(lock, ATK_NANO8K_RETRY_INTERVAL, [this]{ return(stop.load()); });
        }
    }
}
