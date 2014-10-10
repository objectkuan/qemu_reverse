/*
 * replay_helper.c
 *
 * Copyright (c) 2010-2014 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "cpu.h"
#include "exec/helper-proto.h"
#include "replay/replay.h"

uint32_t helper_replay_instruction(CPUARMState *env)
{
    CPUState *cpu = ENV_GET_CPU(env);
    if (replay_mode == REPLAY_MODE_PLAY
        && !replay_has_instruction()) {
        cpu->exception_index = EXCP_REPLAY;
        return 1;
    }

    if (cpu->exit_request) {
        cpu->exception_index = EXCP_REPLAY;
        return 1;
    }

    int timer = replay_has_async_request();
    replay_instruction(timer);
    return timer;
}
