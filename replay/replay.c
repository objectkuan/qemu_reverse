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
#include "migration/vmstate.h"
#include "monitor/monitor.h"

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

/*
    Auto-saving for VM states data
*/

/* Minimum capacity of saved states information array */
#define SAVED_STATES_MIN_CAPACITY   128
/* Format of the name for the saved state */
#define SAVED_STATE_NAME_FORMAT     "replay_%" PRId64

/* Timer for auto-save VM states */
static QEMUTimer *save_timer;
/* Save state period in seconds */
static uint64_t save_state_period;
/* List of the saved states information */
SavedStateInfo *saved_states;
/* Number of saved states */
static size_t saved_states_count;
/* Capacity of the buffer for saved states */
static size_t saved_states_capacity;
/* Number of last loaded/saved state */
static uint64_t current_saved_state;

/*
   Replay functions
 */

ReplaySubmode replay_get_play_submode(void)
{
    return play_submode;
}

static void replay_pre_save(void *opaque)
{
    ReplayState *state = opaque;
    state->file_offset = ftello64(replay_file);
}

static int replay_post_load(void *opaque, int version_id)
{
    first_cpu->instructions_count = 0;

    ReplayState *state = opaque;
    fseeko64(replay_file, state->file_offset, SEEK_SET);
    replay_has_unread_data = 0;

    return 0;
}

static const VMStateDescription vmstate_replay = {
    .name = "replay",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .pre_save = replay_pre_save,
    .post_load = replay_post_load,
    .fields      = (VMStateField[]) {
        VMSTATE_INT64_ARRAY(cached_clock, ReplayState, REPLAY_CLOCK_COUNT),
        VMSTATE_INT32(skipping_instruction, ReplayState),
        VMSTATE_UINT64(current_step, ReplayState),
        VMSTATE_UINT64(file_offset, ReplayState),
        VMSTATE_END_OF_LIST()
    }
};

static void replay_savevm(void *opaque)
{
    char name[128];
    uint64_t offset;

    offset = ftello64(replay_file);

    replay_save_instructions();

    replay_put_event(EVENT_SAVE_VM_BEGIN);

    vm_stop(RUN_STATE_SAVE_VM);

    /* save VM state */
    sprintf(name, SAVED_STATE_NAME_FORMAT, current_saved_state);
    if (save_vmstate(default_mon, name) > 0) {
        /* if period is 0, save only once */
        if (save_state_period != 0) {
            timer_mod(save_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME)
                                  + save_state_period);
        }

        /* add more memory to buffer */
        if (saved_states_count >= saved_states_capacity) {
            saved_states_capacity += SAVED_STATES_MIN_CAPACITY;
            saved_states = g_realloc(saved_states, saved_states_capacity
                                                   * sizeof(SavedStateInfo));
            if (!saved_states) {
                saved_states_count = 0;
                fprintf(stderr,
                        "Replay: Saved states memory reallocation failed.\n");
                exit(1);
            }
        }
        /* save state ID into the buffer */
        saved_states[saved_states_count].file_offset = offset;
        saved_states[saved_states_count].step = replay_get_current_step();
        ++saved_states_count;
        ++current_saved_state;
    } else {
        fprintf(stderr, "Cannot save simulator states for replay.\n");
    }

    replay_put_event(EVENT_SAVE_VM_END);

    tb_flush_all();

    vm_start();
}

/*! Checks SAVEVM event while reading event log. */
static void check_savevm(void)
{
    replay_fetch_data_kind();
    if (replay_data_kind != EVENT_SAVE_VM_BEGIN
        && replay_data_kind != EVENT_SAVE_VM_END) {
        fprintf(stderr, "Replay: read wrong data kind %d within savevm\n",
                replay_data_kind);
        exit(1);
    }
    replay_has_unread_data = 0;
}

