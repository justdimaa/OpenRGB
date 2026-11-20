/*---------------------------------------------------------*\
| RGBController_ATKNano8K.cpp                               |
|                                                           |
|   RGBController for ATK mice on the ATK 8K Nano Dongle    |
|                                                           |
|   justdimaa                                   20 Nov 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <algorithm>
#include <cstdlib>
#include "RGBController_ATKNano8K.h"

#define ATK_NANO8K_BRIGHTNESS_MAX       255

/*---------------------------------------------------------*\
| Raw speed steps used by ATK HUB (slow, medium, fast)      |
\*---------------------------------------------------------*/
static const unsigned char atk_nano8k_speed[]   = { 1, 3, 5 };

#define ATK_NANO8K_SPEED_STEPS          (sizeof(atk_nano8k_speed) / sizeof(atk_nano8k_speed[0]))
#define ATK_NANO8K_SPEED_DEFAULT        1

/*---------------------------------------------------------*\
| Fixed colours 0-7 as seen on an A9 Pro                    |
\*---------------------------------------------------------*/
static const RGBColor atk_nano8k_palette[ATK_NANO8K_PALETTE_SIZE] =
{
    ToRGBColor(255,   0,   0),  /* red          */
    ToRGBColor(255, 128,   0),  /* orange       */
    ToRGBColor(255, 255,   0),  /* yellow       */
    ToRGBColor(  0, 255,   0),  /* green        */
    ToRGBColor(  0,   0, 255),  /* blue         */
    ToRGBColor(  0, 255, 255),  /* cyan         */
    ToRGBColor(255,   0, 255),  /* pink-magenta */
    ToRGBColor(255, 255, 255),  /* white        */
};

/*---------------------------------------------------------*\
| Factory DPI stage colours                                 |
\*---------------------------------------------------------*/
static const RGBColor atk_nano8k_default_dpi_colors[ATK_NANO8K_DPI_STAGES] =
{
    ToRGBColor(255,   0,   0),
    ToRGBColor(  0, 255,   0),
    ToRGBColor(  0,   0, 255),
    ToRGBColor(139,   0, 255),
    ToRGBColor(  0, 255, 255),
    ToRGBColor(  0, 255, 255),
    ToRGBColor(255, 255, 255),
    ToRGBColor(255, 255, 255),
};

/*---------------------------------------------------------*\
| Palette entry closest in hue to a chosen colour. The      |
| colour is scaled to full intensity first, since the mouse |
| sets intensity through brightness, not through the colour.|
\*---------------------------------------------------------*/
static unsigned char NearestPaletteIndex(RGBColor color)
{
    int r   = RGBGetRValue(color);
    int g   = RGBGetGValue(color);
    int b   = RGBGetBValue(color);
    int max = std::max(r, std::max(g, b));

    if(max == 0)
    {
        return(ATK_NANO8K_PALETTE_SIZE - 1);
    }

    r = r * 255 / max;
    g = g * 255 / max;
    b = b * 255 / max;

    unsigned char best      = 0;
    int           best_dist = -1;

    for(unsigned char idx = 0; idx < ATK_NANO8K_PALETTE_SIZE; idx++)
    {
        int dr   = r - (int)RGBGetRValue(atk_nano8k_palette[idx]);
        int dg   = g - (int)RGBGetGValue(atk_nano8k_palette[idx]);
        int db   = b - (int)RGBGetBValue(atk_nano8k_palette[idx]);
        int dist = dr * dr + dg * dg + db * db;

        if(best_dist < 0 || dist < best_dist)
        {
            best      = idx;
            best_dist = dist;
        }
    }

    return(best);
}

static unsigned int NearestSpeedStep(unsigned char value)
{
    unsigned int best = 0;

    for(unsigned int idx = 1; idx < ATK_NANO8K_SPEED_STEPS; idx++)
    {
        if(abs((int)atk_nano8k_speed[idx] - (int)value) < abs((int)atk_nano8k_speed[best] - (int)value))
        {
            best = idx;
        }
    }

    return(best);
}

