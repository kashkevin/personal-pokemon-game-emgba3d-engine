// Native PC build: the volume controls (include/platform/audio_volume.h): the owner's decision of 2026-11-05 (app
// docs/owner-decisions.md #45): music at 70% by default, sound effects at 100%, independent master, music and sound effect
// controls; the Pokemon cries keep a control of their own. Project-owned.
//   --selftest-audio-volumes
// 1. Which control each sound answers to: every song number below START_MUS a sound effect, every one from it on music
//    (the fanfares and jingles too, though they play on sound effect player 2), a player keeping the category of the song
//    it really plays, the cries.
// 2. The game's own mixer (m4a: sound_mixer.c's sample channels, cgb_audio.c's GB channels): a music song, a sound
//    effect, a fanfare and a cry, each from a fresh sound engine for two seconds. At 100% every channel goes through the
//    mixers' unchanged path, and no other control changes a sample; at each level of its own control every sample is
//    level/10 of the 100% mix, at 0% exactly silent; then the master after the mix (x master/10, the x4 make-up gain, the
//    clamp), at 100% bit for bit the old master stage.
// 3. The settings file read and written by the game itself (sdl2.c ReadConfigFile, StoreConfigFile): its defaults, a file
//    of this build, an older file (no musicVolume= or sfxVolume=), junk; the cases of the app's
//    tests/test_game_display_settings.py, so both read a file the same way.
// 4. The F10 menu's Audio page (the 3D build), through its keys as the game handles them: a change heard at once (the
//    mixer's gain, the master) and saved.
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "gba/m4a_internal.h" // before cgb_audio.h (see cgb_audio.c)
#include "cgb_audio.h"
#include "m4a.h"
#include "sound.h"
#include "constants/songs.h"
#include "constants/sound.h"
#include "constants/species.h"
#include "platform/audio_volume.h"
#include "platform/pc_menu.h"

// The game's own pieces this test drives (sdl2.c's are there for this test).
extern float audioBuffer[];                 // music_player.c: the frame m4aSoundVSync hands to Platform_QueueAudio
extern u32 apuFrame;                        // cgb_audio.c: the GB frame sequencer's place
extern u8 apuCycle;
extern struct MusicPlayerInfo *gMPlay_PokemonCry;
void Platform_TestSettingsFile(const char *path);
void Platform_TestStoreSettings(void);
const struct AudioVolumes *Platform_AudioVolumes(void);
#ifdef ENABLE_DIORAMA
bool Platform_TestPcMenuKey(int key);
int Platform_TestPcMenuRows(struct PcMenuRow *rows, const char **title);
#endif

#define RENDER_FRAMES 120 // two seconds
#define MAX_FRAME_FLOATS (MIXED_AUDIO_BUFFER_SIZE * 2)
#define SETTINGS_FILE "audio-volumes-selftest.cfg"

static int sChecks, sFailures;

