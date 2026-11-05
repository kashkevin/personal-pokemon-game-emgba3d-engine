/*
 * The PC game's volume controls: see include/platform/audio_volume.h.
 * Project-owned (no third-party code). Its state (the levels, which category each music player and mixer channel plays)
 * is platform state and survives a soft reset (Makefile_pc PRESERVED_OBJS), as the settings it comes from do.
 */
#include <string.h>

#include "platform/audio_volume.h"
#include "constants/songs.h"

const char *const gAudioVolumeKeys[AUDIO_CONTROL_COUNT] = {
    [AUDIO_MASTER] = "volume",
    [AUDIO_MUSIC] = "musicVolume",
    [AUDIO_EFFECTS] = "sfxVolume",
    [AUDIO_CRIES] = "cryVolume",
};

static const unsigned char sDefaults[AUDIO_CONTROL_COUNT] = {
    [AUDIO_MASTER] = 10,
    [AUDIO_MUSIC] = 7,  // the owner's decision of 2026-11-05
    [AUDIO_EFFECTS] = 10,
    [AUDIO_CRIES] = 10,
};

void AudioVolumes_Defaults(struct AudioVolumes *volumes)
{
    memcpy(volumes->level, sDefaults, sizeof(volumes->level));
    volumes->newKeys = false;
}

static bool IsBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// As the launcher reads a line (the line stripped, the key before '=', the value all digits, 0 to 10).
bool AudioVolumes_ParseLine(struct AudioVolumes *volumes, const char *line)
{
    while (*line == ' ' || *line == '\t')
        line++;
    for (int control = 0; control < AUDIO_CONTROL_COUNT; control++)
    {
        size_t length = strlen(gAudioVolumeKeys[control]);
        const char *value;
        const char *end;
        unsigned int number = 0;
        bool digits;

        if (strncmp(line, gAudioVolumeKeys[control], length) != 0 || line[length] != '=')
            continue;
        if (control == AUDIO_MUSIC || control == AUDIO_EFFECTS)
            volumes->newKeys = true; // even with a value that is ignored: the file is of the new kind
        value = end = line + length + 1;
        while (*end >= '0' && *end <= '9' && number <= AUDIO_VOLUME_MAX)
            number = number * 10 + (unsigned int)(*end++ - '0');
        digits = end != value;
        while (IsBlank(*end))
            end++;
        if (digits && *end == '\0' && number <= AUDIO_VOLUME_MAX)
            volumes->level[control] = (unsigned char)number;
        return true;
    }
    return false;
}

void AudioVolumes_Finish(struct AudioVolumes *volumes)
{
    if (!volumes->newKeys && volumes->level[AUDIO_CRIES] == AUDIO_VOLUME_OLD_CRY_DEFAULT)
        volumes->level[AUDIO_CRIES] = sDefaults[AUDIO_CRIES];
}

void AudioVolumes_Write(const struct AudioVolumes *volumes, FILE *file)
{
    for (int control = 0; control < AUDIO_CONTROL_COUNT; control++)
        fprintf(file, "%s=%u\n", gAudioVolumeKeys[control], (unsigned int)volumes->level[control]);
}

/* ---- The mixers' side -------------------------------------------------------------------------------------------- */

#define MAX_PLAYERS 8 // the four of the song table and the two cry players

unsigned int gAudioVolumeScaledMixes;

// The gain of each category (the master's entry is not used here), which category each music player plays, and each
// mixer channel's (from its last note). The game thread reads them; the settings menu writes the gains between frames.
static float sGain[AUDIO_CONTROL_COUNT] = {1.0f, 0.7f, 1.0f, 1.0f};
static struct
{
    const void *player;
    unsigned char category;
} sPlayers[MAX_PLAYERS];
static unsigned char sChannelCategory[AUDIO_CHANNELS] = {
    [0 ... AUDIO_CHANNELS - 1] = AUDIO_EFFECTS,
};

void AudioVolume_SetLevels(const struct AudioVolumes *volumes)
{
    for (int control = 0; control < AUDIO_CONTROL_COUNT; control++)
    {
        unsigned int level = volumes->level[control] <= AUDIO_VOLUME_MAX ? volumes->level[control] : AUDIO_VOLUME_MAX;

        // 10 / 10.0f is 1.0f exactly: a channel at 100% takes the mixers' unchanged path.
        sGain[control] = (float)level / (float)AUDIO_VOLUME_MAX;
    }
}

enum AudioVolumeControl AudioVolume_SongCategory(unsigned int song)
{
    return song < START_MUS ? AUDIO_EFFECTS : AUDIO_MUSIC;
}

void AudioVolume_PlayerStarted(const void *player, enum AudioVolumeControl category)
{
    int free = -1;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (sPlayers[i].player == player)
        {
            sPlayers[i].category = (unsigned char)category;
            return;
        }
        if (sPlayers[i].player == NULL && free < 0)
            free = i;
    }
    if (free >= 0)
    {
        sPlayers[free].player = player;
        sPlayers[free].category = (unsigned char)category;
    }
}

enum AudioVolumeControl AudioVolume_PlayerCategory(const void *player)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (sPlayers[i].player == player)
            return (enum AudioVolumeControl)sPlayers[i].category;
    }
    return AUDIO_EFFECTS; // a player no song was started on (none in the game)
}

void AudioVolume_NoteStarted(int channel, const void *player)
{
    if (channel >= 0 && channel < AUDIO_CHANNELS)
        sChannelCategory[channel] = (unsigned char)AudioVolume_PlayerCategory(player);
}

enum AudioVolumeControl AudioVolume_ChannelCategory(int channel)
{
    return channel >= 0 && channel < AUDIO_CHANNELS ? (enum AudioVolumeControl)sChannelCategory[channel] : AUDIO_EFFECTS;
}

float AudioVolume_ChannelGain(int channel)
{
    return sGain[AudioVolume_ChannelCategory(channel)];
}

void AudioVolume_ApplyMaster(float *samples, int count, unsigned int level)
{
    // The mixer leaves output headroom, so full volume sounds quiet. Apply a make-up gain and hard-clamp to [-1,1] so
    // loud passages can't clip. Measured with the audio audit (PKMN_DEBUG_SCENE=audio, all 479 sounds): music peaks 0.06
    // at the median and 0.115 at most, a sound effect 0.04 / 0.10, so the old gain of 2 left music near -18 dBFS peak.
    // Gain 4 puts the loudest music at 0.46 and music plus the loudest effect at 0.86: still under full scale, no
    // clipping in the whole library. Music, sound effects and cries were scaled by their own controls (at most 1) in the
    // mixers already.
    float volume = (level / 10.0f) * 4.0f;

    for (int i = 0; i < count; i++)
    {
        float s = samples[i] * volume;
        if (s > 1.0f) s = 1.0f;
        else if (s < -1.0f) s = -1.0f;
        samples[i] = s;
    }
}
