#pragma once
/*
 * audio.h — PWM tone output on the PicoCalc stereo pins.
 */

/*
 * Configure the audio pins for PWM output. Safe to call repeatedly.
 */
void audio_init(void);

/*
 * Play a square-wave tone for `duration_ms`, blocking. Both channels are
 * driven. Frequencies outside 20 Hz - 20 kHz are ignored.
 */
void audio_beep(int freq_hz, int duration_ms);

/*
 * Disable both audio channels immediately.
 */
void audio_stop(void);
