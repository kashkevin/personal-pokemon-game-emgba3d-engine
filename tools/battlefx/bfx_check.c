/*
 * bfx_check: runs the Battle FX package's tests/engine_check.js inside the
 * game's own C host (src/diorama/battlefx/bfx_host.c, QuickJS-ng) and prints
 * its JSON report - the same report the package's bfx_engine_check prints, so
 * tools/battlefx/compare_reports.py can hold it against the package's
 * expected results (every move's frame count and per-frame runtime hash).
 *
 *   bfx_check <bfx_core.js> <bfx_moves.json> <engine_check.js> [MOVE_A,MOVE_B,...|all] [doubles|general]
 *   BFX_PERF=1: time every move instead (BFX_ENGINE_PERF)
 *
 * Built by tools/battlefx/build_check.sh for the game's three targets (Linux
 * x86-64, Linux i386 and Windows i686, the last run under Wine): the game is
 * 32-bit, so this checks that QuickJS-ng's 32-bit builds play every animation
 * exactly as the 64-bit ones and Node do.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diorama/battle_fx_host.h"

static char *ReadFile(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long n;

    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)n + 1);
    if (data == NULL || fread(data, 1, (size_t)n, f) != (size_t)n)
    {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    data[n] = '\0';
    *size = (size_t)n;
    return data;
}

int main(int argc, char **argv)
{
    struct BfxHost *host;
    size_t coreSize, dataSize, checkSize;
    char *core, *data, *check, *out;
    char opts[65536];
    const char *perf = getenv("BFX_PERF");
    size_t len;

    if (argc < 4)
    {
        fprintf(stderr, "usage: %s bfx_core.js bfx_moves.json engine_check.js [moves|all] [doubles|general]\n", argv[0]);
        return 2;
    }
    core = ReadFile(argv[1], &coreSize);
    data = ReadFile(argv[2], &dataSize);
    check = ReadFile(argv[3], &checkSize);
    if (core == NULL || data == NULL || check == NULL)
    {
        fprintf(stderr, "cannot read the core, the data or the check script\n");
        return 1;
    }
    host = BfxHost_Create();
    if (!BfxHost_Init(host, core, coreSize, data, dataSize, 512 * 1024))
    {
        fprintf(stderr, "init failed: %s\n", BfxHost_LastError(host));
        return 1;
    }
    if (!BfxHost_Eval(host, check, checkSize, "engine_check.js"))
    {
        fprintf(stderr, "%s\n", BfxHost_LastError(host));
        return 1;
    }
    len = (size_t)snprintf(opts, sizeof(opts), "{\"engine\":\"quickjs-ng 0.10.1\"");
    if (argc > 4 && strcmp(argv[4], "all") != 0)
    {
        const char *p = argv[4];
        bool first = true;

        len += (size_t)snprintf(opts + len, sizeof(opts) - len, ",\"moves\":[");
        while (*p && len < sizeof(opts) - 64)
        {
            const char *q = strchr(p, ',');
            size_t n = q ? (size_t)(q - p) : strlen(p);
            if (n > 0)
                len += (size_t)snprintf(opts + len, sizeof(opts) - len, "%s\"%.*s\"", first ? "" : ",", (int)n, p);
            first = false;
            p += n + (q ? 1 : 0);
        }
        len += (size_t)snprintf(opts + len, sizeof(opts) - len, "]");
    }
    if (argc > 5 && strcmp(argv[5], "doubles") == 0)
        len += (size_t)snprintf(opts + len, sizeof(opts) - len, ",\"setup\":{\"double\":true,\"attacker\":0,\"target\":3}");
    if (argc > 5 && strcmp(argv[5], "general") == 0)
        len += (size_t)snprintf(opts + len, sizeof(opts) - len, ",\"general\":true");
    snprintf(opts + len, sizeof(opts) - len, "}");
    out = BfxHost_CallGlobal(host, perf && *perf && *perf != '0' ? "BFX_ENGINE_PERF" : "BFX_ENGINE_CHECK", opts);
    if (out == NULL)
    {
        fprintf(stderr, "%s\n", BfxHost_LastError(host));
        return 1;
    }
    fwrite(out, 1, strlen(out), stdout);
    fputc('\n', stdout);
    free(out);
    BfxHost_Destroy(host);
    free(core);
    free(data);
    free(check);
    return 0;
}
