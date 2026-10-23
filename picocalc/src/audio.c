/*
 * audio.c — PWM tone output on the PicoCalc stereo pins.
 */

#include "audio.h"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"

#include "picocalc_hw.h"

static bool _audio_ready = false;
static uint _slice_l = 0;
static uint _slice_r = 0;

void audio_init(void) {
    if (_audio_ready) return;

    gpio_set_function(AUDIO_PIN_L, GPIO_FUNC_PWM);
    gpio_set_function(AUDIO_PIN_R, GPIO_FUNC_PWM);
    _slice_l = pwm_gpio_to_slice_num(AUDIO_PIN_L);
    _slice_r = pwm_gpio_to_slice_num(AUDIO_PIN_R);
    pwm_set_enabled(_slice_l, false);
    pwm_set_enabled(_slice_r, false);
    _audio_ready = true;
}

void audio_stop(void) {
    if (!_audio_ready) return;
    pwm_set_enabled(_slice_l, false);
    pwm_set_enabled(_slice_r, false);
}

void audio_beep(int freq_hz, int duration_ms) {
    if (freq_hz < 20 || freq_hz > 20000 || duration_ms <= 0) return;

    audio_init();

    uint32_t sys_clk = clock_get_hz(clk_sys);
    uint32_t wrap = sys_clk / (uint32_t)freq_hz;
    uint16_t div16 = 1;
    while (wrap > 65535) {
        div16++;
        wrap = sys_clk / ((uint32_t)freq_hz * div16);
    }

    pwm_set_clkdiv_int_frac(_slice_l, (uint8_t)div16, 0);
    pwm_set_wrap(_slice_l, (uint16_t)wrap);
    pwm_set_chan_level(_slice_l, PWM_CHAN_A, (uint16_t)(wrap / 4));
    pwm_set_enabled(_slice_l, true);

    pwm_set_clkdiv_int_frac(_slice_r, (uint8_t)div16, 0);
    pwm_set_wrap(_slice_r, (uint16_t)wrap);
    pwm_set_chan_level(_slice_r, PWM_CHAN_A, (uint16_t)(wrap / 4));
    pwm_set_enabled(_slice_r, true);

    sleep_ms((uint32_t)duration_ms);
    audio_stop();
}
