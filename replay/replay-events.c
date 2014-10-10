/*
 * replay-events.c
 *
 * Copyright (c) 2010-2014 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "replay.h"
#include "replay-internal.h"
#include "block/thread-pool.h"
#include "ui/input.h"

typedef struct Event {
    int event_kind;
    void *opaque;
    void *opaque2;
    uint64_t id;

    QTAILQ_ENTRY(Event) events;
} Event;

static QTAILQ_HEAD(, Event) events_list = QTAILQ_HEAD_INITIALIZER(events_list);

static QemuMutex lock;
static unsigned int read_event_kind = -1;
static uint64_t read_id = -1;
static int read_opt = -1;

static bool replay_events_enabled = false;

/* Functions */

static void replay_run_event(Event *event)
{
    switch (event->event_kind) {
    case REPLAY_ASYNC_EVENT_BH:
        aio_bh_call(event->opaque);
        break;
    case REPLAY_ASYNC_EVENT_THREAD:
        thread_pool_work((ThreadPool *)event->opaque, event->opaque2);
        break;
    case REPLAY_ASYNC_EVENT_INPUT:
        qemu_input_event_send_impl(NULL, (InputEvent *)event->opaque);
        /* Using local variables, when replaying. Do not free them. */
        if (replay_mode == REPLAY_MODE_RECORD) {
            qapi_free_InputEvent((InputEvent *)event->opaque);
        }
        break;
    case REPLAY_ASYNC_EVENT_INPUT_SYNC:
        qemu_input_event_sync_impl();
        break;
    case REPLAY_ASYNC_EVENT_NETWORK:
        replay_net_send_packet(event->opaque);
        break;
    case REPLAY_ASYNC_EVENT_CHAR:
        replay_event_char_run(event->opaque);
        break;
#ifdef CONFIG_USB_LIBUSB
    case REPLAY_ASYNC_EVENT_USB_CTRL:
        replay_event_usb_ctrl(event->opaque);
        break;
    case REPLAY_ASYNC_EVENT_USB_DATA:
        replay_event_usb_data(event->opaque);
        break;
    case REPLAY_ASYNC_EVENT_USB_ISO:
        replay_event_usb_iso(event->opaque);
        break;
#endif
    default:
        fprintf(stderr, "Replay: invalid async event ID (%d) in the queue\n",
                event->event_kind);
        exit(1);
        break;
    }
}

void replay_enable_events(void)
{
    replay_events_enabled = true;
}

bool replay_has_events(void)
{
    return !QTAILQ_EMPTY(&events_list);
}

void replay_flush_events(void)
{
    qemu_mutex_lock(&lock);
    while (!QTAILQ_EMPTY(&events_list)) {
        Event *event = QTAILQ_FIRST(&events_list);
        replay_run_event(event);
        QTAILQ_REMOVE(&events_list, event, events);
        g_free(event);
    }
    qemu_mutex_unlock(&lock);
}

void replay_disable_events(void)
{
    replay_events_enabled = false;
    /* Flush events queue before waiting of completion */
    replay_flush_events();
}

void replay_clear_events(void)
{
    qemu_mutex_lock(&lock);
    while (!QTAILQ_EMPTY(&events_list)) {
        Event *event = QTAILQ_FIRST(&events_list);
        QTAILQ_REMOVE(&events_list, event, events);

        g_free(event);
    }
    qemu_mutex_unlock(&lock);
}

static void replay_add_event_internal(int event_kind, void *opaque,
                                      void *opaque2, uint64_t id)
{
    if (event_kind >= REPLAY_ASYNC_COUNT) {
        fprintf(stderr, "Replay: invalid async event ID (%d)\n", event_kind);
        exit(1);
    }
    if (!replay_file || replay_mode == REPLAY_MODE_NONE
        || !replay_events_enabled) {
        Event e;
        e.event_kind = event_kind;
        e.opaque = opaque;
        e.opaque2 = opaque2;
        e.id = id;
        replay_run_event(&e);
        return;
    }

    Event *event = g_malloc0(sizeof(Event));
    event->event_kind = event_kind;
    event->opaque = opaque;
    event->opaque2 = opaque2;
    event->id = id;

    qemu_mutex_lock(&lock);
    QTAILQ_INSERT_TAIL(&events_list, event, events);
    qemu_mutex_unlock(&lock);
}

void replay_add_event(int event_kind, void *opaque)
{
    replay_add_event_internal(event_kind, opaque, NULL, 0);
}

void replay_add_bh_event(void *bh, uint64_t id)
{
    replay_add_event_internal(REPLAY_ASYNC_EVENT_BH, bh, NULL, id);
}

void replay_add_thread_event(void *opaque, void *opaque2, uint64_t id)
{
    replay_add_event_internal(REPLAY_ASYNC_EVENT_THREAD, opaque, opaque2, id);
}

void replay_add_input_event(struct InputEvent *event)
{
    replay_add_event_internal(REPLAY_ASYNC_EVENT_INPUT, event, NULL, 0);
}

void replay_add_input_sync_event(void)
{
    replay_add_event_internal(REPLAY_ASYNC_EVENT_INPUT_SYNC, NULL, NULL, 0);
}

void replay_add_usb_event(unsigned int event_kind, uint64_t id, void *opaque)
{
    replay_add_event_internal(event_kind, opaque, NULL, id);
}

