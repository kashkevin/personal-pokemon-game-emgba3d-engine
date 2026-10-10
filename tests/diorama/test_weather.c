#include <stdio.h>
#include <stdlib.h>

#include "constants/weather.h"
#include "diorama/weather.h"

#define CHECK(condition) Check((condition), #condition, __LINE__)

static void Check(bool condition, const char *expression, int line)
{
    if (!condition)
    {
        fprintf(stderr, "test_weather.c:%d: check failed: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    CHECK(DioramaWeather_Classify(WEATHER_NONE) == DIORAMA_WEATHER_CLEAR);
    CHECK(DioramaWeather_Classify(WEATHER_SUNNY) == DIORAMA_WEATHER_CLEAR);
    CHECK(DioramaWeather_Classify(WEATHER_SHADE) == DIORAMA_WEATHER_CLEAR);
    CHECK(DioramaWeather_CanRender(WEATHER_SHADE, WEATHER_SHADE));
    CHECK(DioramaWeather_Classify(WEATHER_RAIN) == DIORAMA_WEATHER_RAIN);
    CHECK(DioramaWeather_Classify(WEATHER_FOG_HORIZONTAL) == DIORAMA_WEATHER_FOG);
    CHECK(DioramaWeather_Classify(WEATHER_VOLCANIC_ASH) == DIORAMA_WEATHER_ASH);
    /* Every weather the game shows is drawn in 3D (the graphics overhaul). */
    CHECK(DioramaWeather_Classify(WEATHER_DOWNPOUR) == DIORAMA_WEATHER_RAIN);
    CHECK(DioramaWeather_Classify(WEATHER_RAIN_THUNDERSTORM) == DIORAMA_WEATHER_RAIN);
    CHECK(DioramaWeather_Classify(WEATHER_SUNNY_CLOUDS) == DIORAMA_WEATHER_CLEAR);
    CHECK(DioramaWeather_Classify(WEATHER_DROUGHT) == DIORAMA_WEATHER_CLEAR);
    CHECK(DioramaWeather_Classify(WEATHER_FOG_DIAGONAL) == DIORAMA_WEATHER_FOG);
    CHECK(DioramaWeather_Classify(WEATHER_SNOW) == DIORAMA_WEATHER_SNOW);
    CHECK(DioramaWeather_Classify(WEATHER_SANDSTORM) == DIORAMA_WEATHER_SAND);
    CHECK(DioramaWeather_Classify(WEATHER_UNDERWATER_BUBBLES) == DIORAMA_WEATHER_BUBBLES);
    CHECK(DioramaWeather_Classify(WEATHER_UNDERWATER) == DIORAMA_WEATHER_BUBBLES);
    for (int weather = 0; weather < WEATHER_COUNT; weather++)
        CHECK(DioramaWeather_Classify(weather) != DIORAMA_WEATHER_UNSUPPORTED);
    CHECK(DioramaWeather_Classify(WEATHER_COUNT) == DIORAMA_WEATHER_UNSUPPORTED);
    CHECK(DioramaWeather_CanRender(WEATHER_RAIN, WEATHER_SUNNY));
    CHECK(DioramaWeather_CanRender(WEATHER_VOLCANIC_ASH, WEATHER_VOLCANIC_ASH));
    CHECK(DioramaWeather_CanRender(WEATHER_RAIN, WEATHER_RAIN_THUNDERSTORM));
    CHECK(DioramaWeather_CanRender(WEATHER_SANDSTORM, WEATHER_SNOW));
    CHECK(!DioramaWeather_CanRender(WEATHER_RAIN, WEATHER_COUNT));
    CHECK(DioramaWeather_RainDropCount(0) == 0);
    CHECK(DioramaWeather_RainDropCount(10) == 10);
    CHECK(DioramaWeather_RainDropCount(30) == 24);
    CHECK(DioramaWeather_FogAlpha(0) == 0);
    CHECK(DioramaWeather_FogAlpha(8) == 64);
    CHECK(DioramaWeather_FogAlpha(12) == 96);
    puts("weather tests passed");
    return EXIT_SUCCESS;
}
