/*
 * audio.c — PWM tone output on the PicoCalc stereo pins.
 */

#include "audio.h"

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"

#include "picocalc_hw.h"
#include "core1.h"

static bool _audio_ready = false;
static uint _slice_l = 0;
static uint _slice_r = 0;
static bool _audio_active;
static uint32_t _audio_stop_at;

#define AUDIO_QUEUE_SIZE 16u
#define AUDIO_QUEUE_MASK (AUDIO_QUEUE_SIZE - 1u)
typedef struct {
    int32_t frequency;
    int32_t duration_ms;
} audio_request_t;
static audio_request_t _audio_queue[AUDIO_QUEUE_SIZE];
static volatile uint32_t _audio_head;
static volatile uint32_t _audio_tail;

static void _audio_init_hw(void) {
    if (_audio_ready) return;

    gpio_set_function(AUDIO_PIN_L, GPIO_FUNC_PWM);
    gpio_set_function(AUDIO_PIN_R, GPIO_FUNC_PWM);
    _slice_l = pwm_gpio_to_slice_num(AUDIO_PIN_L);
    _slice_r = pwm_gpio_to_slice_num(AUDIO_PIN_R);
    pwm_set_enabled(_slice_l, false);
    pwm_set_enabled(_slice_r, false);
    _audio_ready = true;
}

static void _audio_stop_hw(void) {
    if (!_audio_ready) return;
    pwm_set_enabled(_slice_l, false);
    pwm_set_enabled(_slice_r, false);
    _audio_active = false;
}

static void _audio_start_hw(int freq_hz, int duration_ms) {
    _audio_init_hw();
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
    _audio_active = true;
    _audio_stop_at = to_ms_since_boot(get_absolute_time()) + (uint32_t)duration_ms;
}

void audio_service_poll(void) {
    uint32_t tail = __atomic_load_n(&_audio_tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&_audio_head, __ATOMIC_ACQUIRE);
    while (tail != head) {
        audio_request_t request = _audio_queue[tail & AUDIO_QUEUE_MASK];
        __atomic_store_n(&_audio_tail, ++tail, __ATOMIC_RELEASE);
        if (request.frequency == 0) _audio_stop_hw();
        else
            _audio_start_hw(request.frequency, request.duration_ms);
        head = __atomic_load_n(&_audio_head, __ATOMIC_ACQUIRE);
    }

    if (_audio_active && (int32_t)(to_ms_since_boot(get_absolute_time()) - _audio_stop_at) >= 0)
        _audio_stop_hw();
}

static void _audio_queue_request(int frequency, int duration_ms) {
    uint32_t head;
    for (;;) {
        head = __atomic_load_n(&_audio_head, __ATOMIC_RELAXED);
        uint32_t tail = __atomic_load_n(&_audio_tail, __ATOMIC_ACQUIRE);
        if (head - tail < AUDIO_QUEUE_SIZE) break;
        watchdog_update();
        tight_loop_contents();
    }
    _audio_queue[head & AUDIO_QUEUE_MASK] = (audio_request_t){
        .frequency = frequency,
        .duration_ms = duration_ms,
    };
    __atomic_store_n(&_audio_head, head + 1u, __ATOMIC_RELEASE);
}

static void _audio_init_task(void *context) {
    (void)context;
    _audio_init_hw();
}

void audio_init(void) {
    if (core1_is_running() && !core1_is_worker()) {
        core1_call(_audio_init_task, NULL);
        return;
    }
    _audio_init_hw();
}

void audio_stop(void) {
    if (core1_is_running() && !core1_is_worker()) {
        _audio_queue_request(0, 0);
        return;
    }
    _audio_stop_hw();
}

void audio_beep(int freq_hz, int duration_ms) {
    if (freq_hz < 20 || freq_hz > 20000 || duration_ms <= 0) return;
    if (core1_is_running() && !core1_is_worker()) {
        _audio_queue_request(freq_hz, duration_ms);
        return;
    }
    if (core1_is_worker()) {
        _audio_start_hw(freq_hz, duration_ms);
        return;
    }

    _audio_start_hw(freq_hz, duration_ms);
    sleep_ms((uint32_t)duration_ms);
    _audio_stop_hw();
}
