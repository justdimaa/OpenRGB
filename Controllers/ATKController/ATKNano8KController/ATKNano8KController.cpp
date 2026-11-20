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

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "ATKNano8KController.h"
#include "LogManager.h"
#include "StringUtils.h"

using namespace std::chrono_literals;

/*---------------------------------------------------------*\
| The receiver relays each request to the mouse over the    |
| air; a short gap between writes keeps it from dropping    |
| them.                                                     |
\*---------------------------------------------------------*/
#define ATK_NANO8K_REPLY_TIMEOUT                    700ms
#define ATK_NANO8K_WRITE_GAP                        40ms
#define ATK_NANO8K_DEBOUNCE                         300ms
#define ATK_NANO8K_RETRY_INTERVAL                   2000ms
#define ATK_NANO8K_IO_RETRY_MAX                     30000ms
#define ATK_NANO8K_FLUSH_READS_MAX                  64
#define ATK_NANO8K_CONFIG_REPLY_LEN                 10
#define ATK_NANO8K_DPI_COLOR_REPLY_LEN              (1 + ATK_NANO8K_DPI_STAGES * 3)

static bool SameLighting(const ATKNano8KLighting& a, const ATKNano8KLighting& b)
{
    return((a.mode            == b.mode)
        && (a.set_brightness  == b.set_brightness)
        && (a.set_speed       == b.set_speed)
        && (a.set_color_index == b.set_color_index)
        && (a.set_dpi_colors  == b.set_dpi_colors)
        && (!a.set_brightness  || (a.brightness  == b.brightness))
        && (!a.set_speed       || (a.speed       == b.speed))
        && (!a.set_color_index || (a.color_index == b.color_index))
        && (!a.set_dpi_colors  || (a.dpi_colors  == b.dpi_colors)));
}

/*---------------------------------------------------------*\
| Lay a newer change over an older one that was not written |
| yet: the newer one wins for every field it sets, fields   |
| it leaves alone (e.g. DPI colours after switching to Off) |
| keep the older value, so no queued change is lost.        |
\*---------------------------------------------------------*/
static void MergeLighting(ATKNano8KLighting& base, const ATKNano8KLighting& newer)
{
    base.mode = newer.mode;

    if(newer.set_brightness)
    {
        base.brightness      = newer.brightness;
        base.set_brightness  = true;
    }

    if(newer.set_speed)
    {
        base.speed           = newer.speed;
        base.set_speed       = true;
    }

    if(newer.set_color_index)
    {
        base.color_index     = newer.color_index;
        base.set_color_index = true;
    }

    if(newer.set_dpi_colors)
    {
        base.dpi_colors      = newer.dpi_colors;
        base.set_dpi_colors  = true;
    }
}

ATKNano8KController::ATKNano8KController(hid_device* dev_handle, const hid_device_info& info, const std::string& dev_name)
{
    dev                 = dev_handle;
    location            = info.path;
    name                = dev_name;
    serial              = info.serial_number ? StringUtils::wstring_to_string(info.serial_number) : "";
    pending_valid       = false;
    pending_attempted   = false;
    pending             = {};
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
    return(serial);
}

std::string ATKNano8KController::GetVersionString()
{
    return(version);
}

std::string ATKNano8KController::LastHidError()
{
    const wchar_t* error = hid_error(dev);

    return(error ? StringUtils::wstring_to_string(error) : std::string("unknown error"));
}

