#pragma once

#include "types.h"
#include "display_minimal.h"

struct tm;

double time_since_start();
i64 milliseconds_since_start();
DisplayCtx time_tm_display(const char* format, struct tm const* time, DisplayCtx d);
struct tm get_current_time_tm_utc();
void sleep_for(double seconds);

#define ISO8601_UTC_FORMAT "%Y-%m-%dT%H:%M:%SZ"
