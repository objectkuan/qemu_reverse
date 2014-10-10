/*
 * replay-qmp.c
 *
 * Copyright (c) 2010-2014 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "qmp-commands.h"
#include "qapi/qmp/qobject.h"
#include "qapi/qmp-input-visitor.h"
#include "replay/replay.h"
#include "replay/replay-internal.h"

ReplayInfo *qmp_replay_info(Error **errp)
{
    ReplayInfo *info = g_malloc0(sizeof(*info));

    info->mode = replay_mode;
    info->submode = replay_get_play_submode();
    info->step = replay_get_current_step();
    info->break_step = replay_get_break_step();

    return info;
}

void qmp_replay_break(uint64_t step, Error **errp)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        if (step >= replay_get_current_step()) {
            replay_set_break(step);
        } else {
            error_setg(errp, "Cannot stop on the preceding step");
        }
    } else {
        error_setg(errp, "replay_break can be used only in PLAY mode");
    }
}