/**------------------------------------------------------------------*\
    @name ATK 8K Nano Dongle Mouse
    @category Mouse
    @type USB
    @save :robot:
    @direct :x:
    @effects :white_check_mark:
    @detectors DetectATKNano8KControllers
    @comment Mice on the ATK 8K Nano Dongle (373B:1178), such as the ATK
        A9 Pro. The mouse stores its lighting itself, so every change
        is saved on the mouse. Static and Breathing show one of eight
        fixed colours (the closest to the one chosen), or step through
        all of them with Random. DPI Breathing takes any colour per DPI
        stage. Reactive picks its own colours.
\*-------------------------------------------------------------------*/
RGBController_ATKNano8K::RGBController_ATKNano8K(ATKNano8KController* controller_ptr)
{
    controller                      = controller_ptr;

    name                            = controller->GetNameString();
    vendor                          = "ATK";
    type                            = DEVICE_TYPE_MOUSE;
    description                     = "ATK Mouse on 8K Nano Dongle";
    version                         = controller->GetVersionString();
    serial                          = controller->GetSerialString();
    location                        = controller->GetDeviceLocation();

    mode Static;
    Static.name                     = "Static";
    Static.value                    = ATK_NANO8K_MODE_STATIC;
    Static.flags                    = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_RANDOM_COLOR | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_AUTOMATIC_SAVE;
    Static.color_mode               = MODE_COLORS_MODE_SPECIFIC;
    Static.colors_min               = 1;
    Static.colors_max               = 1;
    Static.colors.resize(1, atk_nano8k_palette[0]);
    Static.brightness_min           = 0;
    Static.brightness_max           = ATK_NANO8K_BRIGHTNESS_MAX;
    Static.brightness               = ATK_NANO8K_BRIGHTNESS_MAX;
    modes.push_back(Static);

    mode Breathing;
    Breathing.name                  = "Breathing";
    Breathing.value                 = ATK_NANO8K_MODE_BREATHING;
    Breathing.flags                 = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_RANDOM_COLOR | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_HAS_SPEED | MODE_FLAG_AUTOMATIC_SAVE;
    Breathing.color_mode            = MODE_COLORS_MODE_SPECIFIC;
    Breathing.colors_min            = 1;
    Breathing.colors_max            = 1;
    Breathing.colors.resize(1, atk_nano8k_palette[0]);
    Breathing.brightness_min        = 0;
    Breathing.brightness_max        = ATK_NANO8K_BRIGHTNESS_MAX;
    Breathing.brightness            = ATK_NANO8K_BRIGHTNESS_MAX;
    Breathing.speed_min             = 0;
    Breathing.speed_max             = ATK_NANO8K_SPEED_STEPS - 1;
    Breathing.speed                 = ATK_NANO8K_SPEED_DEFAULT;
    modes.push_back(Breathing);

    mode SpectrumCycle;
    SpectrumCycle.name              = "Spectrum Cycle";
    SpectrumCycle.value             = ATK_NANO8K_MODE_SPECTRUM_CYCLE;
    SpectrumCycle.flags             = MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_HAS_SPEED | MODE_FLAG_AUTOMATIC_SAVE;
    SpectrumCycle.color_mode        = MODE_COLORS_NONE;
    SpectrumCycle.brightness_min    = 0;
    SpectrumCycle.brightness_max    = ATK_NANO8K_BRIGHTNESS_MAX;
    SpectrumCycle.brightness        = ATK_NANO8K_BRIGHTNESS_MAX;
    SpectrumCycle.speed_min         = 0;
    SpectrumCycle.speed_max         = ATK_NANO8K_SPEED_STEPS - 1;
    SpectrumCycle.speed             = ATK_NANO8K_SPEED_DEFAULT;
    modes.push_back(SpectrumCycle);

    mode Reactive;
    Reactive.name                   = "Reactive";
    Reactive.value                  = ATK_NANO8K_MODE_REACTIVE;
    Reactive.flags                  = MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_AUTOMATIC_SAVE;
    Reactive.color_mode             = MODE_COLORS_NONE;
    Reactive.brightness_min         = 0;
    Reactive.brightness_max         = ATK_NANO8K_BRIGHTNESS_MAX;
    Reactive.brightness             = ATK_NANO8K_BRIGHTNESS_MAX;
    modes.push_back(Reactive);

    mode DpiBreathing;
    DpiBreathing.name               = "DPI Breathing";
    DpiBreathing.value              = ATK_NANO8K_MODE_DPI_BREATHING;
    DpiBreathing.flags              = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_HAS_SPEED | MODE_FLAG_AUTOMATIC_SAVE;
    DpiBreathing.color_mode         = MODE_COLORS_MODE_SPECIFIC;
    DpiBreathing.colors_min         = ATK_NANO8K_DPI_STAGES;
    DpiBreathing.colors_max         = ATK_NANO8K_DPI_STAGES;
    DpiBreathing.colors.assign(atk_nano8k_default_dpi_colors, atk_nano8k_default_dpi_colors + ATK_NANO8K_DPI_STAGES);
    DpiBreathing.brightness_min     = 0;
    DpiBreathing.brightness_max     = ATK_NANO8K_BRIGHTNESS_MAX;
    DpiBreathing.brightness         = ATK_NANO8K_BRIGHTNESS_MAX;
    DpiBreathing.speed_min          = 0;
    DpiBreathing.speed_max          = ATK_NANO8K_SPEED_STEPS - 1;
    DpiBreathing.speed              = ATK_NANO8K_SPEED_DEFAULT;
    modes.push_back(DpiBreathing);

    mode Off;
    Off.name                        = "Off";
    Off.value                       = ATK_NANO8K_MODE_OFF;
    Off.flags                       = MODE_FLAG_AUTOMATIC_SAVE;
    Off.color_mode                  = MODE_COLORS_NONE;
    modes.push_back(Off);

    SetupZones();

    last_lighting                   = {};
    last_lighting.mode              = ATK_NANO8K_LIGHT_STATIC;
    last_lighting.brightness        = ATK_NANO8K_BRIGHTNESS_MAX;
    last_lighting.speed             = atk_nano8k_speed[ATK_NANO8K_SPEED_DEFAULT];
    last_lighting.color_index       = 0;

    active_mode                     = ATK_NANO8K_MODE_STATIC;

    LoadDeviceState(controller->ReadState());
}