/*---------------------------------------------------------*\
| Send a request and, if a reply is wanted, wait for the    |
| input report answering it. Other input reports (DPI or    |
| rate notifications) that arrive in between are skipped.   |
\*---------------------------------------------------------*/
ATKNano8KResult ATKNano8KController::Transact(unsigned char command, const unsigned char* data, size_t data_len, unsigned char* reply, int min_reply_len)
{
    std::lock_guard<std::mutex> lock(hid_mutex);

    unsigned char buf[ATK_NANO8K_INPUT_SIZE];

    /*-----------------------------------------------------*\
    | Drop stale reports so an old reply is not taken for   |
    | the answer to this request                            |
    \*-----------------------------------------------------*/
    for(unsigned int read_idx = 0; read_idx < ATK_NANO8K_FLUSH_READS_MAX; read_idx++)
    {
        if(hid_read_timeout(dev, buf, sizeof(buf), 0) <= 0)
        {
            break;
        }
    }

    unsigned char request[ATK_NANO8K_FEATURE_SIZE] = {};
    request[1] = command;
    data_len   = std::min(data_len, sizeof(request) - 2);
    if(data_len > 0)
    {
        memcpy(&request[2], data, data_len);
    }

    if(hid_send_feature_report(dev, request, sizeof(request)) < 0)
    {
        LOG_DEBUG("[%s] Error sending command %02X: %s", name.c_str(), command, LastHidError().c_str());
        return(ATK_NANO8K_RESULT_IO_ERROR);
    }

    if(reply == nullptr)
    {
        std::this_thread::sleep_for(ATK_NANO8K_WRITE_GAP);
        return(ATK_NANO8K_RESULT_OK);
    }

    auto deadline = std::chrono::steady_clock::now() + ATK_NANO8K_REPLY_TIMEOUT;

    while(true)
    {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();

        if(left <= 0)
        {
            break;
        }

        int len = hid_read_timeout(dev, buf, sizeof(buf), (int)left);

        if(len < 0)
        {
            LOG_DEBUG("[%s] Error reading reply to command %02X: %s", name.c_str(), command, LastHidError().c_str());
            return(ATK_NANO8K_RESULT_IO_ERROR);
        }

        if(len == 0)
        {
            break;
        }

        if(buf[0] != command)
        {
            continue;
        }

        if(len < min_reply_len)
        {
            LOG_DEBUG("[%s] Short reply to command %02X: %d bytes", name.c_str(), command, len);
            continue;
        }

        memset(reply, 0, ATK_NANO8K_INPUT_SIZE);
        memcpy(reply, buf, (size_t)len);
        return(ATK_NANO8K_RESULT_OK);
    }

    LOG_DEBUG("[%s] No reply to command %02X", name.c_str(), command);
    return(ATK_NANO8K_RESULT_UNCONFIRMED);
}

ATKNano8KResult ATKNano8KController::Send(unsigned char command, const unsigned char* data, size_t data_len)
{
    return(Transact(command, data, data_len, nullptr, 0));
}

/*---------------------------------------------------------*\
| GetConfigData reply:                                      |
|   [82][rate][dpi stage][light mode][brightness][speed]    |
|   [colour index][-][battery][link]                        |
| link: 0 when no mouse is reachable (off or asleep)        |
\*---------------------------------------------------------*/
ATKNano8KResult ATKNano8KController::ReadConfig(ATKNano8KState& state)
{
    unsigned char   reply[ATK_NANO8K_INPUT_SIZE];
    ATKNano8KResult result = Transact(ATK_NANO8K_CMD_GET_CONFIG, nullptr, 0, reply, ATK_NANO8K_CONFIG_REPLY_LEN);

    state.config_valid = (result == ATK_NANO8K_RESULT_OK);
    state.linked       = false;

    if(!state.config_valid)
    {
        return(result);
    }

    state.lighting.mode         = reply[3];
    state.lighting.brightness   = reply[4];
    state.lighting.speed        = reply[5];
    state.lighting.color_index  = reply[6];
    state.linked                = (reply[9] != 0);

    return(state.linked ? ATK_NANO8K_RESULT_OK : ATK_NANO8K_RESULT_NOT_LINKED);
}

