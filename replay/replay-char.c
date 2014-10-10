/*
 * replay-char.c
 *
 * Copyright (c) 2010-2014 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "replay.h"
#include "replay-internal.h"
#include "sysemu/sysemu.h"
#include "sysemu/char.h"

#define MAX_CHAR_DRIVERS MAX_SERIAL_PORTS
/* Char drivers that generate qemu_chr_be_write events
   that should be saved into the log. */
static CharDriverState *char_drivers[MAX_CHAR_DRIVERS];

/* Char event attributes. */
typedef struct CharEvent {
    int id;
    uint8_t *buf;
    size_t len;
} CharEvent;

static int find_char_driver(CharDriverState *chr)
{
    int i = 0;
    while (i < MAX_CHAR_DRIVERS && char_drivers[i] != chr) {
        ++i;
    }

    return i >= MAX_CHAR_DRIVERS ? -1 : i;
}


void replay_register_char_driver(CharDriverState *chr)
{
    chr->replay = true;
    int i = find_char_driver(NULL);

    if (i < 0) {
        fprintf(stderr, "Replay: cannot register char driver\n");
        exit(1);
    } else {
        char_drivers[i] = chr;
    }
}

void replay_chr_be_write(CharDriverState *s, uint8_t *buf, int len)
{
    CharEvent *event = g_malloc0(sizeof(CharEvent));

    event->id = find_char_driver(s);
    if (event->id < 0) {
        fprintf(stderr, "Replay: cannot find char driver\n");
        exit(1);
    }
    event->buf = g_malloc(len);
    memcpy(event->buf, buf, len);
    event->len = len;

    replay_add_event(REPLAY_ASYNC_EVENT_CHAR, event);
}

void replay_event_char_run(void *opaque)
{
    CharEvent *event = (CharEvent *)opaque;

    qemu_chr_be_write_impl(char_drivers[event->id], event->buf,
                           (int)event->len);

    g_free(event->buf);
    g_free(event);
}

void replay_event_char_save(void *opaque)
{
    CharEvent *event = (CharEvent *)opaque;

    replay_put_byte(event->id);
    replay_put_array(event->buf, event->len);
}

void *replay_event_char_read(void)
{
    CharEvent *event = g_malloc0(sizeof(CharEvent));

    event->id = replay_get_byte();
    replay_get_array_alloc(&event->buf, &event->len);

    return event;
}