static void Expect(const char *what, bool ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void Expectf(bool ok, const char *format, ...)
{
    char text[400];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Expect(text, ok);
}

// ---- The sounds --------------------------------------------------------------------------------------------------------

struct TestSound
{
    const char *name;
    int song;                      // a song number, or -1: the cry of `species`
    u16 species;
    enum AudioVolumeControl control;
};

static const struct TestSound sSounds[] = {
    {"music: Route 101 (MUS_ROUTE101, the background music player)", MUS_ROUTE101, 0, AUDIO_MUSIC},
    {"a sound effect: SE_PC_LOGIN (sound effect player 1)", SE_PC_LOGIN, 0, AUDIO_EFFECTS},
    {"a fanfare: MUS_LEVEL_UP (on sound effect player 2, music by its number)", MUS_LEVEL_UP, 0, AUDIO_MUSIC},
    {"a cry: Pikachu's (the cry players)", -1, SPECIES_PIKACHU, AUDIO_CRIES},
};

static const char *const sControlNames[AUDIO_CONTROL_COUNT] = {"master", "music", "sound effects", "cries"};

static void SetLevels(unsigned master, unsigned music, unsigned effects, unsigned cries)
{
    struct AudioVolumes volumes;

    AudioVolumes_Defaults(&volumes);
    volumes.level[AUDIO_MASTER] = (unsigned char)master;
    volumes.level[AUDIO_MUSIC] = (unsigned char)music;
    volumes.level[AUDIO_EFFECTS] = (unsigned char)effects;
    volumes.level[AUDIO_CRIES] = (unsigned char)cries;
    AudioVolume_SetLevels(&volumes);
}

// Every control at 100% but `control` at `level`.
static void SetOneLevel(enum AudioVolumeControl control, unsigned level)
{
    unsigned levels[AUDIO_CONTROL_COUNT] = {10, 10, 10, 10};

    levels[control] = level;
    SetLevels(levels[AUDIO_MASTER], levels[AUDIO_MUSIC], levels[AUDIO_EFFECTS], levels[AUDIO_CRIES]);
}

// The sound engine as the game starts it (m4aSoundInit after power-on): the sound registers cleared, the GB channels'
// emulation and frame sequencer at their start.
static void FreshSoundEngine(void)
{
    memset(&REG_BASE[REG_OFFSET_SOUND1CNT_L], 0, REG_OFFSET_FIFO_A - REG_OFFSET_SOUND1CNT_L);
    apuFrame = 0;
    apuCycle = 0;
    cgb_audio_init(42060);
    m4aSoundInit();
}

static int FrameFloats(void)
{
    return SOUND_INFO_PTR->pcmSamplesPerVBlank * 2;
}

static void Start(const struct TestSound *sound)
{
    if (sound->song >= 0)
        m4aSongNumStart((u16)sound->song);
    else
        PlayCry_NormalNoDucking(sound->species, 0, CRY_VOLUME, CRY_PRIORITY_NORMAL);
}

// The sound from a fresh engine, frame by frame as m4aSoundVSync hands it to Platform_QueueAudio (before the master).
// usedSample/usedGb: how many sample and GB channels its notes took (the channels are first marked with another category).
static int Render(const struct TestSound *sound, float *out, int *usedSample, int *usedGb)
{
    static int sProbePlayer;
    enum AudioVolumeControl probe = sound->control == AUDIO_MUSIC ? AUDIO_CRIES : AUDIO_MUSIC;
    int total = 0;

    FreshSoundEngine();
    AudioVolume_PlayerStarted(&sProbePlayer, probe);
    for (int channel = 0; channel < AUDIO_CHANNELS; channel++)
        AudioVolume_NoteStarted(channel, &sProbePlayer);
    Start(sound);
    for (int frame = 0; frame < RENDER_FRAMES; frame++)
    {
        int floats = FrameFloats();

        m4aSoundMain();
        m4aSoundVSync();
        memcpy(out + total, audioBuffer, (size_t)floats * sizeof(float));
        total += floats;
    }
    if (usedSample != NULL && usedGb != NULL)
    {
        *usedSample = *usedGb = 0;
        for (int channel = 0; channel < AUDIO_CHANNELS; channel++)
        {
            if (AudioVolume_ChannelCategory(channel) != probe)
                (channel < AUDIO_SAMPLE_CHANNELS ? (*usedSample)++ : (*usedGb)++);
        }
    }
    m4aMPlayAllStop();
    return total;
}

static float Peak(const float *samples, int count)
{
    float peak = 0.0f;

    for (int i = 0; i < count; i++)
        peak = fabsf(samples[i]) > peak ? fabsf(samples[i]) : peak;
    return peak;
}

// The largest difference between `samples` and `reference` x gain, and whether every sample is within the rounding of
// float arithmetic of it.
static bool ScaledBy(const float *samples, const float *reference, int count, float gain, double *worst)
{
    bool ok = true;

    *worst = 0.0;
    for (int i = 0; i < count; i++)
    {
        double expected = (double)reference[i] * gain;
        double error = fabs((double)samples[i] - expected);

        if (error > *worst)
            *worst = error;
        if (error > 1e-6 + 1e-5 * fabs(expected))
            ok = false;
    }
    return ok;
}

static bool Silent(const float *samples, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (samples[i] != 0.0f)
            return false;
    }
    return true;
}

// The master stage of every earlier build (sdl2.c Platform_QueueAudio before the volume controls), for the comparison.
static void OldMasterStage(float *samples, int count, u8 setting)
{
    float volume = (setting / 10.0f) * 4.0f;

    for (int i = 0; i < count; i++)
    {
        float s = samples[i] * volume;
        if (s > 1.0f) s = 1.0f;
        else if (s < -1.0f) s = -1.0f;
        samples[i] = s;
    }
}

// ---- 1. Which control each sound answers to ---------------------------------------------------------------------------

