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
#include "qapi-types.h"

extern ReplayMode replay_mode;
extern char *replay_image_suffix;

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


#endif
