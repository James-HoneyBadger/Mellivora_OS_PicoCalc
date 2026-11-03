#include "datetime.h"

int64_t datetime_ymd_to_epoch_ms(int y, int m, int d, int hh, int mm, int ss) {
    y -= (m <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153U * (unsigned)(m + (m > 2 ? -3 : 9)) + 2U) / 5U + (unsigned)(d - 1);
    unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    int64_t days = (int64_t)era * 146097LL + (int64_t)doe - 719468LL;
    return (days * 86400LL + (int64_t)hh * 3600LL + (int64_t)mm * 60LL + (int64_t)ss) * 1000LL;
}

void datetime_epoch_ms_to_ymd(int64_t ms, int *y, int *mo, int *d, int *hh, int *mm, int *ss) {
    int64_t s = ms / 1000;
    int64_t day = s / 86400;
    if (s < 0 && (s % 86400)) day--;
    int64_t tod = s - day * 86400;
    *hh = (int)(tod / 3600);
    *mm = (int)((tod / 60) % 60);
    *ss = (int)(tod % 60);
    int64_t z = day + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yr = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned mn = mp + (mp < 10 ? 3 : -9);
    yr += (mn <= 2);
    *y = yr;
    *mo = (int)mn;
    *d = (int)dd;
}
