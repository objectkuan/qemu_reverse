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

#include "qapi-types.h"

extern ReplayMode replay_mode;
extern char *replay_image_suffix;

/*! Returns replay play submode */
ReplaySubmode replay_get_play_submode(void);

#endif
