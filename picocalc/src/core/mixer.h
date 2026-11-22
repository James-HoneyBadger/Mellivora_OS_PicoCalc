#pragma once
/*
 * core/mixer.h — 4-voice sample mixer for the Mellivora audio layer.
 *
 * Pure logic: no PWM, no interrupts, no SDK. The firmware calls
 * mixer_sample() from a periodic timer and writes the result to the PWM
 * channel level (PWM-as-DAC). Keeping it here means waveforms, envelopes,
 * and clipping are host-tested like the rest of core/.
 */

#include <stdbool.h>
#include <stdint.h>

#define MIXER_VOICES 4

typedef enum {
    MIXER_WAVE_SQUARE = 0,
    MIXER_WAVE_TRIANGLE,
    MIXER_WAVE_NOISE,
} mixer_wave_t;

typedef struct {
    bool active;
    bool releasing;
    mixer_wave_t wave;
    uint32_t frequency_hz;
    uint32_t duration_ms; /* 0 = runs until mixer_note_off */
    uint8_t volume;       /* 0..255 */
    uint16_t attack_ms;
    uint16_t release_ms;

    uint32_t phase;        /* 16.16 fixed point */
    uint32_t sample_index; /* samples since note start */
    uint32_t release_at;   /* sample index where release began */
    uint32_t noise_state;
} mixer_voice_t;

typedef struct {
    mixer_voice_t voice[MIXER_VOICES];
    uint32_t sample_rate;
} mixer_t;

void mixer_init(mixer_t *mixer, uint32_t sample_rate);

/* Start (or restart) a voice. Volume scales the waveform; the envelope ramps
   linearly over attack_ms, then releases over release_ms once the duration
   elapses or mixer_note_off() is called. */
void mixer_note_on(mixer_t *mixer, int voice, mixer_wave_t wave, uint32_t frequency_hz,
                   uint32_t duration_ms, uint8_t volume, uint16_t attack_ms, uint16_t release_ms);

/* Begin the release phase of one voice. */
void mixer_note_off(mixer_t *mixer, int voice);

/* Silence everything immediately (no release ramp). */
void mixer_stop_all(mixer_t *mixer);

/* Advance all voices one sample and return the mixed level, 0..255. */
uint8_t mixer_sample(mixer_t *mixer);

/* True while any voice is active or still releasing. */
bool mixer_playing(const mixer_t *mixer);
