#ifndef GUARD_NATIVE_UI_BENCH_H
#define GUARD_NATIVE_UI_BENCH_H

/*
 * PKMN_NATIVE_UI_BENCH=<results.csv>: the native UI's frame-time benchmark
 * (src/native_ui/ui_bench.c). Runs with the UI on and off (PKMN_NATIVE_UI=0).
 */
#include <stdbool.h>
#include <stdint.h>

void UiBench_Init(bool uiOn);
bool UiBench_Active(void);
/* frame boundary: the script's buttons added to the player's */
uint16_t UiBench_Keys(uint16_t keys);
/* every presented frame (the UI pass's CPU time of the frame before) */
void UiBench_Frame(float uiCpuMs);
/* a UI pass's GPU time, when its timer query is ready */
void UiBench_Gpu(float gpuMs);

#endif // GUARD_NATIVE_UI_BENCH_H
