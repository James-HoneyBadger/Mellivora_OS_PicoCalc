/*
 * test_workbench.c — shell command logic against mocked peripheral registers.
 */

#include "core/workbench.h"
#include "mock_peripherals.h"
#include "test_util.h"

#include <string.h>

typedef struct {
    char text[4096];
    size_t len;
} output_t;

static void _capture(void *ctx, const char *text) {
    output_t *out = (output_t *)ctx;
    size_t n = strlen(text);
    if (out->len + n >= sizeof out->text) return;
    memcpy(out->text + out->len, text, n);
    out->len += n;
    out->text[out->len] = '\0';
}

static mock_hw_t _mock;
static output_t _out;

static void _reset(void) {
    mock_hw_install(&_mock);
    _out.text[0] = '\0';
    _out.len = 0;
    pinmap_select_wlan(false);
}

static bool _contains(const char *needle) {
    return strstr(_out.text, needle) != NULL;
}

void test_workbench(void) {
    wb_io_t io = {.out = _capture, .out_ctx = &_out};

    /* ---- pin map ---- */
    _reset();
    CHECK_EQ(wb_pins(&io), 0);
    CHECK(_contains("Pin map"));
    CHECK(_contains("GP6-7"));
    CHECK(_contains("kbd-i2c"));
    CHECK(_contains("GP10-15"));
    CHECK(_contains("Free:"));
    CHECK(_contains("UART  uart1"));
    CHECK(_contains("I2C   i2c0"));
    CHECK(_contains("SPI   spi0"));
    CHECK(_contains("shares console"));

    /* Without a radio the CYW43 pins are reported as free. */
    CHECK(_contains(" 24 "));

    /* ---- gpio write / read through the register mock ---- */
    _reset();
    CHECK_EQ(wb_gpio(&io, "5 high"), 0);
    CHECK((_mock.out & (1u << 5)) != 0);
    CHECK((_mock.oe & (1u << 5)) != 0);
    CHECK_EQ(_mock.gpio_put_calls, 1);

    _out.text[0] = '\0';
    _out.len = 0;
    CHECK_EQ(wb_gpio(&io, "read 5"), 0);
    CHECK_STR_EQ(_out.text, "GP5 = 1\n");

    _out.len = 0;
    _out.text[0] = '\0';
    CHECK_EQ(wb_gpio(&io, "5 low"), 0);
    CHECK((_mock.out & (1u << 5)) == 0);
    _out.len = 0;
    _out.text[0] = '\0';
    CHECK_EQ(wb_gpio(&io, "read 5"), 0);
    CHECK_STR_EQ(_out.text, "GP5 = 0\n");

    /* Input pins follow the pad, not the latch. */
    _reset();
    _mock.in |= (1u << 9);
    _out.len = 0;
    _out.text[0] = '\0';
    CHECK_EQ(wb_gpio(&io, "read 9"), 0);
    CHECK_STR_EQ(_out.text, "GP9 = 1\n");

    /* ---- gpio mode / pull / release ---- */
    _reset();
    CHECK_EQ(wb_gpio(&io, "mode 4 in"), 0);
    CHECK((_mock.claimed & (1u << 4)) != 0);
    CHECK((_mock.oe & (1u << 4)) == 0);
    CHECK_EQ(wb_gpio(&io, "mode 4 out"), 0);
    CHECK((_mock.oe & (1u << 4)) != 0);
    CHECK_EQ(wb_gpio(&io, "pull 4 up"), 0);
    CHECK((_mock.pull_up & (1u << 4)) != 0);
    CHECK_EQ(wb_gpio(&io, "pull 4 down"), 0);
    CHECK((_mock.pull_down & (1u << 4)) != 0);
    CHECK((_mock.pull_up & (1u << 4)) == 0);
    CHECK_EQ(wb_gpio(&io, "free 4"), 0);
    CHECK((_mock.claimed & (1u << 4)) == 0);

    /* ---- reserved pins are refused unless forced ---- */
    _reset();
    CHECK(wb_gpio(&io, "10 high") < 0);
    CHECK(_contains("lcd-spi"));
    CHECK((_mock.out & (1u << 10)) == 0);
    CHECK_EQ(wb_gpio(&io, "--force 10 high"), 0);
    CHECK((_mock.out & (1u << 10)) != 0);

    /* The console UART pins are also protected. */
    _out.len = 0;
    _out.text[0] = '\0';
    CHECK(wb_gpio(&io, "1 high") < 0);
    CHECK(_contains("console"));

    /* ---- invalid arguments ---- */
    _reset();
    CHECK(wb_gpio(&io, "") < 0);
    CHECK(_contains("usage: gpio"));
    CHECK(wb_gpio(&io, "99 high") < 0);
    CHECK(_contains("invalid GPIO"));
    CHECK(wb_gpio(&io, "abc high") < 0);

    /* ---- gpio list ---- */
    _out.len = 0;
    _out.text[0] = '\0';
    CHECK_EQ(wb_gpio(&io, "list"), 0);
    CHECK(_contains("Free GPIO pins:"));

    /* ---- pwm ---- */
    _reset();
    CHECK_EQ(wb_pwm(&io, "5 1000 50"), 0);
    CHECK(_mock.pwm_running[5]);
    CHECK_EQ(_mock.pwm_freq[5], 1000u);
    CHECK_EQ(_mock.pwm_duty[5], 50);
    CHECK_EQ(_mock.pwm_start_calls, 1);

    CHECK_EQ(wb_pwm(&io, "5 25"), 0);
    CHECK_EQ(_mock.pwm_duty[5], 25);

    CHECK_EQ(wb_pwm(&io, "5 off"), 0);
    CHECK(!_mock.pwm_running[5]);
    CHECK_EQ(_mock.pwm_stop_calls, 1);

    /* Reserved pins and bad duty values are rejected. */
    CHECK(wb_pwm(&io, "26 1000 50") < 0);
    CHECK(_contains("audio-pwm"));
    CHECK(wb_pwm(&io, "5 1000 150") < 0);
    CHECK(wb_pwm(&io, "5 0 50") < 0);

    /* ---- i2cdetect ---- */
    _reset();
    mock_i2c_add(&_mock, 0, 0x20);
    mock_i2c_add(&_mock, 0, 0x3C);
    _out.len = 0;
    _out.text[0] = '\0';
    CHECK_EQ(wb_i2cdetect(&io, ""), 2);
    CHECK(_contains("i2cdetect on i2c0"));
    CHECK(_contains("SDA GP8"));
    CHECK(_contains("2 device(s) found"));
    CHECK(_contains("20: 20"));
    CHECK_EQ(_mock.i2c_probe_calls, 0x77 - 0x03 + 1);
    CHECK(_mock.i2c_speed[0] == 100000u);
    CHECK(!_mock.i2c_open[0]); /* closed again after the scan */

    /* Narrowed range and bus selection. */
    _reset();
    mock_i2c_add(&_mock, 1, 0x50);
    _out.len = 0;
    _out.text[0] = '\0';
    CHECK_EQ(wb_i2cdetect(&io, "--bus 1 0x50-0x5f"), 1);
    CHECK(_contains("i2c1"));
    CHECK(_contains("SDA GP2"));
    CHECK(_contains("1 device(s) found"));
    CHECK_EQ(_mock.i2c_probe_calls, 16);

    /* An empty bus still prints the grid. */
    _reset();
    _out.len = 0;
    _out.text[0] = '\0';
    CHECK_EQ(wb_i2cdetect(&io, ""), 0);
    CHECK(_contains("00:"));
    CHECK(_contains("70:"));
    CHECK(_contains("0 device(s) found"));
}
