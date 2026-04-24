#pragma once

#include <stddef.h>
#include <stdint.h>

uint64_t time_utils_now_seconds(void);
void time_utils_format_compact(uint64_t timestamp_seconds, char *buffer, size_t buffer_len);