/*---------------------------------------------------------*\
| GetDpiColor (A7 00 FF) reply: [A7] then 8 x [R][G][B]     |
\*---------------------------------------------------------*/
ATKNano8KResult ATKNano8KController::ReadDpiColors(ATKNano8KState& state)
{
    unsigned char       reply[ATK_NANO8K_INPUT_SIZE];
    const unsigned char request[] = { 0x00, 0xFF };
    ATKNano8KResult     result    = Transact(ATK_NANO8K_CMD_GET_DPI_COLOR, request, sizeof(request), reply, ATK_NANO8K_DPI_COLOR_REPLY_LEN);

    state.dpi_colors_valid = (result == ATK_NANO8K_RESULT_OK);

    if(state.dpi_colors_valid)
    {
        for(unsigned int stage = 0; stage < ATK_NANO8K_DPI_STAGES; stage++)
        {
            state.lighting.dpi_colors[stage] = ToRGBColor(reply[1 + stage * 3], reply[2 + stage * 3], reply[3 + stage * 3]);
        }
    }

    return(result);
}

ATKNano8KState ATKNano8KController::ReadState()
{
    ATKNano8KState state = {};

    if(ReadConfig(state) == ATK_NANO8K_RESULT_OK)
    {
        ReadDpiColors(state);
    }

    return(state);
}

void ATKNano8KController::SetLighting(const ATKNano8KLighting& lighting)
{
    {
        std::lock_guard<std::mutex> lock(pending_mutex);

        if(pending_valid)
        {
            MergeLighting(pending, lighting);
        }
        else
        {
            pending = lighting;
        }

        pending_valid     = true;
        pending_attempted = false;
    }
    pending_cv.notify_all();
}

/*---------------------------------------------------------*\
| Write only what differs from what the mouse reports, then |
| read back to confirm the mouse took it. Fields the change |
| does not set keep the mouse's current values.             |
\*---------------------------------------------------------*/
ATKNano8KResult ATKNano8KController::ApplyLighting(const ATKNano8KLighting& lighting)
{
    ATKNano8KState  current = {};
    ATKNano8KResult result  = ReadConfig(current);

    if(result != ATK_NANO8K_RESULT_OK)
    {
        return(result);
    }

    ATKNano8KLighting target = lighting;

    if(!target.set_brightness)
    {
        target.brightness  = current.lighting.brightness;
    }

    if(!target.set_speed)
    {
        target.speed       = current.lighting.speed;
    }

    if(!target.set_color_index)
    {
        target.color_index = current.lighting.color_index;
    }

    if(target.set_dpi_colors)
    {
        result = ReadDpiColors(current);

        if(result != ATK_NANO8K_RESULT_OK)
        {
            return(result);
        }

        for(unsigned int stage = 0; stage < ATK_NANO8K_DPI_STAGES; stage++)
        {
            if(current.lighting.dpi_colors[stage] == target.dpi_colors[stage])
            {
                continue;
            }

            const unsigned char data[] =
            {
                0x00,
                (unsigned char)stage,
                (unsigned char)RGBGetRValue(target.dpi_colors[stage]),
                (unsigned char)RGBGetGValue(target.dpi_colors[stage]),
                (unsigned char)RGBGetBValue(target.dpi_colors[stage]),
            };

            result = Send(ATK_NANO8K_CMD_SET_DPI_COLOR, data, sizeof(data));

            if(result != ATK_NANO8K_RESULT_OK)
            {
                return(result);
            }
        }
    }

    if((current.lighting.mode        != target.mode)
    || (current.lighting.brightness  != target.brightness)
    || (current.lighting.speed       != target.speed)
    || (current.lighting.color_index != target.color_index))
    {
        const unsigned char data[] =
        {
            0x00,
            0x00,
            target.mode,
            target.brightness,
            target.speed,
            target.color_index,
        };

        result = Send(ATK_NANO8K_CMD_SET_RGB, data, sizeof(data));

        if(result != ATK_NANO8K_RESULT_OK)
        {
            return(result);
        }
    }

    ATKNano8KState check = {};

    result = ReadConfig(check);

    if(result != ATK_NANO8K_RESULT_OK)
    {
        return(result);
    }

    if((check.lighting.mode        != target.mode)
    || (check.lighting.brightness  != target.brightness)
    || (check.lighting.speed       != target.speed)
    || (check.lighting.color_index != target.color_index))
    {
        return(ATK_NANO8K_RESULT_UNCONFIRMED);
    }

    if(target.set_dpi_colors)
    {
        result = ReadDpiColors(check);

        if(result != ATK_NANO8K_RESULT_OK)
        {
            return(result);
        }

        if(check.lighting.dpi_colors != target.dpi_colors)
        {
            return(ATK_NANO8K_RESULT_UNCONFIRMED);
        }
    }

    return(ATK_NANO8K_RESULT_OK);
}