RGBController_ATKNano8K::~RGBController_ATKNano8K()
{
    Shutdown();

    delete controller;
}

void RGBController_ATKNano8K::SetupZones()
{
    zone mouse_zone;
    mouse_zone.name                 = "Mouse";
    mouse_zone.type                 = ZONE_TYPE_SINGLE;
    mouse_zone.leds_min             = 1;
    mouse_zone.leds_max             = 1;
    mouse_zone.leds_count           = 1;
    zones.push_back(mouse_zone);

    led mouse_led;
    mouse_led.name                  = "Logo";
    mouse_led.value                 = 0;
    leds.push_back(mouse_led);

    SetupColors();
}

/*---------------------------------------------------------*\
| Start from what the mouse currently shows                 |
\*---------------------------------------------------------*/
void RGBController_ATKNano8K::LoadDeviceState(const ATKNano8KState& state)
{
    if(!state.config_valid || !state.linked)
    {
        return;
    }

    const ATKNano8KLighting& lighting   = state.lighting;
    bool                     cycle      = lighting.color_index >= ATK_NANO8K_PALETTE_SIZE;
    RGBColor                 color      = atk_nano8k_palette[cycle ? 0 : lighting.color_index];

    last_lighting.mode                  = lighting.mode;
    last_lighting.brightness            = lighting.brightness;
    last_lighting.speed                 = lighting.speed;
    last_lighting.color_index           = lighting.color_index;

    if(state.dpi_colors_valid)
    {
        modes[ATK_NANO8K_MODE_DPI_BREATHING].colors.assign(lighting.dpi_colors.begin(), lighting.dpi_colors.end());
    }

    unsigned int new_mode;

    switch(lighting.mode)
    {
        case ATK_NANO8K_LIGHT_OFF:
            new_mode = ATK_NANO8K_MODE_OFF;
            break;

        case ATK_NANO8K_LIGHT_BREATHING:
            new_mode = ATK_NANO8K_MODE_BREATHING;
            break;

        case ATK_NANO8K_LIGHT_NEON:
            new_mode = ATK_NANO8K_MODE_SPECTRUM_CYCLE;
            break;

        case ATK_NANO8K_LIGHT_REACTIVE:
            new_mode = ATK_NANO8K_MODE_REACTIVE;
            break;

        case ATK_NANO8K_LIGHT_DPI_BREATHING:
            new_mode = ATK_NANO8K_MODE_DPI_BREATHING;
            break;

        case ATK_NANO8K_LIGHT_STATIC:
        default:
            new_mode = ATK_NANO8K_MODE_STATIC;
            break;
    }

    if(modes[new_mode].flags & MODE_FLAG_HAS_RANDOM_COLOR)
    {
        modes[new_mode].color_mode = cycle ? MODE_COLORS_RANDOM : MODE_COLORS_MODE_SPECIFIC;
        modes[new_mode].colors.assign(1, color);
    }

    for(unsigned int mode_idx = 0; mode_idx < modes.size(); mode_idx++)
    {
        if(modes[mode_idx].flags & MODE_FLAG_HAS_BRIGHTNESS)
        {
            modes[mode_idx].brightness = lighting.brightness;
        }

        if(modes[mode_idx].flags & MODE_FLAG_HAS_SPEED)
        {
            modes[mode_idx].speed = NearestSpeedStep(lighting.speed);
        }
    }

    active_mode = new_mode;
}

