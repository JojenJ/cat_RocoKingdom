#include "common/time_utils.h"

#include <stdio.h>
#include <time.h>

#include "esp_timer.h"

uint64_t time_utils_now_seconds(void)
{
    time_t now = 0;
    time(&now);
    if (now > 1700000000) {
        return (uint64_t)now;
    }
    return (uint64_t)(esp_timer_get_time() / 1000000ULL);
}

void time_utils_format_compact(uint64_t timestamp_seconds, char *buffer, size_t buffer_len)
{
    if (buffer == NULL || buffer_len == 0) {
        return;
    }

    if (timestamp_seconds > 1700000000ULL) {
        time_t timestamp = (time_t)timestamp_seconds;
        struct tm tm_info = {0};
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS)
        localtime_r(&timestamp, &tm_info);
#else
        struct tm *tmp = localtime(&timestamp);
        if (tmp != NULL) {
            tm_info = *tmp;
        }
#endif
        snprintf(buffer, buffer_len, "%04d-%02d-%02d %02d:%02d",
                 tm_info.tm_year + 1900,
                 tm_info.tm_mon + 1,
                 tm_info.tm_mday,
                 tm_info.tm_hour,
                 tm_info.tm_min);
        return;
    }

    snprintf(buffer, buffer_len, "uptime:%llus", (unsigned long long)timestamp_seconds);
}
