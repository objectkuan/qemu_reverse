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

#include "replay.h"

ReplayMode replay_mode = REPLAY_MODE_NONE;
/*! Stores current submode for PLAY mode */
ReplaySubmode play_submode = REPLAY_SUBMODE_UNKNOWN;

/* Suffix for the disk images filenames */
char *replay_image_suffix;


ReplaySubmode replay_get_play_submode(void)
{
    return play_submode;
}
