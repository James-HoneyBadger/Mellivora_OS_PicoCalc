/*
 * audio.c — 4-voice PWM audio for the PicoCalc stereo pins.
 *
 * The PWM slices run as an 8-bit DAC (wrap = 255, carrier = sysclk/256,
 * ~520 kHz). A repeating timer calls core/mixer.c every 62.5 us and writes
 * the mixed level to both channels, so up to four square/triangle/noise
 * voices with envelopes can play at once.
 *
 * The mixer state is owned by the timer IRQ on core 0. Requests from any
 * core go through a small SPSC queue the IRQ drains, which keeps the legacy
 * blocking callers (audio_beep before Core 1 starts) non-blocking instead.
 */

#include "audio.h"

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"

#include "core/mixer.h"
#include "picocalc_hw.h"

#define AUDIO_SAMPLE_HZ 16000u
#define AUDIO_QUEUE_SIZE 16u
#define AUDIO_QUEUE_MASK (AUDIO_QUEUE_SIZE - 1u)

typedef struct {
    int32_t wave;
    int32_t frequency;
    int32_t duration_ms;
    int32_t volume;
    int32_t attack_ms;
    int32_t release_ms;
    int32_t voice;
} audio_request_t;

static mixer_t _mixer;
static struct repeating_timer _mixer_timer;
static bool _mixer_running;
static uint _slice_l, _chan_l, _slice_r, _chan_r;

static audio_request_t _queue[AUDIO_QUEUE_SIZE];
static volatile uint32_t _head;
static volatile uint32_t _tail;
/* Producers (core 0 shell and any core 1 worker) serialise on this flag.
   The IRQ consumer never takes it, so a producer cannot deadlock with the
   timer it may be preempted by. */
static volatile uint32_t _producer_lock;

static void _producer_lock_acquire(void) {
    while (__atomic_exchange_n(&_producer_lock, 1u, __ATOMIC_ACQUIRE)) {
        watchdog_update();
        tight_loop_contents();
    }
}

static void _producer_lock_release(void) {
    __atomic_store_n(&_producer_lock, 0u, __ATOMIC_RELEASE);
}

static void _queue_request(int wave, int frequency, int duration_ms, int volume, int attack_ms,
                           int release_ms, int voice) {
    _producer_lock_acquire();
    uint32_t head = __atomic_load_n(&_head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&_tail, __ATOMIC_ACQUIRE);
    if (head - tail < AUDIO_QUEUE_SIZE) {
        _queue[head & AUDIO_QUEUE_MASK] = (audio_request_t){
            .wave = wave,
            .frequency = frequency,
            .duration_ms = duration_ms,
            .volume = volume,
            .attack_ms = attack_ms,
            .release_ms = release_ms,
            .voice = voice,
        };
        __atomic_store_n(&_head, head + 1u, __ATOMIC_RELEASE);
    }
    _producer_lock_release();
}

/* Drain at most two queued requests per tick so a burst of beeps cannot make
   the timer callback miss its deadline. */
static void _drain_requests(void) {
    for (int processed = 0; processed < 2; processed++) {
        uint32_t tail = __atomic_load_n(&_tail, __ATOMIC_RELAXED);
        uint32_t head = __atomic_load_n(&_head, __ATOMIC_ACQUIRE);
        if (tail == head) break;
        audio_request_t request = _queue[tail & AUDIO_QUEUE_MASK];
        __atomic_store_n(&_tail, tail + 1u, __ATOMIC_RELEASE);
        if (request.wave < 0) {
            /* Sentinel waves carry stops, keeping every mixer write on the
               IRQ owner instead of racing the producer core. */
            if (request.wave == -1) mixer_stop_all(&_mixer);
            else
                mixer_note_off(&_mixer, request.voice);
            continue;
        }
        mixer_note_on(&_mixer, request.voice, (mixer_wave_t)request.wave,
                      (uint32_t)request.frequency, (uint32_t)request.duration_ms,
                      (uint8_t)request.volume, (uint16_t)request.attack_ms,
                      (uint16_t)request.release_ms);
    }
}

static bool _timer_tick(struct repeating_timer *timer) {
    (void)timer;
    _drain_requests();
    uint8_t level = mixer_sample(&_mixer);
    pwm_set_chan_level(_slice_l, _chan_l, level);
    pwm_set_chan_level(_slice_r, _chan_r, level);
    return true;
}

static void _audio_init_hw(void) {
    if (_mixer_running) return;

    gpio_set_function(AUDIO_PIN_L, GPIO_FUNC_PWM);
    gpio_set_function(AUDIO_PIN_R, GPIO_FUNC_PWM);
    _slice_l = pwm_gpio_to_slice_num(AUDIO_PIN_L);
    _slice_r = pwm_gpio_to_slice_num(AUDIO_PIN_R);
    _chan_l = pwm_gpio_to_channel(AUDIO_PIN_L);
    _chan_r = pwm_gpio_to_channel(AUDIO_PIN_R);

    /* Carrier = sysclk / 256 (~520 kHz); level 0..255 is the DAC value. */
    pwm_set_enabled(_slice_l, false);
    pwm_set_enabled(_slice_r, false);
    pwm_set_wrap(_slice_l, 255);
    pwm_set_clkdiv_int_frac(_slice_l, 1, 0);
    if (_slice_r != _slice_l) {
        pwm_set_wrap(_slice_r, 255);
        pwm_set_clkdiv_int_frac(_slice_r, 1, 0);
    }
    pwm_set_chan_level(_slice_l, _chan_l, 128);
    pwm_set_chan_level(_slice_r, _chan_r, 128);
    pwm_set_enabled(_slice_l, true);
    pwm_set_enabled(_slice_r, true);

    mixer_init(&_mixer, AUDIO_SAMPLE_HZ);
    /* Default pool: the callback runs in IRQ context on core 0, which owns
       the mixer regardless of where audio_init() is called from. The pool is
       up long before main() thanks to the SDK runtime init. */
    alarm_pool_init_default();
    if (alarm_pool_add_repeating_timer_us(alarm_pool_get_default(), 1000000u / AUDIO_SAMPLE_HZ,
                                          _timer_tick, NULL, &_mixer_timer)) {
        _mixer_running = true;
    }
}

void audio_init(void) {
    /* No core hop needed: the timer IRQ always lands on core 0. */
    _audio_init_hw();
}

void audio_note(audio_wave_t wave, int freq_hz, int duration_ms, int volume, int attack_ms,
                int release_ms, int voice) {
    if (freq_hz < 20 || freq_hz > 20000 || duration_ms < 0) return;
    if (voice < 0 || voice >= MIXER_VOICES) return;
    if (!_mixer_running) _audio_init_hw();
    _queue_request((int)wave, freq_hz, duration_ms, volume, attack_ms, release_ms, voice);
}

void audio_voice_stop(int voice) {
    if (voice < 0 || voice >= MIXER_VOICES) return;
    if (!_mixer_running) return;
    _queue_request(-2, 0, 0, 0, 0, 0, voice);
}

void audio_beep(int freq_hz, int duration_ms) {
    if (freq_hz < 20 || freq_hz > 20000 || duration_ms <= 0) return;
    audio_note(AUDIO_WAVE_SQUARE, freq_hz, duration_ms, 255, 1, 5, 0);
}

void audio_stop(void) {
    if (!_mixer_running) return;
    _queue_request(-1, 0, 0, 0, 0, 0, 0);
}

/* Legacy hook retained for callers that still poll; the timer IRQ drains the
   queue, so this just keeps the name alive. */
void audio_service_poll(void) {}
