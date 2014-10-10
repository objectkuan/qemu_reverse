/*
 * replay.c
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

ReplayMode replay_mode = REPLAY_MODE_NONE;
/*! Stores current submode for PLAY mode */
ReplaySubmode play_submode = REPLAY_SUBMODE_UNKNOWN;

/* Suffix for the disk images filenames */
char *replay_image_suffix;

ReplayState replay_state;


ReplaySubmode replay_get_play_submode(void)
{
    return play_submode;
}

bool skip_async_events(int stop_event)
{
    /* nothing to skip - not all instructions used */
    if (first_cpu != NULL && first_cpu->instructions_count != 0
        && replay_has_unread_data) {
        return stop_event == EVENT_INSTRUCTION;
    }

    bool res = false;
    while (true) {
        replay_fetch_data_kind();
        if (stop_event == replay_data_kind) {
            res = true;
        }
        switch (replay_data_kind) {
        case EVENT_INSTRUCTION:
            first_cpu->instructions_count = replay_get_dword();
            return res;
        default:
            /* clock, time_t, checkpoint and other events */
            return res;
        }
    }

    return res;
}

void skip_async_events_until(unsigned int kind)
{
    if (!skip_async_events(kind)) {
        if (replay_data_kind == EVENT_ASYNC && kind == EVENT_INSTRUCTION) {
            return;
        }

        fprintf(stderr, "%"PRId64": Read data kind %d instead of expected %d\n",
            replay_get_current_step(), replay_data_kind, kind);
        exit(1);
    }
}

void replay_instruction(int process_events)
{
    if (replay_state.skipping_instruction) {
        replay_state.skipping_instruction = 0;
        return;
    }

    if (replay_file) {
        if (replay_mode == REPLAY_MODE_RECORD) {
            if (process_events && replay_has_events()) {
                replay_save_instructions();
                /* events will be after the last instruction */
                replay_save_events(-1);
            } else {
                /* instruction - increase the step counter */
                ++first_cpu->instructions_count;
            }
        } else if (replay_mode == REPLAY_MODE_PLAY) {
            skip_async_events_until(EVENT_INSTRUCTION);
            if (first_cpu->instructions_count >= 1) {
                ++replay_state.current_step;
                --first_cpu->instructions_count;
                if (first_cpu->instructions_count == 0) {
                    replay_has_unread_data = 0;
                }
            } else {
                replay_read_events(-1);
            }
        }
    }
}

void replay_undo_last_instruction(void)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        first_cpu->instructions_count--;
    } else {
        replay_state.skipping_instruction = 1;
    }
}

bool replay_has_async_request(void)
{
    if (replay_state.skipping_instruction) {
        return false;
    }

    if (replay_mode == REPLAY_MODE_PLAY) {
        if (skip_async_events(EVENT_ASYNC)) {
            return true;
        }

        return false;
    } else if (replay_mode == REPLAY_MODE_RECORD) {
        if (replay_has_events()) {
            return true;
        }
    }

    return false;
}

bool replay_has_instruction(void)
{
    if (replay_state.skipping_instruction) {
        return true;
    }

    if (replay_mode == REPLAY_MODE_PLAY) {
        skip_async_events(EVENT_INSTRUCTION);
        if (replay_data_kind != EVENT_INSTRUCTION
            && replay_data_kind != EVENT_ASYNC) {
            return false;
        }
    }
    return true;
}

uint64_t replay_get_current_step(void)
{
    if (first_cpu == NULL) {
        return 0;
    }
    if (replay_file) {
        if (replay_mode == REPLAY_MODE_RECORD) {
            return replay_state.current_step + first_cpu->instructions_count;
        }
    }
    return replay_state.current_step;
}

bool replay_exception(void)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_save_instructions();
        replay_put_event(EVENT_EXCEPTION);
        return true;
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        if (skip_async_events(EVENT_EXCEPTION)) {
            replay_has_unread_data = 0;
            return true;
        }
        return false;
    }

    return true;
}

bool replay_interrupt(void)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_save_instructions();
        replay_put_event(EVENT_INTERRUPT);
        return true;
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        if (skip_async_events(EVENT_INTERRUPT)) {
            replay_has_unread_data = 0;
            return true;
        }
        return false;
    }

    return true;
}

bool replay_has_interrupt(void)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        return skip_async_events(EVENT_INTERRUPT);
    }
    return false;
}