/*! Loads specified VM state. */
static void replay_loadvm(int64_t state)
{
    char name[128];
    bool running = runstate_is_running();
    if (running && !qemu_in_vcpu_thread()) {
        vm_stop(RUN_STATE_RESTORE_VM);
    } else {
        cpu_disable_ticks();
    }

    replay_clear_events();

    sprintf(name, SAVED_STATE_NAME_FORMAT, state);
    if (load_vmstate(name) < 0) {
        fprintf(stderr, "Replay: cannot load VM state\n");
        exit(1);
    }
    /* check end event */
    check_savevm();

    tb_flush_all();

    current_saved_state = state;

    cpu_enable_ticks();
    if (running && !qemu_in_vcpu_thread()) {
        vm_start();
    }

    replay_fetch_data_kind();
    while (replay_data_kind >= EVENT_CLOCK
           && replay_data_kind < EVENT_CLOCK + REPLAY_CLOCK_COUNT) {
        replay_read_next_clock(-1);
        replay_fetch_data_kind();
    }
}

/*! Skips clock events saved to file while saving the VM state. */
static void replay_skip_savevm(void)
{
    replay_has_unread_data = 0;
    replay_loadvm(current_saved_state + 1);
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
        case EVENT_SAVE_VM_BEGIN:
            /* cannot correctly load VM while in CPU thread */
            if (qemu_in_vcpu_thread()) {
                return res;
            }
            replay_skip_savevm();
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
    current_saved_state = 0;

    replay_net_init();

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
        /* read states table */
        fseeko64(replay_file, offset, SEEK_SET);
        saved_states_count = replay_get_qword();
        saved_states_capacity = saved_states_count;
        if (saved_states_count) {
            saved_states = g_malloc(sizeof(SavedStateInfo)
                                    * saved_states_count);
            fread(saved_states, sizeof(SavedStateInfo), saved_states_count,
                  replay_file);
        }
        replay_net_read_packets_data();
        /* go to the beginning */
        fseek(replay_file, 12, SEEK_SET);
    }

    replay_init_events();

    vmstate_register(NULL, 0, &vmstate_replay, &replay_state);
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
    save_state_period = 1000LL * qemu_opt_get_number(opts, "period", 0);

    replay_enable(fname, mode);
}

void replay_init_timer(void)
{
    if (replay_mode == REPLAY_MODE_NONE) {
        return;
    }

    replay_enable_events();

    /* create timer for states auto-saving */
    if (replay_mode == REPLAY_MODE_RECORD) {
        saved_states_count = 0;
        if (!saved_states) {
            saved_states = g_malloc(sizeof(SavedStateInfo)
                                    * SAVED_STATES_MIN_CAPACITY);
            saved_states_capacity = SAVED_STATES_MIN_CAPACITY;
        }
        if (save_state_period) {
            save_timer = timer_new_ms(QEMU_CLOCK_REALTIME, replay_savevm, NULL);
            timer_mod(save_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
        }
        replay_put_event(EVENT_END_STARTUP);
        /* Save it right now without waiting for timer */
        replay_savevm(NULL);
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        /* load starting VM state */
        replay_loadvm(0);
    }
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

            /* write states table */
            offset = ftello64(replay_file);
            replay_put_qword(saved_states_count);
            if (saved_states && saved_states_count) {
                fwrite(saved_states, sizeof(SavedStateInfo),
                       saved_states_count, replay_file);
            }
            replay_net_write_packets_data();

            /* write header */
            fseek(replay_file, 0, SEEK_SET);
            replay_put_dword(REPLAY_VERSION);
            replay_put_qword(offset);
        }

        fclose(replay_file);
        replay_file = NULL;
    }
    if (save_timer) {
        timer_del(save_timer);
        timer_free(save_timer);
        save_timer = NULL;
    }
    if (saved_states) {
        g_free(saved_states);
        saved_states = NULL;
    }
    if (replay_filename) {
        g_free(replay_filename);
        replay_filename = NULL;
    }
    if (replay_image_suffix) {
        g_free(replay_image_suffix);
        replay_image_suffix = NULL;
    }

    replay_net_free();
    replay_finish_events();
}
