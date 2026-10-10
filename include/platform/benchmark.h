#ifndef GUARD_PLATFORM_BENCHMARK_H
#define GUARD_PLATFORM_BENCHMARK_H

/*
 * --benchmark: the 3D view's frame rate at each resolution of the display
 * settings (1280x720, 1920x1080, 2560x1440, 3840x2160), measured on the
 * player's own PC. The game boots and Continues by itself (Start past the
 * intro, then A every 3 seconds until the 3D field is drawn, so the save should
 * stand somewhere outdoors; after 90 seconds without it the measurement starts
 * anyway and the log says no frame was 3D), then the scene is drawn at each
 * resolution in turn (offscreen when the window is smaller, so 4K is measured
 * on any display), VSync off, with the player's quality settings; frames are
 * counted after a warm-up and benchmark.log is written (average, 1% low,
 * frame-time percentiles, how many frames were 3D, the GPU and CPU), then the
 * game quits. Nothing is saved: the game's own Flash write never happens
 * without the player's Save.
 *
 * The bookkeeping here is pure (tools/pc_port/tests checks it); sdl2.c feeds it
 * the frames and does what it says.
 */
#include <stdbool.h>
#include <stdio.h>

#define BENCHMARK_BOOT_MAX_MS 90000
#define BENCHMARK_SETTLE_MS 3000   /* after the first 3D frame (the map name fades) */
#define BENCHMARK_WARMUP_MS 3000
#define BENCHMARK_DEFAULT_SECONDS 15
#define BENCHMARK_MAX_FRAMES 20000

struct BenchmarkResult
{
    int width;
    int height;
    int frames;
    int frames3d;
    double seconds;
    double averageFps;
    double lowFps;       /* 1% low: the frame rate at the 99th percentile frame time */
    double medianMs;
    double p99Ms;
    double worstMs;
};

void Benchmark_Start(unsigned int nowMs, int secondsPerResolution);
bool Benchmark_Active(void);
/* The buttons the benchmark presses while the game boots, added to the player's. */
unsigned short Benchmark_FilterKeys(unsigned short keys, unsigned int nowMs);
/* The scene's render height now (0: not measuring yet). */
int Benchmark_RenderHeight(unsigned int nowMs);
/* One presented frame: how long it took and whether it drew the 3D scene. */
void Benchmark_Frame(unsigned int nowMs, double frameMs, bool frame3d);
bool Benchmark_Finished(unsigned int nowMs);
int Benchmark_Results(struct BenchmarkResult *results, int max);
/* Statistics of one set of frame times (sorts them). */
void Benchmark_Summarize(double *frameMs, int count, double seconds, struct BenchmarkResult *result);
void Benchmark_WriteResults(FILE *file);

#endif
