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
