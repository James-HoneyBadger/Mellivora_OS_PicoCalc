#include "core1.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#define CORE1_QUEUE_SIZE 8u
#define CORE1_QUEUE_MASK (CORE1_QUEUE_SIZE - 1u)

typedef struct {
    core1_task_fn task;
    void *context;
    volatile uint32_t done;
} core1_request_t;

static core1_request_t *_queue[CORE1_QUEUE_SIZE];
static volatile uint32_t _head;
static volatile uint32_t _tail;
static volatile uint32_t _running;
static volatile uint32_t _core0_busy_us;
static volatile uint32_t _core1_busy_us;
static volatile uint32_t _core0_idle_us;
static volatile uint32_t _core1_idle_us;
static uint32_t _last_core0_busy;
static uint32_t _last_core1_busy;
static uint32_t _last_core0_idle;
static uint32_t _last_core1_idle;
static uint32_t _last_sample_ms;
static uint8_t _core0_load;
static uint8_t _core1_load;

bool core1_is_running(void) {
    return __atomic_load_n(&_running, __ATOMIC_ACQUIRE) != 0;
}

bool core1_is_worker(void) {
    return core1_is_running() && get_core_num() == 1;
}

void core1_service_set_running(bool running) {
    __atomic_store_n(&_running, running ? 1u : 0u, __ATOMIC_RELEASE);
}

void core1_call(core1_task_fn task, void *context) {
    if (!task) return;
    if (!core1_is_running() || core1_is_worker()) {
        task(context);
        return;
    }

    core1_request_t request = {
        .task = task,
        .context = context,
        .done = 0,
    };

    uint32_t head;
    for (;;) {
        head = __atomic_load_n(&_head, __ATOMIC_RELAXED);
        uint32_t tail = __atomic_load_n(&_tail, __ATOMIC_ACQUIRE);
        if (head - tail < CORE1_QUEUE_SIZE) break;
        watchdog_update();
        tight_loop_contents();
    }

    _queue[head & CORE1_QUEUE_MASK] = &request;
    __atomic_store_n(&_head, head + 1u, __ATOMIC_RELEASE);
    __sev();
    while (!__atomic_load_n(&request.done, __ATOMIC_ACQUIRE)) {
        watchdog_update();
        tight_loop_contents();
    }
}

bool core1_service_process(void) {
    uint32_t tail = __atomic_load_n(&_tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&_head, __ATOMIC_ACQUIRE);
    if (tail == head) return false;

    core1_request_t *request = _queue[tail & CORE1_QUEUE_MASK];
    __atomic_store_n(&_tail, tail + 1u, __ATOMIC_RELEASE);
    request->task(request->context);
    __atomic_store_n(&request->done, 1u, __ATOMIC_RELEASE);
    __sev();
    return true;
}

void core1_metrics_add_core0_busy(uint32_t elapsed_us) {
    __atomic_fetch_add(&_core0_busy_us, elapsed_us, __ATOMIC_RELAXED);
}

void core1_metrics_add_core1_busy(uint32_t elapsed_us) {
    __atomic_fetch_add(&_core1_busy_us, elapsed_us, __ATOMIC_RELAXED);
}

void core1_metrics_add_idle(uint32_t elapsed_us) {
    volatile uint32_t *counter = get_core_num() == 1 ? &_core1_idle_us : &_core0_idle_us;
    __atomic_fetch_add(counter, elapsed_us, __ATOMIC_RELAXED);
}

void core1_metrics_get_load(uint8_t *core0_percent, uint8_t *core1_percent) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (_last_sample_ms == 0) {
        _last_sample_ms = now;
    } else {
        uint32_t elapsed = now - _last_sample_ms;
        if (elapsed >= 1000u) {
            uint32_t core0_busy = __atomic_load_n(&_core0_busy_us, __ATOMIC_RELAXED);
            uint32_t core1_busy = __atomic_load_n(&_core1_busy_us, __ATOMIC_RELAXED);
            uint32_t core0_idle = __atomic_load_n(&_core0_idle_us, __ATOMIC_RELAXED);
            uint32_t core1_idle = __atomic_load_n(&_core1_idle_us, __ATOMIC_RELAXED);
            uint32_t core0_delta = core0_busy - _last_core0_busy;
            uint32_t core1_delta = core1_busy - _last_core1_busy;
            uint32_t core0_idle_delta = core0_idle - _last_core0_idle;
            uint32_t core1_idle_delta = core1_idle - _last_core1_idle;
            uint32_t core0_active = core0_delta > core0_idle_delta
                ? core0_delta - core0_idle_delta : 0;
            uint32_t core1_active = core1_delta > core1_idle_delta
                ? core1_delta - core1_idle_delta : 0;
            uint32_t core0 = core0_active / elapsed / 10u;
            uint32_t core1 = core1_active / elapsed / 10u;
            _core0_load = (uint8_t)(core0 > 100u ? 100u : core0);
            _core1_load = (uint8_t)(core1 > 100u ? 100u : core1);
            _last_core0_busy = core0_busy;
            _last_core1_busy = core1_busy;
            _last_core0_idle = core0_idle;
            _last_core1_idle = core1_idle;
            _last_sample_ms = now;
        }
    }
    if (core0_percent) *core0_percent = _core0_load;
    if (core1_percent) *core1_percent = _core1_load;
}