static void TestCategories(void)
{
    int effects = 0, music = 0, onEffectPlayers = 0, wrong = 0;
    static const u16 sFanfares[] = {MUS_LEVEL_UP, MUS_HEAL, MUS_OBTAIN_ITEM, MUS_EVOLVED, MUS_OBTAIN_BADGE, MUS_OBTAIN_TMHM};

    printf("1. which control each sound answers to\n");
    for (unsigned song = 0; song <= LAST_PHONEME_SONG; song++)
    {
        enum AudioVolumeControl category = AudioVolume_SongCategory(song);

        if (category != (song < START_MUS ? AUDIO_EFFECTS : AUDIO_MUSIC))
            wrong++;
        if (category == AUDIO_EFFECTS)
            effects++;
        else
        {
            music++;
            if (gSongTable[song].ms != 0) // not the background music player
                onEffectPlayers++;
        }
    }
    Expectf(wrong == 0 && effects == START_MUS && music == LAST_PHONEME_SONG + 1 - START_MUS,
            "every song number: below %d a sound effect (%d), from it on music (%d)", START_MUS, effects, music);
    Expectf(onEffectPlayers > 0, "%d music songs play on a sound effect player (the fanfares and jingles, the bard's "
            "phonemes): music by their number", onEffectPlayers);
    for (unsigned i = 0; i < ARRAY_COUNT(sFanfares); i++)
        Expectf(gSongTable[sFanfares[i]].ms == 2 && AudioVolume_SongCategory(sFanfares[i]) == AUDIO_MUSIC,
                "fanfare %u: on sound effect player 2, music", (unsigned)sFanfares[i]);
    Expect("SE_SELECT is a sound effect, MUS_ROUTE101 and the bard's PH_TRAP_BLEND music",
           AudioVolume_SongCategory(SE_SELECT) == AUDIO_EFFECTS && AudioVolume_SongCategory(MUS_ROUTE101) == AUDIO_MUSIC
           && AudioVolume_SongCategory(PH_TRAP_BLEND) == AUDIO_MUSIC);

    // The players, as songs start on them.
    FreshSoundEngine();
    Expect("before any song: the background music player music, the others sound effects, the cry players cries",
           AudioVolume_PlayerCategory(&gMPlayInfo_BGM) == AUDIO_MUSIC
           && AudioVolume_PlayerCategory(&gMPlayInfo_SE1) == AUDIO_EFFECTS
           && AudioVolume_PlayerCategory(&gMPlayInfo_SE2) == AUDIO_EFFECTS
           && AudioVolume_PlayerCategory(&gPokemonCryMusicPlayers[0]) == AUDIO_CRIES
           && AudioVolume_PlayerCategory(&gPokemonCryMusicPlayers[1]) == AUDIO_CRIES);
    m4aSongNumStart(MUS_LEVEL_UP);
    Expect("a fanfare started on sound effect player 2: the player plays music",
           AudioVolume_PlayerCategory(&gMPlayInfo_SE2) == AUDIO_MUSIC);
    for (int frame = 0; frame < 10; frame++)
    {
        m4aSoundMain();
        m4aSoundVSync();
    }
    {
        int musicChannels = 0, others = 0;

        for (int channel = 0; channel < AUDIO_CHANNELS; channel++)
        {
            struct MixerSource *chan = channel < AUDIO_SAMPLE_CHANNELS
                                     ? &((struct SoundMixerState *)SOUND_INFO_PTR)->chans[channel]
                                     : &((struct SoundMixerState *)SOUND_INFO_PTR)->cgbChans[channel - AUDIO_SAMPLE_CHANNELS];

            if ((chan->status & SOUND_CHANNEL_SF_ON) == 0)
                continue;
            (AudioVolume_ChannelCategory(channel) == AUDIO_MUSIC ? musicChannels++ : others++);
        }
        Expectf(musicChannels > 0 && others == 0, "its notes: %d channels, every one music", musicChannels);
    }
    // A sound effect on the same player: the player plays it only if its priority allows; its category is then the
    // effect's, otherwise still the fanfare's.
    m4aSongNumStart(SE_SELECT);
    Expectf(AudioVolume_PlayerCategory(&gMPlayInfo_SE2)
            == (gMPlayInfo_SE2.songHeader == gSongTable[SE_SELECT].header ? AUDIO_EFFECTS : AUDIO_MUSIC),
            "SE_SELECT on the same player (%s): the player's category is the song it plays",
            gMPlayInfo_SE2.songHeader == gSongTable[SE_SELECT].header ? "it took the player" : "refused, the fanfare plays on");
    m4aMPlayAllStop();
    m4aSongNumStart(SE_SELECT);
    Expect("SE_SELECT on the free player: sound effects", AudioVolume_PlayerCategory(&gMPlayInfo_SE2) == AUDIO_EFFECTS);
    m4aSongNumStart(MUS_ROUTE101);
    Expect("Route 101 on the background music player: music", AudioVolume_PlayerCategory(&gMPlayInfo_BGM) == AUDIO_MUSIC);
    gMPlay_PokemonCry = NULL;
    PlayCry_NormalNoDucking(SPECIES_PIKACHU, 0, CRY_VOLUME, CRY_PRIORITY_NORMAL);
    Expect("a cry: its player plays cries", gMPlay_PokemonCry != NULL && AudioVolume_PlayerCategory(gMPlay_PokemonCry) == AUDIO_CRIES);
    m4aMPlayAllStop();
}

