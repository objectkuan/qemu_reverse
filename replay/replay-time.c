/*
 * replay-time.c
 *
 * Copyright (c) 2010-2014 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "replay.h"
#include "replay-internal.h"


void replay_save_clock(unsigned int kind, int64_t clock)
{
    replay_save_instructions();

    if (kind >= REPLAY_CLOCK_COUNT) {
        fprintf(stderr, "invalid clock ID %d for replay\n", kind);
        exit(1);
    }

    if (replay_file) {
        replay_put_event(EVENT_CLOCK + kind);
        replay_put_qword(clock);
    }
}

void replay_read_next_clock(unsigned int kind)
{
    replay_fetch_data_kind();
    if (replay_file) {
        unsigned int read_kind = replay_data_kind - EVENT_CLOCK;

        if (kind != -1 && read_kind != kind) {
            return;
        }
        if (read_kind >= REPLAY_CLOCK_COUNT) {
            fprintf(stderr,
                    "invalid clock ID %d was read from replay\n", read_kind);
            exit(1);
        }

        int64_t clock = replay_get_qword();

        replay_check_error();
        replay_has_unread_data = 0;

        replay_state.cached_clock[read_kind] = clock;
    }
}

/*! Reads next clock event from the input. */
int64_t replay_read_clock(unsigned int kind)
{
    if (kind >= REPLAY_CLOCK_COUNT) {
        fprintf(stderr, "invalid clock ID %d for replay\n", kind);
        exit(1);
    }

    if (replay_file) {
        if (skip_async_events(EVENT_CLOCK + kind)) {
            replay_read_next_clock(kind);
        }
        int64_t ret = replay_state.cached_clock[kind];

        return ret;
    }

    fprintf(stderr, "REPLAY INTERNAL ERROR %d\n", __LINE__);
    exit(1);
}

/*! Saves time_t value to the log */
static void replay_save_time_t(time_t tm)
{
    replay_save_instructions();

    if (replay_file) {
        replay_put_event(EVENT_TIME_T);
        if (sizeof(tm) == 4) {
            replay_put_dword(tm);
        } else if (sizeof(tm) == 8) {
            replay_put_qword(tm);
        } else {
            fprintf(stderr, "invalid time_t sizeof: %u\n",
                    (unsigned)sizeof(tm));
            exit(1);
        }
    }
}

/*! Reads time_t value from the log. Stops execution in case of error */
static time_t replay_read_time_t(void)
{
    if (replay_file) {
        time_t tm;

        skip_async_events_until(EVENT_TIME_T);

        if (sizeof(tm) == 4) {
            tm = replay_get_dword();
        } else if (sizeof(tm) == 8) {
            tm = replay_get_qword();
        } else {
            fprintf(stderr, "invalid time_t sizeof: %u\n",
                    (unsigned)sizeof(tm));
            exit(1);
        }

        replay_check_error();

        replay_has_unread_data = 0;

        return tm;
    }

    fprintf(stderr, "REPLAY INTERNAL ERROR %d\n", __LINE__);
    exit(1);
}

void replay_save_tm(struct tm *tm)
{
    replay_save_instructions();

    if (replay_file) {
        replay_put_event(EVENT_TM);

        replay_put_dword(tm->tm_sec);
        replay_put_dword(tm->tm_min);
        replay_put_dword(tm->tm_hour);
        replay_put_dword(tm->tm_mday);
        replay_put_dword(tm->tm_mon);
        replay_put_dword(tm->tm_year);
        replay_put_dword(tm->tm_wday);
        replay_put_dword(tm->tm_yday);
        replay_put_dword(tm->tm_isdst);
    }
}

void replay_read_tm(struct tm *tm)
{
    if (replay_file) {
        skip_async_events_until(EVENT_TM);

        tm->tm_sec = replay_get_dword();
        tm->tm_min = replay_get_dword();
        tm->tm_hour = replay_get_dword();
        tm->tm_mday = replay_get_dword();
        tm->tm_mon = replay_get_dword();
        tm->tm_year = replay_get_dword();
        tm->tm_wday = replay_get_dword();
        tm->tm_yday = replay_get_dword();
        tm->tm_isdst = replay_get_dword();

        replay_check_error();
        replay_has_unread_data = 0;

        return;
    }

    fprintf(stderr, "REPLAY INTERNAL ERROR %d\n", __LINE__);
    exit(1);
}

time_t replay_time(void)
{
    time_t systime;

    if (replay_mode == REPLAY_MODE_RECORD) {
        systime = time(NULL);
        replay_save_time_t(systime);
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        systime = replay_read_time_t();
    } else {
        systime = time(NULL);
    }

    return systime;
}