/*---------------------------------------------------------*\
| There is no per-LED (direct) control: every change is a   |
| stored setting on the mouse, so colours go through modes. |
\*---------------------------------------------------------*/
void RGBController_ATKNano8K::DeviceUpdateLEDs()
{
}

void RGBController_ATKNano8K::DeviceUpdateZoneLEDs(int /*zone*/)
{
}

void RGBController_ATKNano8K::DeviceUpdateSingleLED(int /*led*/)
{
}

void RGBController_ATKNano8K::DeviceUpdateMode()
{
    mode& current               = modes[active_mode];
    ATKNano8KLighting lighting  = last_lighting;
    lighting.set_dpi_colors     = false;

    switch(current.value)
    {
        case ATK_NANO8K_MODE_STATIC:
            lighting.mode = ATK_NANO8K_LIGHT_STATIC;
            break;

        case ATK_NANO8K_MODE_BREATHING:
            lighting.mode = ATK_NANO8K_LIGHT_BREATHING;
            break;

        case ATK_NANO8K_MODE_SPECTRUM_CYCLE:
            lighting.mode = ATK_NANO8K_LIGHT_NEON;
            break;

        case ATK_NANO8K_MODE_REACTIVE:
            lighting.mode = ATK_NANO8K_LIGHT_REACTIVE;
            break;

        case ATK_NANO8K_MODE_DPI_BREATHING:
            lighting.mode = ATK_NANO8K_LIGHT_DPI_BREATHING;
            for(unsigned int stage = 0; stage < ATK_NANO8K_DPI_STAGES && stage < current.colors.size(); stage++)
            {
                lighting.dpi_colors[stage] = current.colors[stage];
            }
            lighting.set_dpi_colors = current.colors.size() >= ATK_NANO8K_DPI_STAGES;
            break;

        case ATK_NANO8K_MODE_OFF:
        default:
            lighting.mode = ATK_NANO8K_LIGHT_OFF;
            break;
    }

    /*-----------------------------------------------------*\
    | Static and Breathing: one of the fixed colours, or    |
    | the built-in cycle through all of them for Random.    |
    | The chosen colour is replaced by the one the mouse    |
    | shows, so OpenRGB displays what is really on.         |
    \*-----------------------------------------------------*/
    if(current.flags & MODE_FLAG_HAS_RANDOM_COLOR)
    {
        if(current.color_mode == MODE_COLORS_RANDOM)
        {
            lighting.color_index = ATK_NANO8K_COLOR_INDEX_CYCLE;
        }
        else if(!current.colors.empty())
        {
            lighting.color_index = NearestPaletteIndex(current.colors[0]);
            current.colors[0]    = atk_nano8k_palette[lighting.color_index];
        }
    }

    if(current.flags & MODE_FLAG_HAS_BRIGHTNESS)
    {
        lighting.brightness = (unsigned char)std::min<unsigned int>(current.brightness, ATK_NANO8K_BRIGHTNESS_MAX);
    }

    if((current.flags & MODE_FLAG_HAS_SPEED) && current.speed < ATK_NANO8K_SPEED_STEPS)
    {
        lighting.speed = atk_nano8k_speed[current.speed];
    }

    last_lighting.mode          = lighting.mode;
    last_lighting.brightness    = lighting.brightness;
    last_lighting.speed         = lighting.speed;
    last_lighting.color_index   = lighting.color_index;

    controller->SetLighting(lighting);
}