// ---- 2. Through the mixer -----------------------------------------------------------------------------------------------

static void TestMixer(void)
{
    static const unsigned sLevels[] = {9, 7, 5, 3, 1, 0};
    int floats = RENDER_FRAMES * MAX_FRAME_FLOATS;
    float *reference = malloc(sizeof(float) * (size_t)floats);
    float *render = malloc(sizeof(float) * (size_t)floats);
    float *staged = malloc(sizeof(float) * (size_t)floats);

    printf("2. through the game's mixer, %d frames from a fresh sound engine\n", RENDER_FRAMES);
    if (reference == NULL || render == NULL || staged == NULL)
    {
        Expect("memory for the renders", false);
        return;
    }
    for (unsigned s = 0; s < ARRAY_COUNT(sSounds); s++)
    {
        const struct TestSound *sound = &sSounds[s];
        enum AudioVolumeControl control = sound->control;
        unsigned before;
        int count, usedSample, usedGb;
        float peak;

        printf("%s, the %s control\n", sound->name, sControlNames[control]);
        SetLevels(10, 10, 10, 10);
        before = gAudioVolumeScaledMixes;
        count = Render(sound, reference, &usedSample, &usedGb);
        peak = Peak(reference, count);
        Expectf(peak > 0.001f, "  at 100%%: it plays (peak %.4f; %d sample channels, %d GB channels)", peak, usedSample, usedGb);
        Expect("  at 100%: every channel mixed by the mixers' unchanged path (none at a gain)",
               gAudioVolumeScaledMixes == before);
        if (control == AUDIO_MUSIC && s == 0)
            Expect("  (the music song uses sample and GB channels both)", usedSample > 0 && usedGb > 0);

        // The other two controls, even at 0%, change nothing of it.
        SetLevels(10, control == AUDIO_MUSIC ? 10 : 0, control == AUDIO_EFFECTS ? 10 : 0, control == AUDIO_CRIES ? 10 : 0);
        Render(sound, render, NULL, NULL);
        Expect("  the other controls at 0%: bit for bit the 100% mix", memcmp(render, reference, sizeof(float) * (size_t)count) == 0);

        for (unsigned l = 0; l < ARRAY_COUNT(sLevels); l++)
        {
            double worst = 0.0;
            bool scaled;

            SetOneLevel(control, sLevels[l]);
            before = gAudioVolumeScaledMixes;
            Render(sound, render, NULL, NULL);
            if (sLevels[l] == 0)
            {
                Expect("  at 0%: silent, every sample 0", Silent(render, count) && gAudioVolumeScaledMixes != before);
                continue;
            }
            scaled = ScaledBy(render, reference, count, sLevels[l] / 10.0f, &worst); // before `worst` is printed
            Expectf(scaled && gAudioVolumeScaledMixes != before,
                    "  at %u%%: every sample %u/10 of the 100%% mix (largest difference %.1e, float rounding)",
                    sLevels[l] * 10, sLevels[l], worst);
        }

        // The master after the mix: its own 70% render through the master stage.
        SetOneLevel(control, 7);
        Render(sound, render, NULL, NULL);
        for (unsigned master = 0; master <= 10; master += 5)
        {
            double worst;
            bool ok;

            memcpy(staged, render, sizeof(float) * (size_t)count);
            AudioVolume_ApplyMaster(staged, count, master);
            ok = master == 0 ? Silent(staged, count) : ScaledBy(staged, reference, count, 0.7f * master / 10.0f * 4.0f, &worst);
            Expectf(ok, "  %s 70%%, master %u%%: every sample 0.7 x %u/10 x 4 (the make-up gain) of the 100%% mix",
                    sControlNames[control], master * 10, master);
        }
        memcpy(staged, reference, sizeof(float) * (size_t)count);
        memcpy(render, reference, sizeof(float) * (size_t)count);
        AudioVolume_ApplyMaster(staged, count, 10);
        OldMasterStage(render, count, 10);
        Expect("  every control at 100%: the master stage bit for bit the old one",
               memcmp(staged, render, sizeof(float) * (size_t)count) == 0);
    }

    // The clamp: a sample past full scale after the make-up gain stays at full scale.
    {
        float loud[4] = {0.3f, -0.3f, 0.2f, -0.2f};

        AudioVolume_ApplyMaster(loud, 4, 10);
        Expect("the master clamps to [-1, 1] after the x4 make-up gain",
               loud[0] == 1.0f && loud[1] == -1.0f && loud[2] == 0.8f && loud[3] == -0.8f);
    }
    free(reference);
    free(render);
    free(staged);
    SetLevels(10, 7, 10, 10);
}

