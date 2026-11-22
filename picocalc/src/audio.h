#pragma once
/*
 * audio.h — 4-voice PWM audio on the PicoCalc stereo pins.
 *
 * audio_init() switches the pins to PWM-as-DAC and starts a 16 kHz mixer
 * timer on core 0. After that all calls are non-blocking: a voice starts on
 * the next timer tick and fades per its envelope.
 */

#include <stdint.h>

typedef enum {
    AUDIO_WAVE_SQUARE = 0,
    AUDIO_WAVE_TRIANGLE,
    AUDIO_WAVE_NOISE,
} audio_wave_t;

/* Configure the audio pins and start the mixer timer. Safe to call repeatedly. */
void audio_init(void);

/* Start (or restart) voice 0-3. Volume 0..255; attack/release in ms.
   duration_ms of 0 runs until audio_voice_stop(). Frequencies outside
   20 Hz - 20 kHz are ignored. */
void audio_note(audio_wave_t wave, int freq_hz, int duration_ms, int volume, int attack_ms,
                int release_ms, int voice);

/* Begin the release phase of one voice. */
void audio_voice_stop(int voice);

/* Convenience: square beep on voice 0, matching the legacy behaviour. */
void audio_beep(int freq_hz, int duration_ms);

/* Silence all voices immediately. */
void audio_stop(void);

/* No-op retained for compatibility; the mixer timer drains its own queue. */
void audio_service_poll(void);
