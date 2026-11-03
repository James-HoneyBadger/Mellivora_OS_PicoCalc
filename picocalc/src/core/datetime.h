#pragma once

#include <stdint.h>

int64_t datetime_ymd_to_epoch_ms(int year, int month, int day, int hour, int minute, int second);
void datetime_epoch_ms_to_ymd(int64_t epoch_ms, int *year, int *month, int *day, int *hour,
                              int *minute, int *second);