// ---- 3. The settings file -----------------------------------------------------------------------------------------------

static void WriteSettings(const char *text)
{
    FILE *file = fopen(SETTINGS_FILE, "w");

    if (file != NULL)
    {
        fputs(text, file);
        fclose(file);
    }
}

static bool Levels(unsigned master, unsigned music, unsigned effects, unsigned cries)
{
    const struct AudioVolumes *volumes = Platform_AudioVolumes();

    return volumes->level[AUDIO_MASTER] == master && volumes->level[AUDIO_MUSIC] == music
        && volumes->level[AUDIO_EFFECTS] == effects && volumes->level[AUDIO_CRIES] == cries;
}

static void ReadSettings(const char *text)
{
    if (text == NULL)
        remove(SETTINGS_FILE);
    else
        WriteSettings(text);
    Platform_TestSettingsFile(SETTINGS_FILE);
}

static void StoredText(char *out, size_t size)
{
    FILE *file;
    size_t length = 0;

    Platform_TestStoreSettings();
    file = fopen(SETTINGS_FILE, "r");
    if (file != NULL)
    {
        length = fread(out, 1, size - 1, file);
        fclose(file);
    }
    out[length] = '\0';
}

// The gain a channel playing `category` is mixed at now.
static float GainOf(enum AudioVolumeControl category)
{
    static int sPlayer;

    AudioVolume_PlayerStarted(&sPlayer, category);
    AudioVolume_NoteStarted(0, &sPlayer);
    return AudioVolume_ChannelGain(0);
}

