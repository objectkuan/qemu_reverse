#ifndef REPLAY_H
#define REPLAY_H

/*
 * replay.h
 *
 * Copyright (c) 2010-2014 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "qapi-types.h"

/* replay clock kinds */
/* rdtsc */
#define REPLAY_CLOCK_REAL_TICKS 0
/* host_clock */
#define REPLAY_CLOCK_REALTIME   1
/* vm_clock */
#define REPLAY_CLOCK_VIRTUAL    2

#define REPLAY_CLOCK_COUNT      3

extern ReplayMode replay_mode;
extern char *replay_image_suffix;
/*! Shift value for icount based on replay or zero, if it is disabled. */
extern int replay_icount;

/*! Returns replay play submode */
ReplaySubmode replay_get_play_submode(void);

/* Processing the instructions */

/*! Returns number of executed instructions. */
uint64_t replay_get_current_step(void);
/*! Called before instruction execution */
void replay_instruction(int process_events);
/*! Undo last instruction count, when exception occurs */
void replay_undo_last_instruction(void);
/*! Returns true if asynchronous event is pending */
bool replay_has_async_request(void);
/*! Returns non-zero if next event is instruction. */
bool replay_has_instruction(void);

/* Interrupts and exceptions */

/*! Called by exception handler to write or read
    exception processing events. */
bool replay_exception(void);
/*! Called by interrupt handlers to write or read
    interrupt processing events.
    \return true if interrupt should be processed */
bool replay_interrupt(void);
/*! Tries to read interrupt event from the file.
    Returns true, when interrupt request is pending */
bool replay_has_interrupt(void);

/* Processing clocks and other time sources */

/*! Save the specified clock */
void replay_save_clock(unsigned int kind, int64_t clock);
/*! Read the specified clock from the log or return cached data */
int64_t replay_read_clock(unsigned int kind);
/*! Returns result of time() function execution in normal and record modes.
    In play mode returns value read from the log. */
time_t replay_time(void);
/*! Saves struct tm value to the log */
void replay_save_tm(struct tm *tm);
/*! Reads struct tm value from the log. Stops execution in case of error */
void replay_read_tm(struct tm *tm);

/* Events */

/*! Called when qemu shutdown is requested. */
void replay_shutdown_request(void);
/*! Should be called at check points in the execution.
    These check points are skipped, if they were not met.
    Saves checkpoint in the SAVE mode and validates in the PLAY mode.
    Returns 0 in PLAY mode if checkpoint was not found.
    Returns 1 in all other cases. */
int replay_checkpoint(unsigned int checkpoint);

/* Asynchronous events queue */

/*! Disables storing events in the queue */
void replay_disable_events(void);

/* icount-based virtual clock */

/* Initializes icount-based virtual clock */
void replay_init_icount(void);
/* Returns the virtual CPU time, based on the instruction counter.  */
int64_t replay_get_icount(void);
void replay_clock_warp(void);

#endif
