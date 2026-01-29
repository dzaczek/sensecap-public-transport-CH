#ifndef INDICATOR_TIME_H
#define INDICATOR_TIME_H

#include "config.h"
#include "view_data.h"
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif


//ntp sync
int indicator_time_init(void);

/** @return true if system time has been synced (e.g. year >= 2020) */
bool indicator_time_is_synced(void);

// set TZ
int indicator_time_net_zone_set( char *p);

#ifdef __cplusplus
}
#endif

#endif