static void TestSettingsFile(void)
{
    char stored[2048];

    printf("3. the settings file, as the game reads and writes it\n");
    ReadSettings(NULL);
    Expect("no file: master 100%, music 70%, sound effects 100%, cries 100%", Levels(10, 7, 10, 10));
    Expect("... and the mixer plays music at 0.7, sound effects and cries at 1",
           GainOf(AUDIO_MUSIC) == 0.7f && GainOf(AUDIO_EFFECTS) == 1.0f && GainOf(AUDIO_CRIES) == 1.0f);

    ReadSettings("border=1\nvolume=8\nmusicVolume=3\nsfxVolume=6\ncryVolume=5\nresolution=1920x1080\n");
    Expect("a file of this build: as written (a chosen cryVolume=5 kept)", Levels(8, 3, 6, 5));
    Expect("... the mixer at those levels", GainOf(AUDIO_MUSIC) == 0.3f && GainOf(AUDIO_EFFECTS) == 0.6f
           && GainOf(AUDIO_CRIES) == 0.5f);
    ReadSettings("volume=0\nmusicVolume=10\nsfxVolume=0\ncryVolume=0\n");
    Expect("a file of this build: 0% and 100% as written", Levels(0, 10, 0, 0));
    ReadSettings("sfxVolume=9\n");
    Expect("one of the new keys is enough to make it a file of this build", Levels(10, 7, 9, 10));

    ReadSettings("border=1\nvolume=10\ncryVolume=5\nresolution=1920x1080\n");
    Expect("an older file (volume= and cryVolume=5, every earlier build's): music 70%, effects 100%, its unapplied "
           "cryVolume=5 read as 100%", Levels(10, 7, 10, 10));
    ReadSettings("volume=4\ncryVolume=5\n");
    Expect("an older file's volume= stays the master", Levels(4, 7, 10, 10));
    ReadSettings("volume=6\ncryVolume=2\n");
    Expect("an older file's own cry volume (not 5) is kept, now applied", Levels(6, 7, 10, 2) && GainOf(AUDIO_CRIES) == 0.2f);
    ReadSettings("volume=6\ncryVolume=0\n");
    Expect("... 0 too", Levels(6, 7, 10, 0) && GainOf(AUDIO_CRIES) == 0.0f);
    ReadSettings("volume=6\n");
    Expect("an older file without cryVolume=: the default", Levels(6, 7, 10, 10));

    ReadSettings("volume=7\ncryVolume=eleven\n");
    Expect("junk ignored: cryVolume=eleven", Levels(7, 7, 10, 10));
    ReadSettings("volume=11\ncryVolume=3\nmusicVolume=-1\nsfxVolume=12\n");
    Expect("junk ignored: past 10, negative", Levels(10, 7, 10, 3));
    ReadSettings("musicVolume=\nvolume=2\n");
    Expect("an empty musicVolume= still makes it a file of this build", Levels(2, 7, 10, 10));

    ReadSettings("volume=4\ncryVolume=5\n");
    StoredText(stored, sizeof(stored));
    Expect("written back: the four lines, the older file's meaning kept",
           strstr(stored, "volume=4\nmusicVolume=7\nsfxVolume=10\ncryVolume=10\n") != NULL);
    ReadSettings("volume=9\nmusicVolume=2\nsfxVolume=8\ncryVolume=5\n");
    StoredText(stored, sizeof(stored));
    Platform_TestSettingsFile(SETTINGS_FILE);
    Expect("a file of this build written and read back: the same (cryVolume=5 stays 5)",
           strstr(stored, "volume=9\nmusicVolume=2\nsfxVolume=8\ncryVolume=5\n") != NULL && Levels(9, 2, 8, 5));
}

// ---- 4. The F10 menu's Audio page ---------------------------------------------------------------------------------------

#ifdef ENABLE_DIORAMA
static int MenuRows(struct PcMenuRow *rows, const char **title)
{
    *title = "";
    return Platform_TestPcMenuRows(rows, title);
}

static int SelectedRow(const struct PcMenuRow *rows, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (rows[i].selected)
            return i;
    }
    return -1;
}

static bool FileHas(const char *line)
{
    char text[2048];
    FILE *file = fopen(SETTINGS_FILE, "r");
    size_t length = 0;

    if (file != NULL)
    {
        length = fread(text, 1, sizeof(text) - 1, file);
        fclose(file);
    }
    text[length] = '\0';
    return strstr(text, line) != NULL;
}

