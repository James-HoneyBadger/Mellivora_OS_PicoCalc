#include "core/datetime.h"
#include "test_util.h"

static void check_datetime(int year, int month, int day, int hour, int minute, int second) {
    int actual_year, actual_month, actual_day;
    int actual_hour, actual_minute, actual_second;
    int64_t epoch_ms = datetime_ymd_to_epoch_ms(year, month, day, hour, minute, second);

    datetime_epoch_ms_to_ymd(epoch_ms, &actual_year, &actual_month, &actual_day, &actual_hour,
                             &actual_minute, &actual_second);
    CHECK_EQ(actual_year, year);
    CHECK_EQ(actual_month, month);
    CHECK_EQ(actual_day, day);
    CHECK_EQ(actual_hour, hour);
    CHECK_EQ(actual_minute, minute);
    CHECK_EQ(actual_second, second);
}

void test_datetime(void) {
    CHECK_EQ(datetime_ymd_to_epoch_ms(1970, 1, 1, 0, 0, 0), 0);
    CHECK_EQ(datetime_ymd_to_epoch_ms(2000, 1, 1, 0, 0, 0), 946684800000LL);
    CHECK_EQ(datetime_ymd_to_epoch_ms(2000, 2, 29, 0, 0, 0), 951782400000LL);
    CHECK_EQ(datetime_ymd_to_epoch_ms(2024, 2, 29, 12, 34, 56), 1709210096000LL);
    CHECK_EQ(datetime_ymd_to_epoch_ms(2100, 3, 1, 0, 0, 0), 4107542400000LL);

    check_datetime(1970, 1, 1, 0, 0, 0);
    check_datetime(2000, 2, 29, 23, 59, 59);
    check_datetime(2024, 2, 29, 12, 34, 56);
    check_datetime(2038, 1, 19, 3, 14, 7);
    check_datetime(2100, 3, 1, 0, 0, 0);
    check_datetime(2400, 2, 29, 6, 7, 8);
}
