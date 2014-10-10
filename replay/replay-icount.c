/*
 * replay-icount.c
 *
 * Copyright (c) 2010-2014 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qemu-common.h"
#include "sysemu/cpus.h"
#include "sysemu/sysemu.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "replay.h"
#include "replay-internal.h"

int replay_icount;

typedef struct {
    /* Compensate for varying guest execution speed.  */
    int64_t bias;
    /* Timer for advancing VM clock, when all CPUs are sleeping */
    QEMUTimer *icount_warp_timer;
    int64_t vm_clock_warp_start;
} ReplayIcount;
static ReplayIcount icount_data;


/* Return the virtual CPU time, based on the instruction counter.  */
int64_t replay_get_icount(void)
{
    int64_t icount = replay_get_current_step();
    return icount_data.bias + (icount << replay_icount);
}

static void replay_icount_warp_rt(void *opaque)
{
    if (icount_data.vm_clock_warp_start == -1) {
        return;
    }

    if (runstate_is_running()) {
        int64_t clock = qemu_clock_get_ns(QEMU_CLOCK_HOST);
        int64_t warp_delta = clock - icount_data.vm_clock_warp_start;
        icount_data.bias += warp_delta;
        if (qemu_clock_expired(QEMU_CLOCK_VIRTUAL)) {
            qemu_notify_event();
        }
    }
    icount_data.vm_clock_warp_start = -1;
}

void replay_clock_warp(void)
{
    int64_t deadline;
    if (!replay_checkpoint(9)) {
        return;
    }
    /*
     * If the CPUs have been sleeping, advance the vm_clock timer now.  This
     * ensures that the deadline for the timer is computed correctly below.
     * This also makes sure that the insn counter is synchronized before the
     * CPU starts running, in case the CPU is woken by an event other than
     * the earliest vm_clock timer.
     */
    if (icount_data.vm_clock_warp_start != -1) {
        replay_icount_warp_rt(NULL);
    }
    if (!all_cpu_threads_idle() || !qemu_clock_has_timers(QEMU_CLOCK_VIRTUAL)) {
        timer_del(icount_data.icount_warp_timer);
        return;
    }

    icount_data.vm_clock_warp_start = qemu_clock_get_ns(QEMU_CLOCK_HOST);
    deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL);
    if (deadline > 0) {
        /*
         * Ensure the vm_clock proceeds even when the virtual CPU goes to
         * sleep.  Otherwise, the CPU might be waiting for a future timer
         * interrupt to wake it up, but the interrupt never comes because
         * the vCPU isn't running any insns and thus doesn't advance the
         * vm_clock.
         *
         * An extreme solution for this problem would be to never let VCPUs
         * sleep in icount mode if there is a pending vm_clock timer; rather
         * time could just advance to the next vm_clock event.  Instead, we
         * do stop VCPUs and only advance vm_clock after some "real" time,
         * (related to the time left until the next event) has passed.  This
         * rt_clock timer will do this.  This avoids that the warps are too
         * visible externally---for example, you will not be sending network
         * packets continuously instead of every 100ms.
         */
        timer_mod_ns(icount_data.icount_warp_timer,
                     icount_data.vm_clock_warp_start + deadline);
    } else {
        qemu_notify_event();
    }
}

static const VMStateDescription vmstate_icount = {
    .name = "icount",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_INT64(bias, ReplayIcount),
        VMSTATE_TIMER(icount_warp_timer, ReplayIcount),
        VMSTATE_INT64(vm_clock_warp_start, ReplayIcount),
        VMSTATE_END_OF_LIST()
    }
};

void replay_init_icount(void)
{
    if (!replay_icount) {
        return;
    }

    vmstate_register(NULL, 0, &vmstate_icount, &icount_data);
    icount_data.icount_warp_timer = timer_new_ns(QEMU_CLOCK_HOST,
                                                 replay_icount_warp_rt, NULL);
}