static void TestMenu(void)
{
    struct PcMenuRow rows[PC_MENU_ITEM_COUNT];
    const char *title;
    int count, audio = -1, presses = 0;

    printf("4. the F10 menu's Audio page\n");
    ReadSettings(NULL);
    Expect("F10 opens the menu", Platform_TestPcMenuKey(SDLK_F10) && MenuRows(rows, &title) > 0);
    count = MenuRows(rows, &title);
    for (int i = 0; i < count; i++)
    {
        if (strcmp(rows[i].label, "Audio") == 0)
            audio = i;
    }
    Expectf(audio >= 0 && audio + 2 < count && strcmp(rows[audio + 1].label, "Controls") == 0
            && strcmp(rows[audio + 2].label, "Close") == 0 && !rows[audio].adjustable,
            "the main sheet (%s, %d rows) has an Audio row before Controls and Close", title, count);
    while (audio >= 0 && SelectedRow(rows, count) != audio && presses++ < 40)
    {
        Platform_TestPcMenuKey(SDLK_DOWN);
        count = MenuRows(rows, &title);
    }
    Platform_TestPcMenuKey(SDLK_RETURN);
    count = MenuRows(rows, &title);
    Expectf(strcmp(title, "Audio") == 0 && count == PC_MENU_AUDIO_ROW_COUNT
            && strcmp(rows[0].label, "Master volume") == 0 && strcmp(rows[0].value, "100%") == 0
            && strcmp(rows[1].label, "Music") == 0 && strcmp(rows[1].value, "70%") == 0
            && strcmp(rows[2].label, "Sound effects") == 0 && strcmp(rows[2].value, "100%") == 0
            && strcmp(rows[3].label, "Pokemon cries") == 0 && strcmp(rows[3].value, "100%") == 0
            && strcmp(rows[4].label, "Back") == 0 && rows[0].selected,
            "Enter opens the Audio page: %s %s, %s %s, %s %s, %s %s, %s", rows[0].label, rows[0].value, rows[1].label,
            rows[1].value, rows[2].label, rows[2].value, rows[3].label, rows[3].value, rows[4].label);

    Platform_TestPcMenuKey(SDLK_DOWN);
    Platform_TestPcMenuKey(SDLK_RIGHT);
    count = MenuRows(rows, &title);
    Expect("Right on Music: 80%, heard at once (the mixer's music gain 0.8) and saved (musicVolume=8)",
           strcmp(rows[1].value, "80%") == 0 && Levels(10, 8, 10, 10) && GainOf(AUDIO_MUSIC) == 0.8f
           && FileHas("musicVolume=8\n"));
    for (int i = 0; i < 4; i++)
        Platform_TestPcMenuKey(SDLK_RIGHT);
    count = MenuRows(rows, &title);
    Expect("Right up to 100%, and once more: it stays at 100% (no wrap to silence)",
           strcmp(rows[1].value, "100%") == 0 && Levels(10, 10, 10, 10) && FileHas("musicVolume=10\n"));
    Platform_TestPcMenuKey(SDLK_DOWN);
    Platform_TestPcMenuKey(SDLK_DOWN);
    for (int i = 0; i < 12; i++)
        Platform_TestPcMenuKey(SDLK_LEFT);
    count = MenuRows(rows, &title);
    Expect("Left on Pokemon cries down to 0%, and once more: it stays at 0%; the cries silent and saved",
           strcmp(rows[3].value, "0%") == 0 && Levels(10, 10, 10, 0) && GainOf(AUDIO_CRIES) == 0.0f
           && FileHas("cryVolume=0\n"));
    Platform_TestPcMenuKey(SDLK_UP);
    Platform_TestPcMenuKey(SDLK_LEFT);
    count = MenuRows(rows, &title);
    Expect("Left on Sound effects: 90%, the mixer's effects gain 0.9, sfxVolume=9",
           strcmp(rows[2].value, "90%") == 0 && Levels(10, 10, 9, 0) && GainOf(AUDIO_EFFECTS) == 0.9f
           && FileHas("sfxVolume=9\n"));
    Platform_TestPcMenuKey(SDLK_UP);
    Platform_TestPcMenuKey(SDLK_UP);
    Platform_TestPcMenuKey(SDLK_LEFT);
    Platform_TestPcMenuKey(SDLK_LEFT);
    count = MenuRows(rows, &title);
    Expect("Left twice on Master volume: 80%, the master stage's level, volume=8",
           strcmp(rows[0].value, "80%") == 0 && Levels(8, 10, 9, 0) && FileHas("volume=8\n"));
    Platform_TestPcMenuKey(SDLK_RETURN);
    count = MenuRows(rows, &title);
    Expect("Enter on a volume changes nothing", strcmp(title, "Audio") == 0 && Levels(8, 10, 9, 0));
    Platform_TestPcMenuKey(SDLK_ESCAPE);
    count = MenuRows(rows, &title);
    Expect("Esc: back to the main sheet, on the Audio row",
           strcmp(title, "Display & Graphics") == 0 && audio >= 0 && SelectedRow(rows, count) == audio);
    Platform_TestPcMenuKey(SDLK_F10);
    Expect("F10 closes the menu", MenuRows(rows, &title) == 0);
    Platform_TestSettingsFile(SETTINGS_FILE);
    Expect("the next start reads what the menu saved", Levels(8, 10, 9, 0));
}
#endif

int NativeSelfTest_AudioVolumes(void)
{
    printf("selftest-audio-volumes: master, music (70%% by default), sound effects and cries (include/platform/audio_volume.h)\n");
    TestCategories();
    TestMixer();
    TestSettingsFile();
#ifdef ENABLE_DIORAMA
    TestMenu();
#else
    printf("4. the F10 menu: only in the 3D build (pokeemerald-mp-diorama)\n");
#endif
    remove(SETTINGS_FILE);
    printf("selftest-audio-volumes: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures ? 1 : 0;
}
