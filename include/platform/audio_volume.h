#ifndef GUARD_PLATFORM_AUDIO_VOLUME_H
#define GUARD_PLATFORM_AUDIO_VOLUME_H

/*
 * The PC game's volume controls: the owner's decision of 2026-11-05 (app repository docs/owner-decisions.md #45, which
 * supersedes #23 "Loudness"): "Set default music volume to 70%. Keep sound effects at 100%. Provide independent master,
 * music, and sound-effect controls." The Pokemon cries keep a control of their own (cryVolume=, read and saved by every
 * earlier build but never applied).
 *
 * The settings file (pokeemerald.cfg; the launcher's game-settings.cfg), each 0 to 10 = 0-100% in 10% steps:
 *   volume=       master: everything, after the mix (sdl2.c Platform_QueueAudio: x volume/10, then the x4 make-up gain of
 *                 decision #23, clamped to [-1, 1]; unchanged)                                        default 10
 *   musicVolume=  every music song, the fanfares and jingles included                                 default 7
 *   sfxVolume=    every sound effect                                                                   default 10
 *   cryVolume=    the Pokemon cries                                                                    default 10
 * A file without musicVolume= and sfxVolume= (written before they existed) gets those two defaults, its volume= stays the
 * master, and its cryVolume=5 - every earlier build's default, never applied - reads as 10, so the cries stay as loud as
 * they were; any other cryVolume= in it was the player's own choice and is kept (and now applied). The launcher reads the
 * file the same way (app src/emerald_multiplayer/game_display_settings.py read_audio_settings). A build older than this
 * one rewrites the file without the two new lines (its next reader takes the defaults again): the app and the engine
 * ship together.
 *
 * Music, sound effects and cries are each mixed at their own gain, per channel, in the mixers: sound_mixer.c for the 12
 * sample channels, cgb_audio.c for the 4 GB channels. A channel takes the category of the player whose note it plays,
 * set when the note starts (music_player.c MP2K_event_nxx); a music player's category is its song's, set when the song
 * starts (m4a_pc.c m4aSongNumStart and the like): a song number below START_MUS is a sound effect, from START_MUS on
 * music - by number, not by player, because the fanfares and jingles (mus_level_up, mus_heal, mus_obtain_item,
 * mus_evolved...) play on sound effect player 2; the bard's phonemes (PH_*, his song) count as music too. The two cry
 * players (SetPokemonCryTone) play cries. Every gain is at most 1, so no sound is louder than before; a channel at 100%
 * is mixed by the code that mixed it before, so with every control at 100% the sound is bit for bit what it was.
 */
#include <stdbool.h>
#include <stdio.h>

enum AudioVolumeControl
{
    AUDIO_MASTER,
    AUDIO_MUSIC,
    AUDIO_EFFECTS,
    AUDIO_CRIES,
    AUDIO_CONTROL_COUNT,
};

#define AUDIO_VOLUME_MAX 10              /* 100%; each step is 10% */
#define AUDIO_VOLUME_OLD_CRY_DEFAULT 5   /* cryVolume= of every build before musicVolume=: never applied */

/* The mixers' channels: the 12 sample channels (0-11), then the 4 GB channels (square 1, square 2, wave, noise). */
#define AUDIO_SAMPLE_CHANNELS 12
#define AUDIO_GB_CHANNEL(n) (AUDIO_SAMPLE_CHANNELS + (n))
#define AUDIO_CHANNELS (AUDIO_SAMPLE_CHANNELS + 4)

struct AudioVolumes
{
    unsigned char level[AUDIO_CONTROL_COUNT]; /* 0-10 each, by enum AudioVolumeControl */
    bool newKeys;                             /* the file had musicVolume= or sfxVolume= (written by this build or later) */
};

/* The settings file's key of each control: "volume", "musicVolume", "sfxVolume", "cryVolume". */
extern const char *const gAudioVolumeKeys[AUDIO_CONTROL_COUNT];

void AudioVolumes_Defaults(struct AudioVolumes *volumes);
/* One line of the settings file: true when it is one of the four keys (a value past 10 or not a number is ignored). */
bool AudioVolumes_ParseLine(struct AudioVolumes *volumes, const char *line);
/* After the file's last line: an older file's meaning (see above). */
void AudioVolumes_Finish(struct AudioVolumes *volumes);
/* The four lines, always all four. */
void AudioVolumes_Write(const struct AudioVolumes *volumes, FILE *file);

/* ---- The mixers' side -------------------------------------------------------------------------------------------- */

/* The music, sound effect and cry levels the mixers use from now on (the master is applied after the mix). */
void AudioVolume_SetLevels(const struct AudioVolumes *volumes);
/* Below START_MUS a sound effect, from it on music (constants/songs.h). */
enum AudioVolumeControl AudioVolume_SongCategory(unsigned int song);
/* A song started on a music player (m4a_pc.c): its notes from now on are of this category. */
void AudioVolume_PlayerStarted(const void *player, enum AudioVolumeControl category);
enum AudioVolumeControl AudioVolume_PlayerCategory(const void *player);
/* A note started on a mixer channel (0 to AUDIO_CHANNELS - 1) for a music player (music_player.c). */
void AudioVolume_NoteStarted(int channel, const void *player);
/* The category of the sound a channel plays, and its gain: 1.0f exactly at 100%, 0.0f at 0%. */
enum AudioVolumeControl AudioVolume_ChannelCategory(int channel);
float AudioVolume_ChannelGain(int channel);
/* The master stage of Platform_QueueAudio: each sample x level/10 x 4 (the make-up gain), clamped to [-1, 1]. */
void AudioVolume_ApplyMaster(float *samples, int count, unsigned int level);

/* For --selftest-audio-volumes: how many times a mixer mixed a channel at a gain below 100% (none while every
 * control is at 100%: then each channel is mixed by the code that mixed it before). */
extern unsigned int gAudioVolumeScaledMixes;

#endif // GUARD_PLATFORM_AUDIO_VOLUME_H