/*---------------------------------------------------------*\
| Writes queued changes:                                    |
|  - the first change after a quiet moment goes out at once |
|    (so a CLI call or profile load that exits right after  |
|    is not lost); changes arriving within the debounce     |
|    time after a write are coalesced until input stops     |
|  - a change that could not be written is retried, every   |
|    2 s while the mouse sleeps, with growing gaps up to    |
|    30 s while the receiver itself fails; a newer change   |
|    ends the wait at once                                  |
|  - on close, a change never attempted is written once;    |
|    one that already failed is dropped instead of holding  |
|    up the close                                           |
\*---------------------------------------------------------*/
void ATKNano8KController::WorkerThread()
{
    std::unique_lock<std::mutex>            lock(pending_mutex);
    std::chrono::steady_clock::time_point   last_write;
    std::chrono::milliseconds               io_retry = ATK_NANO8K_RETRY_INTERVAL;

    while(true)
    {
        pending_cv.wait(lock, [this]{ return(stop.load() || pending_valid); });

        if(!pending_valid || (stop && pending_attempted))
        {
            break;
        }

        ATKNano8KLighting target = pending;

        if(!stop && (std::chrono::steady_clock::now() < last_write + ATK_NANO8K_DEBOUNCE))
        {
            auto quiet_until = std::chrono::steady_clock::now() + ATK_NANO8K_DEBOUNCE;

            while(pending_cv.wait_until(lock, quiet_until, [this, &target]{ return(stop.load() || !SameLighting(target, pending)); }))
            {
                if(stop)
                {
                    break;
                }

                target      = pending;
                quiet_until = std::chrono::steady_clock::now() + ATK_NANO8K_DEBOUNCE;
            }

            target = pending;
        }

        pending_valid = false;
        lock.unlock();

        ATKNano8KResult result = ApplyLighting(target);

        /*-------------------------------------------------*\
        | Mouse answered but the read-back differs: likely  |
        | a dropped packet, so try once more right away     |
        \*-------------------------------------------------*/
        if(result == ATK_NANO8K_RESULT_UNCONFIRMED && !stop)
        {
            result = ApplyLighting(target);
        }

        lock.lock();
        last_write = std::chrono::steady_clock::now();

        if(result == ATK_NANO8K_RESULT_OK)
        {
            io_retry = ATK_NANO8K_RETRY_INTERVAL;
            continue;
        }

        /*-------------------------------------------------*\
        | Keep the change for later, under any newer one    |
        \*-------------------------------------------------*/
        if(pending_valid)
        {
            ATKNano8KLighting newer = pending;
            pending                 = target;
            MergeLighting(pending, newer);
        }
        else
        {
            pending           = target;
            pending_valid     = true;
            pending_attempted = true;
        }

        if(stop)
        {
            continue;
        }

        std::chrono::milliseconds wait = ATK_NANO8K_RETRY_INTERVAL;

        if(result == ATK_NANO8K_RESULT_IO_ERROR)
        {
            wait     = io_retry;
            io_retry = std::min<std::chrono::milliseconds>(io_retry * 2, ATK_NANO8K_IO_RETRY_MAX);
            LOG_DEBUG("[%s] Receiver error, lighting change kept, retrying in %d ms", name.c_str(), (int)wait.count());
        }
        else
        {
            io_retry = ATK_NANO8K_RETRY_INTERVAL;
            LOG_DEBUG("[%s] Mouse not reachable, lighting change kept for when it wakes", name.c_str());
        }

        ATKNano8KLighting waiting = pending;

        pending_cv.wait_for(lock, wait, [this, &waiting]{ return(stop.load() || !SameLighting(waiting, pending)); });
    }
}
