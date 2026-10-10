#ifdef ENABLE_DIORAMA

#include "constants/weather.h"
#include "diorama/weather.h"

enum DioramaWeatherEffect DioramaWeather_Classify(uint8_t weather)
{
    switch (weather)
    {
    case WEATHER_NONE:
    case WEATHER_SUNNY:
    case WEATHER_SUNNY_CLOUDS:
    /* The woods' shade, the drought's glare and a thunderstorm's lightning
     * are the palettes' own (darkened, brightened, flashed), which the 3D
     * view reads from the live palette RAM like the GBA's picture. */
    case WEATHER_SHADE:
    case WEATHER_DROUGHT:
    /* Weathers the game resolves to another before it shows them. */
    case WEATHER_ROUTE119_CYCLE:
    case WEATHER_ROUTE123_CYCLE:
    case WEATHER_ABNORMAL:
    case WEATHER_DYNAMIC:
        return DIORAMA_WEATHER_CLEAR;
    case WEATHER_RAIN:
    case WEATHER_RAIN_THUNDERSTORM:
    case WEATHER_DOWNPOUR:
        return DIORAMA_WEATHER_RAIN;
    case WEATHER_FOG_HORIZONTAL:
    case WEATHER_FOG_DIAGONAL:
    case WEATHER_FOG:
        return DIORAMA_WEATHER_FOG;
    case WEATHER_VOLCANIC_ASH:
        return DIORAMA_WEATHER_ASH;
    case WEATHER_SNOW:
        return DIORAMA_WEATHER_SNOW;
    case WEATHER_SANDSTORM:
        return DIORAMA_WEATHER_SAND;
    case WEATHER_UNDERWATER:
    case WEATHER_UNDERWATER_BUBBLES:
        return DIORAMA_WEATHER_BUBBLES;
    default:
        return DIORAMA_WEATHER_UNSUPPORTED;
    }
}

bool DioramaWeather_CanRender(uint8_t currentWeather, uint8_t nextWeather)
{
    return DioramaWeather_Classify(currentWeather) != DIORAMA_WEATHER_UNSUPPORTED
        && DioramaWeather_Classify(nextWeather) != DIORAMA_WEATHER_UNSUPPORTED;
}

uint8_t DioramaWeather_RainDropCount(uint8_t visibleCount)
{
    return visibleCount > 24 ? 24 : visibleCount;
}

uint8_t DioramaWeather_FogAlpha(uint16_t blendEVA)
{
    return blendEVA >= 12 ? 96 : blendEVA * 8;
}

#endif
