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

/* Current version of the replay mechanism.
   Increase it when file format changes. */
#define REPLAY_VERSION              0xe02001
/* Size of replay log header */
#define HEADER_SIZE                 (sizeof(uint32_t) + sizeof(uint64_t))

ReplayMode replay_mode = REPLAY_MODE_NONE;
/*! Stores current submode for PLAY mode */
ReplaySubmode play_submode = REPLAY_SUBMODE_UNKNOWN;

/* Name of replay file  */
static char *replay_filename;
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
        case EVENT_SHUTDOWN:
            replay_has_unread_data = 0;
            qemu_system_shutdown_request_impl();
            break;
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

void replay_shutdown_request(void)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_put_event(EVENT_SHUTDOWN);
    }
}

/* Used checkpoints: 2 3 5 6 7 8 9 */
int replay_checkpoint(unsigned int checkpoint)
{
    replay_save_instructions();

    if (replay_file) {
        if (replay_mode == REPLAY_MODE_PLAY) {
            if (!skip_async_events(EVENT_CHECKPOINT + checkpoint)) {
                if (replay_data_kind == EVENT_ASYNC_OPT) {
                    replay_read_events(checkpoint);
                    replay_fetch_data_kind();
                    return replay_data_kind != EVENT_ASYNC_OPT;
                }
                return 0;
            }
            replay_has_unread_data = 0;
            replay_read_events(checkpoint);
            replay_fetch_data_kind();
            return replay_data_kind != EVENT_ASYNC_OPT;
        } else if (replay_mode == REPLAY_MODE_RECORD) {
            replay_put_event(EVENT_CHECKPOINT + checkpoint);
            replay_save_events(checkpoint);
        }
    }

    return 1;
}

static void replay_enable(const char *fname, int mode)
{
    const char *fmode = NULL;
    if (replay_file) {
        fprintf(stderr,
                "Replay: some record/replay operation is already started\n");
        return;
    }

    switch (mode) {
    case REPLAY_MODE_RECORD:
        fmode = "wb";
        break;
    case REPLAY_MODE_PLAY:
        fmode = "rb";
        play_submode = REPLAY_SUBMODE_NORMAL;
        break;
    default:
        fprintf(stderr, "Replay: internal error: invalid replay mode\n");
        exit(1);
    }

    atexit(replay_finish);

    replay_file = fopen(fname, fmode);
    if (replay_file == NULL) {
        fprintf(stderr, "Replay: open %s: %s\n", fname, strerror(errno));
        exit(1);
    }

    replay_filename = g_strdup(fname);

    replay_mode = mode;
    replay_has_unread_data = 0;
    replay_data_kind = -1;
    replay_state.skipping_instruction = 0;
    replay_state.current_step = 0;

    /* skip file header for RECORD and check it for PLAY */
    if (replay_mode == REPLAY_MODE_RECORD) {
        fseek(replay_file, HEADER_SIZE, SEEK_SET);
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        unsigned int version = replay_get_dword();
        uint64_t offset = replay_get_qword();
        if (version != REPLAY_VERSION) {
            fprintf(stderr, "Replay: invalid input log file version\n");
            exit(1);
        }
        /* go to the beginning */
        fseek(replay_file, 12, SEEK_SET);
    }

    replay_init_events();
}

void replay_configure(QemuOpts *opts, int mode)
{
    const char *fname;

    fname = qemu_opt_get(opts, "fname");
    if (!fname) {
        fprintf(stderr, "File name not specified for replay\n");
        exit(1);
    }

    const char *suffix = qemu_opt_get(opts, "suffix");
    if (suffix) {
        replay_image_suffix = g_strdup(suffix);
    } else {
        replay_image_suffix = g_strdup("replay_qcow");
    }

    replay_icount = (int)qemu_opt_get_number(opts, "icount", 0);

    replay_enable(fname, mode);
}

void replay_init_timer(void)
{
    if (replay_mode == REPLAY_MODE_NONE) {
        return;
    }

    replay_enable_events();
}

void replay_finish(void)
{
    if (replay_mode == REPLAY_MODE_NONE) {
        return;
    }

    replay_save_instructions();

    /* finalize the file */
    if (replay_file) {
        if (replay_mode == REPLAY_MODE_RECORD) {
            uint64_t offset = 0;
            /* write end event */
            replay_put_event(EVENT_END);

            /* write header */
            fseek(replay_file, 0, SEEK_SET);
            replay_put_dword(REPLAY_VERSION);
            replay_put_qword(offset);
        }

        fclose(replay_file);
        replay_file = NULL;
    }
    if (replay_filename) {
        g_free(replay_filename);
        replay_filename = NULL;
    }
    if (replay_image_suffix) {
        g_free(replay_image_suffix);
        replay_image_suffix = NULL;
    }

    replay_finish_events();
}
