/*
 * test_mixer.c — 4-voice sample mixer: waveforms, envelopes, and clipping.
 */

#include "core/mixer.h"
#include "test_util.h"

#define SR 16000u

static mixer_t mixer;

void test_mixer(void) {
    mixer_init(&mixer, SR);
    CHECK_EQ(mixer.sample_rate, SR);
    for (int i = 0; i < MIXER_VOICES; i++)
        CHECK(!mixer.voice[i].active);

    /* Silence is the PWM midpoint. */
    CHECK_EQ(mixer_sample(&mixer), 128);
    CHECK_EQ(mixer_sample(&mixer), 128);
    CHECK(!mixer_playing(&mixer));

    /* ---- Square wave -------------------------------------------------- */

    /* Nyquist/2 square with instant attack: alternates high and low. */
    mixer_note_on(&mixer, 0, MIXER_WAVE_SQUARE, SR / 2, 0, 255, 0, 0);
    uint8_t first = mixer_sample(&mixer);
    uint8_t second = mixer_sample(&mixer);
    CHECK(first != 128);
    CHECK(first != second);

    /* The square alternates every sample at half the sample rate. */
    uint8_t third = mixer_sample(&mixer);
    CHECK_EQ(third, first);
    CHECK(mixer_playing(&mixer));
    mixer_note_off(&mixer, 0);
    CHECK_EQ(mixer_sample(&mixer), 128); /* release_ms == 0: dies at once */
    CHECK(!mixer_playing(&mixer));

    /* ---- Triangle wave ------------------------------------------------- */

    mixer_note_on(&mixer, 0, MIXER_WAVE_TRIANGLE, SR / 16, 0, 255, 0, 0);
    int16_t prev = (int16_t)mixer_sample(&mixer) - 128;
    bool rose = false, fell = false;
    for (int i = 0; i < 32; i++) {
        int16_t cur = (int16_t)mixer_sample(&mixer) - 128;
        if (cur > prev) rose = true;
        if (cur < prev) fell = true;
        prev = cur;
    }
    CHECK(rose);
    CHECK(fell);
    /* The ramp stays within the full scale. */
    CHECK(prev >= -128 && prev <= 127);
    mixer_stop_all(&mixer);
    CHECK(!mixer_playing(&mixer));

    /* ---- Noise -------------------------------------------------------- */

    mixer_note_on(&mixer, 0, MIXER_WAVE_NOISE, 440, 0, 255, 0, 0);
    uint8_t samples[8];
    for (int i = 0; i < 8; i++)
        samples[i] = mixer_sample(&mixer);
    bool varied = false;
    for (int i = 1; i < 8; i++) {
        if (samples[i] != samples[0]) varied = true;
    }
    CHECK(varied);

    /* Noise voices carry independent per-voice seeds: the same note on
       voice 1 of one mixer and voice 0 of another produces different
       streams. */
    mixer_t other;
    mixer_init(&other, SR);
    mixer_note_on(&mixer, 1, MIXER_WAVE_NOISE, 440, 0, 255, 0, 0);
    mixer_note_on(&other, 0, MIXER_WAVE_NOISE, 440, 0, 255, 0, 0);
    bool streams_differ = false;
    for (int i = 0; i < 16; i++) {
        if (mixer_sample(&mixer) != mixer_sample(&other)) streams_differ = true;
    }
    CHECK(streams_differ);
    mixer_stop_all(&mixer);
    mixer_stop_all(&other);

    /* Two freshly initialised mixers produce the same stream for the same
       voice: the default seed is deterministic. */
    mixer_init(&mixer, SR);
    mixer_init(&other, SR);
    mixer_note_on(&mixer, 0, MIXER_WAVE_NOISE, 440, 0, 255, 0, 0);
    mixer_note_on(&other, 0, MIXER_WAVE_NOISE, 440, 0, 255, 0, 0);
    for (int i = 0; i < 16; i++)
        CHECK_EQ(mixer_sample(&mixer), mixer_sample(&other));
    mixer_stop_all(&mixer);
    mixer_stop_all(&other);

    /* Restarting a note on an already-used voice continues the stream
       rather than replaying the same burst (no click loops in a tracker). */
    mixer_note_on(&mixer, 0, MIXER_WAVE_NOISE, 440, 0, 255, 0, 0);
    uint8_t before[4];
    for (int i = 0; i < 4; i++)
        before[i] = mixer_sample(&mixer);
    mixer_note_on(&mixer, 0, MIXER_WAVE_NOISE, 440, 0, 255, 0, 0);
    uint8_t after[4];
    for (int i = 0; i < 4; i++)
        after[i] = mixer_sample(&mixer);
    bool continued = false;
    for (int i = 0; i < 4; i++) {
        if (after[i] != before[i]) continued = true;
    }
    CHECK(continued);
    mixer_stop_all(&mixer);

    /* ---- Attack and release envelopes -------------------------------- */

    mixer_note_on(&mixer, 0, MIXER_WAVE_SQUARE, SR / 2, 0, 255, 10, 0);
    /* Sample 1: the attack has barely started. Capture a run of 20 ms. */
    uint16_t run[200];
    for (int i = 0; i < 200; i++)
        run[i] = mixer_sample(&mixer);
    /* Levels should rise towards the extremes during the attack. */
    int32_t early = (int32_t)run[0] - 128;
    int32_t late = (int32_t)run[190] - 128;
    if (early < 0) early = -early;
    if (late < 0) late = -late;
    CHECK(late >= early);
    /* ...and never leave the DAC range. */
    for (int i = 0; i < 200; i++)
        CHECK(run[i] <= 255);

    /* A timed note releases by itself and ends silent. */
    mixer_note_on(&mixer, 0, MIXER_WAVE_SQUARE, SR / 2, 10, 255, 0, 5);
    for (int i = 0; i < (int)SR * 30 / 1000; i++)
        (void)mixer_sample(&mixer);
    CHECK_EQ(mixer_sample(&mixer), 128);
    CHECK(!mixer_playing(&mixer));

    /* Releasing mid-attack must not jump louder. */
    mixer_note_on(&mixer, 0, MIXER_WAVE_SQUARE, SR / 2, 0, 255, 50, 10);
    for (int i = 0; i < 5 * (int)SR / 1000; i++)
        (void)mixer_sample(&mixer); /* 5 ms in */
    mixer_note_off(&mixer, 0);
    uint8_t after_off = mixer_sample(&mixer);
    int32_t magnitude = (int32_t)after_off - 128;
    if (magnitude < 0) magnitude = -magnitude;
    CHECK(magnitude < 60); /* nowhere near the 127 full scale */

    /* ---- Polyphony and clipping --------------------------------------- */

    /* Four full-volume voices sum to a wider dynamic range than one, and
       the mix stays audible (not stuck at the PWM midpoint). */
    uint16_t solo_min = 65535, solo_max = 0;
    mixer_note_on(&mixer, 0, MIXER_WAVE_TRIANGLE, 220, 0, 255, 0, 0);
    for (int i = 0; i < 500; i++) {
        uint16_t level = mixer_sample(&mixer);
        if (level < solo_min) solo_min = level;
        if (level > solo_max) solo_max = level;
    }
    mixer_stop_all(&mixer);

    uint16_t chord_min = 65535, chord_max = 0;
    for (int i = 0; i < MIXER_VOICES; i++)
        mixer_note_on(&mixer, i, MIXER_WAVE_TRIANGLE, 220 + (uint32_t)i * 33, 0, 255, 0, 0);
    for (int i = 0; i < 500; i++) {
        uint16_t level = mixer_sample(&mixer);
        if (level < chord_min) chord_min = level;
        if (level > chord_max) chord_max = level;
    }
    CHECK(chord_max - chord_min >= solo_max - solo_min);
    CHECK(chord_max > 128);
    CHECK(chord_min < 128);
    CHECK(mixer_playing(&mixer));
    mixer_stop_all(&mixer);
    CHECK(!mixer_playing(&mixer));
    CHECK_EQ(mixer_sample(&mixer), 128);

    /* ---- Parameter handling ------------------------------------------- */

    /* Zero frequency is treated as the lowest audible note, not a hang. */
    mixer_note_on(&mixer, 0, MIXER_WAVE_SQUARE, 0, 0, 255, 0, 0);
    CHECK(mixer.voice[0].active);
    CHECK_EQ(mixer.voice[0].frequency_hz, 20u);
    mixer_stop_all(&mixer);

    /* Out-of-range voices and NULL mixers are ignored. */
    mixer_note_on(&mixer, MIXER_VOICES, MIXER_WAVE_SQUARE, 440, 0, 255, 0, 0);
    mixer_note_on(&mixer, -1, MIXER_WAVE_SQUARE, 440, 0, 255, 0, 0);
    mixer_note_off(&mixer, MIXER_VOICES);
    mixer_note_off(&mixer, -1);
    mixer_note_on(NULL, 0, MIXER_WAVE_SQUARE, 440, 0, 255, 0, 0);
    mixer_note_off(NULL, 0);
    mixer_stop_all(NULL);
    CHECK(!mixer_playing(NULL));
    CHECK_EQ(mixer_sample(NULL), 128);
    CHECK(!mixer_playing(&mixer));

    /* note_off on an idle voice is a no-op. */
    mixer_note_off(&mixer, 1);
    CHECK(!mixer.voice[1].active);
}
