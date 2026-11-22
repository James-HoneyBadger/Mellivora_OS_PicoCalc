/*
 * core/mixer.c — 4-voice sample mixer.
 */

#include "core/mixer.h"

#include <string.h>

#define PHASE_FRACTION 65536u

void mixer_init(mixer_t *mixer, uint32_t sample_rate) {
    memset(mixer, 0, sizeof *mixer);
    mixer->sample_rate = sample_rate == 0 ? 16000u : sample_rate;
    mixer->voice[0].noise_state = 0x4A5Du;
    mixer->voice[1].noise_state = 0x91E3u;
    mixer->voice[2].noise_state = 0x2C7Bu;
    mixer->voice[3].noise_state = 0xB81Fu;
}

static uint32_t _duration_samples(const mixer_t *mixer, uint32_t duration_ms) {
    uint64_t samples = ((uint64_t)duration_ms * mixer->sample_rate) / 1000u;
    return samples > UINT32_MAX ? UINT32_MAX : (uint32_t)samples;
}

void mixer_note_on(mixer_t *mixer, int voice, mixer_wave_t wave, uint32_t frequency_hz,
                   uint32_t duration_ms, uint8_t volume, uint16_t attack_ms, uint16_t release_ms) {
    if (!mixer || voice < 0 || voice >= MIXER_VOICES) return;
    /* Keep per-voice noise seeds across notes so four noise voices do not
       correlate with each other or with their previous note. */
    uint32_t seed = mixer->voice[voice].noise_state;
    if (seed == 0) seed = 0x4A5Du ^ ((uint32_t)(voice + 1) * 0x9E37u);

    mixer_voice_t *v = &mixer->voice[voice];
    memset(v, 0, sizeof *v);

    v->active = true;
    v->wave = wave;
    /* A zero frequency would divide into a zero phase step and make the
       waveform a DC level; treat it as the lowest audible note. */
    v->frequency_hz = frequency_hz == 0 ? 20u : frequency_hz;
    v->duration_ms = duration_ms;
    v->volume = volume;
    v->attack_ms = attack_ms;
    v->release_ms = release_ms;
    v->noise_state = seed;
}

void mixer_note_off(mixer_t *mixer, int voice) {
    if (!mixer || voice < 0 || voice >= MIXER_VOICES) return;
    mixer_voice_t *v = &mixer->voice[voice];
    if (!v->active || v->releasing) return;
    v->releasing = true;
    v->release_at = v->sample_index;
}

void mixer_stop_all(mixer_t *mixer) {
    if (!mixer) return;
    for (int i = 0; i < MIXER_VOICES; i++) {
        mixer_voice_t *v = &mixer->voice[i];
        uint32_t seed = v->noise_state;
        memset(v, 0, sizeof *v);
        v->noise_state = seed;
    }
}

/* Envelope: 0..volume. Linear attack, then linear release from wherever the
   attack had reached, so a note released mid-attack never jumps louder. */
static int32_t _envelope(const mixer_t *mixer, const mixer_voice_t *v) {
    uint32_t attack_samples = _duration_samples(mixer, v->attack_ms);
    int32_t attack_env;
    if (attack_samples == 0 || v->sample_index >= attack_samples) {
        attack_env = v->volume;
    } else {
        attack_env = (int32_t)v->volume * (int32_t)v->sample_index / (int32_t)attack_samples;
    }

    if (!v->releasing) return attack_env;

    uint32_t release_samples = _duration_samples(mixer, v->release_ms);
    uint32_t into = v->sample_index - v->release_at;
    if (release_samples == 0 || into >= release_samples) return 0;
    return attack_env * (int32_t)(release_samples - into) / (int32_t)release_samples;
}

/* Waveform: -128..127 at full volume. */
static int32_t _waveform(mixer_voice_t *v) {
    switch (v->wave) {
    case MIXER_WAVE_TRIANGLE: {
        uint16_t frac = (uint16_t)(v->phase >> 16);
        if ((v->phase & 0x10000u) != 0) frac = (uint16_t)~frac;
        return (int32_t)(frac >> 8) - 128;
    }
    case MIXER_WAVE_NOISE: {
        uint32_t lfsr = v->noise_state;
        uint32_t bit = (lfsr ^ (lfsr >> 1)) & 1u;
        lfsr = (lfsr >> 1) | (bit << 14);
        v->noise_state = lfsr;
        return (int32_t)((lfsr >> 7) & 0xFFu) - 128;
    }
    case MIXER_WAVE_SQUARE:
    default:
        return (v->phase & 0x8000u) != 0 ? 127 : -128;
    }
}

uint8_t mixer_sample(mixer_t *mixer) {
    if (!mixer) return 128;
    int32_t sum = 0;
    int live = 0;

    for (int i = 0; i < MIXER_VOICES; i++) {
        mixer_voice_t *v = &mixer->voice[i];
        if (!v->active) continue;

        int32_t env = _envelope(mixer, v);
        int32_t wave = _waveform(v);
        sum += wave * env;
        live++;

        v->phase += (uint32_t)(((uint64_t)v->frequency_hz * PHASE_FRACTION) / mixer->sample_rate);
        v->sample_index++;

        if (!v->releasing && v->duration_ms != 0 &&
            v->sample_index >= _duration_samples(mixer, v->duration_ms)) {
            v->releasing = true;
            v->release_at = v->sample_index;
        }
        if (v->releasing && _envelope(mixer, v) == 0) v->active = false;
    }

    if (live == 0) return 128;

    /* Each voice contributes at most ±(128*255); four of them can sum to
       ±130k. Scale back to a full-scale wave and clamp into DAC range. */
    int32_t scaled = sum / 512; /* ±255 */
    int32_t level = scaled + 128;
    if (level < 0) return 0;
    if (level > 255) return 255;
    return (uint8_t)level;
}

bool mixer_playing(const mixer_t *mixer) {
    if (!mixer) return false;
    for (int i = 0; i < MIXER_VOICES; i++) {
        if (mixer->voice[i].active) return true;
    }
    return false;
}