void replay_save_events(int opt)
{
    qemu_mutex_lock(&lock);
    while (!QTAILQ_EMPTY(&events_list)) {
        Event *event = QTAILQ_FIRST(&events_list);
        if (replay_mode != REPLAY_MODE_PLAY) {
            /* put the event into the file */
            if (opt == -1) {
                replay_put_event(EVENT_ASYNC);
            } else {
                replay_put_event(EVENT_ASYNC_OPT);
                replay_put_byte(opt);
            }
            replay_put_byte(event->event_kind);

            /* save event-specific data */
            switch (event->event_kind) {
            case REPLAY_ASYNC_EVENT_BH:
            case REPLAY_ASYNC_EVENT_THREAD:
                replay_put_qword(event->id);
                break;
            case REPLAY_ASYNC_EVENT_INPUT:
                replay_save_input_event(event->opaque);
                break;
            case REPLAY_ASYNC_EVENT_NETWORK:
                replay_net_save_packet(event->opaque);
                break;
            case REPLAY_ASYNC_EVENT_CHAR:
                replay_event_char_save(event->opaque);
                break;
#ifdef CONFIG_USB_LIBUSB
            case REPLAY_ASYNC_EVENT_USB_CTRL:
            case REPLAY_ASYNC_EVENT_USB_DATA:
                replay_put_qword(event->id);
                replay_event_save_usb_xfer(event->opaque);
                break;
            case REPLAY_ASYNC_EVENT_USB_ISO:
                replay_put_qword(event->id);
                replay_event_save_usb_iso_xfer(event->opaque);
                break;
#endif
            }
        }

        replay_run_event(event);
        QTAILQ_REMOVE(&events_list, event, events);
        g_free(event);
    }
    qemu_mutex_unlock(&lock);
}

void replay_read_events(int opt)
{
    replay_fetch_data_kind();
    while ((opt == -1 && replay_data_kind == EVENT_ASYNC)
        || (opt != -1 && replay_data_kind == EVENT_ASYNC_OPT)) {

        if (read_event_kind == -1) {
            if (opt != -1) {
                read_opt = replay_get_byte();
            }
            read_event_kind = replay_get_byte();
            read_id = -1;
            replay_check_error();
        }

        if (opt != read_opt) {
            break;
        }
        /* Execute some events without searching them in the queue */
        Event e;
        switch (read_event_kind) {
        case REPLAY_ASYNC_EVENT_BH:
        case REPLAY_ASYNC_EVENT_THREAD:
            if (read_id == -1) {
                read_id = replay_get_qword();
            }
            break;
        case REPLAY_ASYNC_EVENT_INPUT:
            e.event_kind = read_event_kind;
            e.opaque = replay_read_input_event();

            replay_run_event(&e);

            replay_has_unread_data = 0;
            read_event_kind = -1;
            read_opt = -1;
            replay_fetch_data_kind();
            /* continue with the next event */
            continue;
        case REPLAY_ASYNC_EVENT_INPUT_SYNC:
            e.event_kind = read_event_kind;
            e.opaque = 0;
            replay_run_event(&e);

            replay_has_unread_data = 0;
            read_event_kind = -1;
            read_opt = -1;
            replay_fetch_data_kind();
            /* continue with the next event */
            continue;
        case REPLAY_ASYNC_EVENT_NETWORK:
            e.opaque = replay_net_read_packet();
            e.event_kind = read_event_kind;
            replay_run_event(&e);

            replay_has_unread_data = 0;
            read_event_kind = -1;
            read_opt = -1;
            replay_fetch_data_kind();
            /* continue with the next event */
            continue;
        case REPLAY_ASYNC_EVENT_CHAR:
            e.event_kind = read_event_kind;
            e.opaque = replay_event_char_read();

            replay_has_unread_data = 0;
            read_event_kind = -1;
            read_opt = -1;
            replay_fetch_data_kind();

            replay_run_event(&e);
            /* continue with the next event */
            continue;
#ifdef CONFIG_USB_LIBUSB
        case REPLAY_ASYNC_EVENT_USB_CTRL:
        case REPLAY_ASYNC_EVENT_USB_DATA:
        case REPLAY_ASYNC_EVENT_USB_ISO:
            if (read_id == -1) {
                read_id = replay_get_qword();
            }
            /* read after finding event in the list */
            break;
#endif
        default:
            fprintf(stderr, "Unknown ID %d of replay event\n", read_event_kind);
            exit(1);
            break;
        }

        qemu_mutex_lock(&lock);

        Event *event = NULL;
        Event *curr = NULL;
        QTAILQ_FOREACH(curr, &events_list, events) {
            if (curr->event_kind == read_event_kind
                && (read_id == -1 || read_id == curr->id)) {
                event = curr;
                break;
            }
        }

        if (event) {
            /* read event-specific reading data */
#ifdef CONFIG_USB_LIBUSB
            switch (read_event_kind) {
            case REPLAY_ASYNC_EVENT_USB_CTRL:
            case REPLAY_ASYNC_EVENT_USB_DATA:
                replay_event_read_usb_xfer(event->opaque);
                break;
            case REPLAY_ASYNC_EVENT_USB_ISO:
                replay_event_read_usb_iso_xfer(event->opaque);
                break;
            }
#endif

            QTAILQ_REMOVE(&events_list, event, events);

            qemu_mutex_unlock(&lock);

            /* reset unread data and other parameters to allow
               reading other data from the log while
               running the event */
            replay_has_unread_data = 0;
            read_event_kind = -1;
            read_id = -1;
            read_opt = -1;

            replay_run_event(event);
            g_free(event);

            replay_fetch_data_kind();
        } else {
            qemu_mutex_unlock(&lock);
            /* No such event found in the queue */
            break;
        }
    }
}

void replay_init_events(void)
{
    read_event_kind = -1;
    qemu_mutex_init(&lock);
}

void replay_finish_events(void)
{
    replay_events_enabled = false;
    replay_clear_events();
    qemu_mutex_destroy(&lock);
}
