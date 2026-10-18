#pragma once

// touch7_icons.h — MyAuraPT weather icon assets (google-weather-icons v2,
// RGB565A8, LVGL v9 lv_image_dsc_t). One .c per condition, compiled as
// separate units under src/touch7_icons/.
//
// The display layer picks the asset from the Open-Meteo WMO weathercode +
// day/night, exactly like MyAuraPT's choose_image().

#include <lvgl.h>

LV_IMAGE_DECLARE(image_blizzard);
LV_IMAGE_DECLARE(image_blowing_snow);
LV_IMAGE_DECLARE(image_clear_night);
LV_IMAGE_DECLARE(image_cloudy);
LV_IMAGE_DECLARE(image_drizzle);
LV_IMAGE_DECLARE(image_flurries);
LV_IMAGE_DECLARE(image_haze_fog_dust_smoke);
LV_IMAGE_DECLARE(image_heavy_rain);
LV_IMAGE_DECLARE(image_heavy_snow);
LV_IMAGE_DECLARE(image_isolated_scattered_tstorms_day);
LV_IMAGE_DECLARE(image_isolated_scattered_tstorms_night);
LV_IMAGE_DECLARE(image_mostly_clear_night);
LV_IMAGE_DECLARE(image_mostly_cloudy_day);
LV_IMAGE_DECLARE(image_mostly_cloudy_night);
LV_IMAGE_DECLARE(image_mostly_sunny);
LV_IMAGE_DECLARE(image_partly_cloudy);
LV_IMAGE_DECLARE(image_partly_cloudy_night);
LV_IMAGE_DECLARE(image_scattered_showers_day);
LV_IMAGE_DECLARE(image_scattered_showers_night);
LV_IMAGE_DECLARE(image_showers_rain);
LV_IMAGE_DECLARE(image_sleet_hail);
LV_IMAGE_DECLARE(image_snow_showers_snow);
LV_IMAGE_DECLARE(image_strong_tstorms);
LV_IMAGE_DECLARE(image_sunny);
LV_IMAGE_DECLARE(image_tornado);
LV_IMAGE_DECLARE(image_wintry_mix_rain_snow);

// WMO weathercode -> Aura icon (same table as MyAuraPT).
static inline const lv_image_dsc_t* t7ChooseImage(int code, bool isDay) {
    switch (code) {
        case 0:  return isDay ? &image_sunny : &image_clear_night;
        case 1:  return isDay ? &image_mostly_sunny : &image_mostly_clear_night;
        case 2:  return isDay ? &image_partly_cloudy : &image_partly_cloudy_night;
        case 3:  return &image_cloudy;
        case 45: case 48: return &image_haze_fog_dust_smoke;
        case 51: case 53: case 55: return &image_drizzle;
        case 56: case 57: return &image_sleet_hail;
        case 61: return isDay ? &image_scattered_showers_day : &image_scattered_showers_night;
        case 63: return &image_showers_rain;
        case 65: return &image_heavy_rain;
        case 66: case 67: return &image_wintry_mix_rain_snow;
        case 71: case 73: case 75: case 85: return &image_snow_showers_snow;
        case 77: return &image_flurries;
        case 80: case 81: return isDay ? &image_scattered_showers_day : &image_scattered_showers_night;
        case 82: return &image_heavy_rain;
        case 86: return &image_heavy_snow;
        case 95: return isDay ? &image_isolated_scattered_tstorms_day : &image_isolated_scattered_tstorms_night;
        case 96: case 99: return &image_strong_tstorms;
        default: return isDay ? &image_mostly_cloudy_day : &image_mostly_cloudy_night;
    }
}
