/*
 * replay-debug.c
 *
 * Copyright (c) 2010-2014 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "exec/cpu-common.h"
#include "exec/cpu-defs.h"

#include "replay.h"
#include "replay-internal.h"

/* Reverse debugging data */

/* Saved handler of the debug exception */
static CPUDebugExcpHandler *prev_debug_excp_handler;
/* Step of the last breakpoint hit.
   Used for seeking in reverse continue mode. */
static uint64_t last_breakpoint_step;
/* Start step, where reverse continue begins,
   or target step for reverse stepping.*/
static uint64_t last_reverse_step;
/* Start step, where reverse continue begins.*/
static uint64_t start_reverse_step;
/* Previously loaded step for reverse continue */
static SavedStateInfo *reverse_state;

/*! Breakpoint handler for pass2 of reverse continue.
    Stops the execution at previously saved breakpoint step. */
static void reverse_continue_pass2_breakpoint_handler(CPUArchState *env)
{
    if (replay_get_current_step() == last_breakpoint_step) {
        CPUState *cpu = ENV_GET_CPU(env);
        CPUDebugExcpHandler *handler = prev_debug_excp_handler;
        prev_debug_excp_handler = NULL;

        play_submode = REPLAY_SUBMODE_NORMAL;

        cpu->exception_index = EXCP_DEBUG;
        /* invoke the breakpoint */
        cpu_set_debug_excp_handler(handler);
        handler(env);
        cpu_exit(cpu);
    }
}

/*! Breakpoint handler for pass1 of reverse continue.
    Saves last breakpoint hit and switches to pass2
    when starting point is reached. */
static void reverse_continue_pass1_breakpoint_handler(CPUArchState *env)
{
    if (replay_get_current_step() == last_reverse_step) {
        CPUState *cpu = ENV_GET_CPU(env);
        /* repeat first pass if breakpoint was not found
           on current iteration */
        if (last_breakpoint_step == reverse_state->step - 1
            && reverse_state != saved_states) {
            last_reverse_step = reverse_state->step;
            /* load previous state */
            --reverse_state;
            last_breakpoint_step = reverse_state->step - 1;
            replay_seek_step(reverse_state->step);
            /* set break should be after seek, because seek resets break */
            replay_set_break(last_reverse_step);
            cpu_loop_exit(cpu);
        } else {
            /* this condition is needed, when no breakpoints were found */
            if (last_breakpoint_step == reverse_state->step - 1) {
                ++last_breakpoint_step;
            }
            cpu_set_debug_excp_handler(
                reverse_continue_pass2_breakpoint_handler);

            reverse_continue_pass2_breakpoint_handler(env);
            replay_seek_step(last_breakpoint_step);
            cpu_loop_exit(cpu);
        }
    } else {
        /* skip watchpoint/breakpoint at the current step
           to allow reverse continue */
        last_breakpoint_step = replay_get_current_step();
    }
}

void replay_reverse_breakpoint(void)
{
    /* we started reverse execution from a breakpoint */
    if (replay_get_current_step() != start_reverse_step) {
        last_breakpoint_step = replay_get_current_step();
    }
}

void replay_reverse_continue(void)
{
    if (replay_mode == REPLAY_MODE_PLAY
        && play_submode == REPLAY_SUBMODE_NORMAL) {
        tb_flush_all();
        play_submode = REPLAY_SUBMODE_REVERSE;

        last_reverse_step = replay_get_current_step();
        start_reverse_step = replay_get_current_step();
        /* load initial state */
        reverse_state = find_nearest_state(replay_get_current_step());
        replay_seek_step(reverse_state->step);
        /* run to current step */
        replay_set_break(last_reverse_step);
        /* decrement to allow breaking at the first step */
        last_breakpoint_step = reverse_state->step - 1;
        prev_debug_excp_handler =
            cpu_set_debug_excp_handler(
                reverse_continue_pass1_breakpoint_handler);
    }
}

/*! Breakpoint handler for reverse stepping.
    Stops at the desired step and skips other breakpoints. */
static void reverse_step_breakpoint_handler(CPUArchState *env)
{
    if (replay_get_current_step() == last_reverse_step) {
        CPUState *cpu = ENV_GET_CPU(env);
        CPUDebugExcpHandler *handler = prev_debug_excp_handler;
        prev_debug_excp_handler = NULL;

        play_submode = REPLAY_SUBMODE_NORMAL;

        cpu->exception_index = EXCP_DEBUG;
        /* invoke the breakpoint */
        cpu_set_debug_excp_handler(handler);
        handler(env);
        cpu_exit(cpu);
    }
}

void replay_reverse_step(void)
{
    if (replay_mode == REPLAY_MODE_PLAY
        && play_submode == REPLAY_SUBMODE_NORMAL
        && replay_get_current_step() > 0) {
        tb_flush_all();
        play_submode = REPLAY_SUBMODE_REVERSE;

        last_reverse_step = replay_get_current_step() - 1;
        replay_seek_step(last_reverse_step);

        prev_debug_excp_handler =
            cpu_set_debug_excp_handler(reverse_step_breakpoint_handler);
    }
}
