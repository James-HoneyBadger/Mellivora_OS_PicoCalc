#pragma once
/*
 * audio.h — PWM tone output on the PicoCalc stereo pins.
 */

/*
 * Configure the audio pins for PWM output. Safe to call repeatedly.
 */
void audio_init(void);

/*
 * Play a square-wave tone for `duration_ms` on both channels. After the Core 1
 * audio worker starts, the request is queued and this function returns;
 * before then it blocks for the requested duration. Frequencies outside
 * 20 Hz - 20 kHz are ignored.
 */
void audio_beep(int freq_hz, int duration_ms);

/*
 * Disable both audio channels immediately.
 */
void audio_stop(void);

/* Core 1 audio worker hook. Called periodically by the service loop. */
void audio_service_poll(void);
