/*
 * replay-audio.c
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
#include "replay.h"
#include "replay-internal.h"
#ifdef _WIN32
struct audsettings;
#include "audio/audio_win_int.h"
#endif

/* Sound card state */
typedef struct {
    void *instance;
    const int event_id;
#ifdef _WIN32
    WAVEHDR *queue;
#endif
    /*! Maximum size of the queue */
    int size;
    /*! Current size of the queue */
    sig_atomic_t cur_size;
    unsigned int head, tail;
} SoundQueue;


static SoundQueue sound_in = {
        .event_id = EVENT_SOUND_IN
    },
    sound_out = {
        .event_id = EVENT_SOUND_OUT,
    };

#ifdef _WIN32
/*! Spinlock for sound events processing. */
static spinlock_t sound_lock = SPIN_LOCK_UNLOCKED;
#endif

/*****************************************************************************
 *  Sound queue functions                                                    *
 *****************************************************************************/

/* callback functions */
#ifdef _WIN32

void replay_init_sound_in(void *instance, WAVEHDR *hdrs, int sz)
{
    sound_in.instance = instance;
    sound_in.queue = hdrs;
    sound_in.size = sz;
    sound_in.head = 0;
    sound_in.tail = 0;
    sound_in.cur_size = 0;
}

void replay_init_sound_out(void *instance, WAVEHDR *hdrs, int sz)
{
    sound_out.instance = instance;
    sound_out.queue = hdrs;
    sound_out.size = sz;
    sound_out.head = 0;
    sound_out.tail = 0;
    sound_out.cur_size = 0;
}

static int sound_queue_add(SoundQueue *q, WAVEHDR *hdr)
{
    if (q->queue + q->tail != hdr) {
        /* state was loaded and we need to reset the queue */
        if (q->cur_size == 0) {
            q->head = q->tail = hdr - q->queue;
        } else {
            fprintf(stderr, "Replay: Sound queue error\n");
            exit(1);
        }
    }

    if (q->cur_size == q->size) {
        if (replay_mode == REPLAY_MODE_PLAY) {
            return 1;
        }

        fprintf(stderr, "Replay: Sound queue overflow\n");
        exit(1);
    }

    q->tail = (q->tail + 1) % q->size;
    ++q->cur_size;

    return 0;
}

void replay_save_sound_out(void)
{
    spin_lock(&sound_lock);
    while (sound_out.cur_size != 0) {
        /* put the message ID */
        replay_put_event(sound_out.event_id);
        /* save the buffer size */
        replay_put_dword(sound_out.queue[sound_out.head].dwBytesRecorded);
        /* perform winwave-specific actions */
        winwave_callback_out_impl(sound_out.instance,
                                  &sound_out.queue[sound_out.head]);
        /* goto the next buffer */
        sound_out.head = (sound_out.head + 1) % sound_out.size;
        --sound_out.cur_size;
    }
    spin_unlock(&sound_lock);
}

void replay_save_sound_in(void)
{
    spin_lock(&sound_lock);
    while (sound_in.cur_size != 0) {
        /* put the message ID */
        replay_put_event(sound_in.event_id);
        /* save the buffer */
        replay_put_array((const uint8_t *)sound_in.queue[sound_in.head].lpData,
                         sound_in.queue[sound_in.head].dwBytesRecorded);
        /* perform winwave-specific actions */
        winwave_callback_in_impl(sound_in.instance,
                                 &sound_in.queue[sound_in.head]);
        /* goto the next buffer */
        sound_in.head = (sound_in.head + 1) % sound_in.size;
        --sound_in.cur_size;
    }
    spin_unlock(&sound_lock);
}

void replay_read_sound_out(void)
{
    if (sound_out.cur_size == 0) {
        fprintf(stderr, "Replay: Sound queue underflow\n");
        exit(1);
    }

    /* get the buffer size */
    sound_out.queue[sound_out.head].dwBytesRecorded = replay_get_dword();

    replay_check_error();
    replay_has_unread_data = 0;

    /* perform winwave-specific actions */
    winwave_callback_out_impl(sound_out.instance,
                              &sound_out.queue[sound_out.head]);
    sound_out.head = (sound_out.head + 1) % sound_out.size;
    --sound_out.cur_size;
}

void replay_read_sound_in(void)
{
    if (sound_in.cur_size == 0) {
        fprintf(stderr, "Replay: Sound queue underflow\n");
        exit(1);
    }

    /* get the buffer size */
    size_t size;
    replay_get_array((uint8_t *)sound_in.queue[sound_in.head].lpData, &size);
    sound_in.queue[sound_in.head].dwBytesRecorded = (unsigned int)size;

    replay_check_error();
    replay_has_unread_data = 0;

    /* perform winwave-specific actions */
    winwave_callback_in_impl(sound_in.instance, &sound_in.queue[sound_in.head]);
    sound_in.head = (sound_in.head + 1) % sound_in.size;
    --sound_in.cur_size;
}

void replay_sound_in_event(WAVEHDR *hdr)
{
    spin_lock(&sound_lock);
    if (sound_queue_add(&sound_in, hdr)) {
        fprintf(stderr, "Replay: Input sound buffer overflow\n");
        exit(1);
    }
    spin_unlock(&sound_lock);
}

int replay_sound_out_event(WAVEHDR *hdr)
{
    spin_lock(&sound_lock);
    int result = sound_queue_add(&sound_out, hdr);
    spin_unlock(&sound_lock);

    return result;
}
#endif

bool replay_has_sound_events(void)
{
    return sound_in.cur_size || sound_out.cur_size;
}

void replay_sound_flush_queue(void)
{
#ifdef _WIN32
    spin_lock(&sound_lock);
    while (sound_out.cur_size != 0) {
        /* perform winwave-specific actions */
        winwave_callback_out_impl(sound_out.instance,
                                  &sound_out.queue[sound_out.head]);
        /* goto the next buffer */
        sound_out.head = (sound_out.head + 1) % sound_out.size;
        --sound_out.cur_size;
    }
    while (sound_in.cur_size != 0) {
        /* perform winwave-specific actions */
        winwave_callback_in_impl(sound_in.instance,
                                 &sound_in.queue[sound_in.head]);
        /* goto the next buffer */
        sound_in.head = (sound_in.head + 1) % sound_in.size;
        --sound_in.cur_size;
    }
    spin_unlock(&sound_lock);
#endif
}

