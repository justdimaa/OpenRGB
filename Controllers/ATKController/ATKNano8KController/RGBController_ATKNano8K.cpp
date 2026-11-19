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

#include <cstdlib>
#include "RGBController_ATKNano8K.h"

/*---------------------------------------------------------*\
| Raw brightness and speed steps used by ATK HUB            |
\*---------------------------------------------------------*/
static const unsigned char atk_nano8k_brightness[] = { 16, 127, 255 };
static const unsigned char atk_nano8k_speed[]      = { 1, 3, 5 };

#define ATK_NANO8K_BRIGHTNESS_STEPS     (sizeof(atk_nano8k_brightness) / sizeof(atk_nano8k_brightness[0]))
#define ATK_NANO8K_SPEED_STEPS          (sizeof(atk_nano8k_speed) / sizeof(atk_nano8k_speed[0]))

/*---------------------------------------------------------*\
| Fixed colours 0-7, as seen on an A9 Pro                   |
\*---------------------------------------------------------*/
static const RGBColor atk_nano8k_palette[ATK_NANO8K_PALETTE_SIZE] =
{
    ToRGBColor(255,   0,   0),  /* red          */
    ToRGBColor(255, 128,   0),  /* orange       */
    ToRGBColor(255, 255,   0),  /* yellow       */
    ToRGBColor(  0, 255,   0),  /* green        */
    ToRGBColor(  0,   0, 255),  /* blue         */
    ToRGBColor(  0, 255, 255),  /* cyan         */
    ToRGBColor(255,   0, 160),  /* pink-magenta */
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

static unsigned int NearestStep(unsigned char value, const unsigned char* steps, unsigned int count)
{
    unsigned int best = 0;

    for(unsigned int idx = 1; idx < count; idx++)
    {
        if(abs((int)steps[idx] - (int)value) < abs((int)steps[best] - (int)value))
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
        is saved on the mouse. Any colour is shown by writing it to the
        DPI stage colours, so Static, Color Steps, Breathing and Reactive
        replace the DPI indicator colours; DPI Breathing sets them back.
        With the light Off, the mouse also hides its low battery warning.
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
    Static.flags                    = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_AUTOMATIC_SAVE;
    Static.color_mode               = MODE_COLORS_MODE_SPECIFIC;
    Static.colors_min               = 1;
    Static.colors_max               = 1;
    Static.colors.resize(1, atk_nano8k_palette[0]);
    Static.brightness_min           = 0;
    Static.brightness_max           = ATK_NANO8K_BRIGHTNESS_STEPS - 1;
    Static.brightness               = ATK_NANO8K_BRIGHTNESS_STEPS - 1;
    modes.push_back(Static);

    mode ColorSteps;
    ColorSteps.name                 = "Color Steps";
    ColorSteps.value                = ATK_NANO8K_MODE_COLOR_STEPS;
    ColorSteps.flags                = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_AUTOMATIC_SAVE;
    ColorSteps.color_mode           = MODE_COLORS_MODE_SPECIFIC;
    ColorSteps.colors_min           = 1;
    ColorSteps.colors_max           = ATK_NANO8K_DPI_STAGES;
    ColorSteps.colors.assign(atk_nano8k_default_dpi_colors, atk_nano8k_default_dpi_colors + 4);
    ColorSteps.brightness_min       = 0;
    ColorSteps.brightness_max       = ATK_NANO8K_BRIGHTNESS_STEPS - 1;
    ColorSteps.brightness           = ATK_NANO8K_BRIGHTNESS_STEPS - 1;
    modes.push_back(ColorSteps);

    mode Breathing;
    Breathing.name                  = "Breathing";
    Breathing.value                 = ATK_NANO8K_MODE_BREATHING;
    Breathing.flags                 = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_HAS_SPEED | MODE_FLAG_AUTOMATIC_SAVE;
    Breathing.color_mode            = MODE_COLORS_MODE_SPECIFIC;
    Breathing.colors_min            = 1;
    Breathing.colors_max            = ATK_NANO8K_DPI_STAGES;
    Breathing.colors.resize(1, atk_nano8k_palette[0]);
    Breathing.brightness_min        = 0;
    Breathing.brightness_max        = ATK_NANO8K_BRIGHTNESS_STEPS - 1;
    Breathing.brightness            = ATK_NANO8K_BRIGHTNESS_STEPS - 1;
    Breathing.speed_min             = 0;
    Breathing.speed_max             = ATK_NANO8K_SPEED_STEPS - 1;
    Breathing.speed                 = 1;
    modes.push_back(Breathing);

    mode SpectrumCycle;
    SpectrumCycle.name              = "Spectrum Cycle";
    SpectrumCycle.value             = ATK_NANO8K_MODE_SPECTRUM_CYCLE;
    SpectrumCycle.flags             = MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_HAS_SPEED | MODE_FLAG_AUTOMATIC_SAVE;
    SpectrumCycle.color_mode        = MODE_COLORS_NONE;
    SpectrumCycle.brightness_min    = 0;
    SpectrumCycle.brightness_max    = ATK_NANO8K_BRIGHTNESS_STEPS - 1;
    SpectrumCycle.brightness        = ATK_NANO8K_BRIGHTNESS_STEPS - 1;
    SpectrumCycle.speed_min         = 0;
    SpectrumCycle.speed_max         = ATK_NANO8K_SPEED_STEPS - 1;
    SpectrumCycle.speed             = 1;
    modes.push_back(SpectrumCycle);

    mode Reactive;
    Reactive.name                   = "Reactive";
    Reactive.value                  = ATK_NANO8K_MODE_REACTIVE;
    Reactive.flags                  = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_AUTOMATIC_SAVE;
    Reactive.color_mode             = MODE_COLORS_MODE_SPECIFIC;
    Reactive.colors_min             = 1;
    Reactive.colors_max             = 1;
    Reactive.colors.resize(1, atk_nano8k_palette[0]);
    Reactive.brightness_min         = 0;
    Reactive.brightness_max         = ATK_NANO8K_BRIGHTNESS_STEPS - 1;
    Reactive.brightness             = ATK_NANO8K_BRIGHTNESS_STEPS - 1;
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
    DpiBreathing.brightness_max     = ATK_NANO8K_BRIGHTNESS_STEPS - 1;
    DpiBreathing.brightness         = ATK_NANO8K_BRIGHTNESS_STEPS - 1;
    DpiBreathing.speed_min          = 0;
    DpiBreathing.speed_max          = ATK_NANO8K_SPEED_STEPS - 1;
    DpiBreathing.speed              = 1;
    modes.push_back(DpiBreathing);

    mode Off;
    Off.name                        = "Off";
    Off.value                       = ATK_NANO8K_MODE_OFF;
    Off.flags                       = MODE_FLAG_AUTOMATIC_SAVE;
    Off.color_mode                  = MODE_COLORS_NONE;
    modes.push_back(Off);

    SetupZones();

    /*-----------------------------------------------------*\
    | Start from what the mouse currently shows             |
    \*-----------------------------------------------------*/
    last_lighting                   = {};
    last_lighting.mode              = ATK_NANO8K_LIGHT_STATIC;
    last_lighting.brightness        = atk_nano8k_brightness[ATK_NANO8K_BRIGHTNESS_STEPS - 1];
    last_lighting.speed             = atk_nano8k_speed[1];
    last_lighting.color_index       = 0;
    for(unsigned int stage = 0; stage < ATK_NANO8K_DPI_STAGES; stage++)
    {
        last_lighting.dpi_colors[stage] = atk_nano8k_default_dpi_colors[stage];
    }

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

void RGBController_ATKNano8K::LoadDeviceState(const ATKNano8KState& state)
{
    if(!state.config_valid || !state.linked)
    {
        return;
    }

    const ATKNano8KLighting& lighting   = state.lighting;
    unsigned int brightness_step        = NearestStep(lighting.brightness, atk_nano8k_brightness, ATK_NANO8K_BRIGHTNESS_STEPS);
    unsigned int speed_step             = NearestStep(lighting.speed, atk_nano8k_speed, ATK_NANO8K_SPEED_STEPS);
    unsigned int stage_count            = state.dpi_count;

    if(stage_count < 1 || stage_count > ATK_NANO8K_DPI_STAGES)
    {
        stage_count = ATK_NANO8K_DPI_STAGES;
    }

    last_lighting.mode                  = lighting.mode;
    last_lighting.brightness            = lighting.brightness;
    last_lighting.speed                 = lighting.speed;
    last_lighting.color_index           = lighting.color_index;

    /*-----------------------------------------------------*\
    | Colours the light shows: the active DPI stage colours |
    | for index 8, else the fixed palette colour            |
    \*-----------------------------------------------------*/
    std::vector<RGBColor> shown;
    bool all_same = true;

    if(state.dpi_colors_valid)
    {
        last_lighting.dpi_colors = lighting.dpi_colors;

        for(unsigned int stage = 1; stage < stage_count; stage++)
        {
            if(lighting.dpi_colors[stage] != lighting.dpi_colors[0])
            {
                all_same = false;
            }
        }

        /*-------------------------------------------------*\
        | Stage colours that are all the same were written  |
        | by Static or Reactive, not chosen as DPI colours: |
        | keep the factory set for DPI Breathing then       |
        \*-------------------------------------------------*/
        if(!all_same || lighting.mode == ATK_NANO8K_LIGHT_DPI_BREATHING)
        {
            modes[ATK_NANO8K_MODE_DPI_BREATHING].colors.assign(lighting.dpi_colors.begin(), lighting.dpi_colors.end());
        }
    }

    if(lighting.color_index == ATK_NANO8K_COLOR_INDEX_DPI && state.dpi_colors_valid)
    {
        if(all_same)
        {
            shown.push_back(lighting.dpi_colors[0]);
        }
        else
        {
            shown.assign(lighting.dpi_colors.begin(), lighting.dpi_colors.begin() + stage_count);
        }
    }
    else
    {
        shown.push_back(atk_nano8k_palette[lighting.color_index < ATK_NANO8K_PALETTE_SIZE ? lighting.color_index : 0]);
    }

    unsigned int new_mode;

    switch(lighting.mode)
    {
        case ATK_NANO8K_LIGHT_OFF:
            new_mode = ATK_NANO8K_MODE_OFF;
            break;

        case ATK_NANO8K_LIGHT_BREATHING:
            new_mode = ATK_NANO8K_MODE_BREATHING;
            modes[new_mode].colors = shown;
            break;

        case ATK_NANO8K_LIGHT_NEON:
            new_mode = ATK_NANO8K_MODE_SPECTRUM_CYCLE;
            break;

        case ATK_NANO8K_LIGHT_REACTIVE:
            new_mode = ATK_NANO8K_MODE_REACTIVE;
            modes[new_mode].colors.assign(1, shown[0]);
            break;

        case ATK_NANO8K_LIGHT_DPI_BREATHING:
            new_mode = ATK_NANO8K_MODE_DPI_BREATHING;
            break;

        case ATK_NANO8K_LIGHT_STATIC:
        default:
            if(shown.size() > 1)
            {
                new_mode = ATK_NANO8K_MODE_COLOR_STEPS;
                modes[new_mode].colors = shown;
            }
            else
            {
                new_mode = ATK_NANO8K_MODE_STATIC;
                modes[new_mode].colors.assign(1, shown[0]);
            }
            break;
    }

    for(unsigned int mode_idx = 0; mode_idx < modes.size(); mode_idx++)
    {
        if(modes[mode_idx].flags & MODE_FLAG_HAS_BRIGHTNESS)
        {
            modes[mode_idx].brightness = brightness_step;
        }

        if(modes[mode_idx].flags & MODE_FLAG_HAS_SPEED)
        {
            modes[mode_idx].speed = speed_step;
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
    const mode& current         = modes[active_mode];
    ATKNano8KLighting lighting  = last_lighting;
    lighting.set_dpi_colors     = false;

    /*-----------------------------------------------------*\
    | Spread the chosen colours over the DPI stages; the    |
    | light steps through them with colour index 8          |
    \*-----------------------------------------------------*/
    auto fill_stages = [&lighting](const std::vector<RGBColor>& colors)
    {
        for(unsigned int stage = 0; stage < ATK_NANO8K_DPI_STAGES; stage++)
        {
            lighting.dpi_colors[stage] = colors.empty() ? atk_nano8k_palette[0] : colors[stage % colors.size()];
        }
        lighting.color_index    = ATK_NANO8K_COLOR_INDEX_DPI;
        lighting.set_dpi_colors = true;
    };

    switch(current.value)
    {
        case ATK_NANO8K_MODE_STATIC:
            lighting.mode = ATK_NANO8K_LIGHT_STATIC;
            fill_stages(current.colors);
            break;

        case ATK_NANO8K_MODE_COLOR_STEPS:
            lighting.mode = ATK_NANO8K_LIGHT_STATIC;
            fill_stages(current.colors);
            break;

        case ATK_NANO8K_MODE_BREATHING:
            lighting.mode = ATK_NANO8K_LIGHT_BREATHING;
            fill_stages(current.colors);
            break;

        case ATK_NANO8K_MODE_SPECTRUM_CYCLE:
            lighting.mode = ATK_NANO8K_LIGHT_NEON;
            break;

        case ATK_NANO8K_MODE_REACTIVE:
            lighting.mode = ATK_NANO8K_LIGHT_REACTIVE;
            fill_stages(current.colors);
            break;

        case ATK_NANO8K_MODE_DPI_BREATHING:
            lighting.mode = ATK_NANO8K_LIGHT_DPI_BREATHING;
            fill_stages(current.colors);
            break;

        case ATK_NANO8K_MODE_OFF:
        default:
            lighting.mode = ATK_NANO8K_LIGHT_OFF;
            break;
    }

    if((current.flags & MODE_FLAG_HAS_BRIGHTNESS) && current.brightness < ATK_NANO8K_BRIGHTNESS_STEPS)
    {
        lighting.brightness = atk_nano8k_brightness[current.brightness];
    }

    if((current.flags & MODE_FLAG_HAS_SPEED) && current.speed < ATK_NANO8K_SPEED_STEPS)
    {
        lighting.speed = atk_nano8k_speed[current.speed];
    }

    if(lighting.set_dpi_colors)
    {
        last_lighting.dpi_colors = lighting.dpi_colors;
    }
    last_lighting.mode          = lighting.mode;
    last_lighting.brightness    = lighting.brightness;
    last_lighting.speed         = lighting.speed;
    last_lighting.color_index   = lighting.color_index;

    controller->SetLighting(lighting);
}
