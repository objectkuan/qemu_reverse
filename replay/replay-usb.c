/*
 * replay-usb.c
 *
 * Copyright (c) 2010-2014 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "replay.h"
#include "replay-internal.h"
#include "hw/usb.h"

#ifdef CONFIG_USB_LIBUSB
#include "hw/host-libusb.h"

static uint64_t replay_get_xfer_id(struct libusb_transfer *xfer)
{
    USBHostRequest *r = xfer->user_data;
    USBHostDevice *host = r->host;

    return ((uint64_t)host->match.vendor_id << 32)
           | host->match.product_id;
}

static uint64_t replay_get_iso_xfer_id(struct libusb_transfer *xfer)
{
    USBHostIsoXfer *r = xfer->user_data;
    USBHostDevice  *host = r->ring->host;

    return ((uint64_t)host->match.vendor_id << 32)
           | host->match.product_id;
}

void replay_req_complete_ctrl(struct libusb_transfer *xfer)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_add_usb_event(REPLAY_ASYNC_EVENT_USB_CTRL,
                             replay_get_xfer_id(xfer), xfer);
    }
}

void replay_req_register_ctrl(struct libusb_transfer *xfer)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        replay_add_usb_event(REPLAY_ASYNC_EVENT_USB_CTRL,
                             replay_get_xfer_id(xfer), xfer);
    }
}

void replay_event_usb_ctrl(void *opaque)
{
    struct libusb_transfer *xfer = opaque;

    usb_host_req_complete_ctrl(xfer);
}

void replay_event_save_usb_xfer(void *opaque)
{
    struct libusb_transfer *xfer = opaque;
    USBHostRequest *r = xfer->user_data;
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_put_dword(xfer->status);
        replay_put_dword(xfer->actual_length);
        replay_put_array(xfer->buffer, r->in ? xfer->length : 0);
    }
}

void replay_event_save_usb_iso_xfer(void *opaque)
{
    struct libusb_transfer *xfer = opaque;
    USBHostIsoXfer *iso = xfer->user_data;
    int i;
    if (replay_mode == REPLAY_MODE_RECORD) {
        bool in = iso->ring->ep->pid == USB_TOKEN_IN;
        replay_put_dword(xfer->status);
        replay_put_dword(xfer->num_iso_packets);
        for (i = 0 ; i < xfer->num_iso_packets ; ++i) {
            /* all other fields of the packet are not used */
            unsigned int len = xfer->iso_packet_desc[i].actual_length;
            if (in) {
                replay_put_array(usb_host_get_iso_packet_buffer(iso, i), len);
            }
        }
    }
}

void replay_event_read_usb_xfer(void *opaque)
{
    struct libusb_transfer *xfer = opaque;
    USBHostRequest *r = xfer->user_data;

    if (replay_mode == REPLAY_MODE_PLAY) {
        xfer->status = replay_get_dword();
        xfer->actual_length = replay_get_dword();
        size_t sz;
        replay_get_array(xfer->buffer, &sz);
        if (r->in && xfer->length != (int)sz) {
            fprintf(stderr, "Replay: trying to read USB control/data buffer with unexpected size\n");
            exit(1);
        }
    }
}

void replay_event_read_usb_iso_xfer(void *opaque)
{
    struct libusb_transfer *xfer = opaque;
    USBHostIsoXfer *iso = xfer->user_data;
    int i;

    if (replay_mode == REPLAY_MODE_PLAY) {
        bool in = iso->ring->ep->pid == USB_TOKEN_IN;
        xfer->status = replay_get_dword();
        xfer->num_iso_packets = replay_get_dword();
        for (i = 0 ; i < xfer->num_iso_packets ; ++i) {
            /* all other fields of the packet are not used */
            if (in) {
                size_t sz;
                replay_get_array(usb_host_get_iso_packet_buffer(iso, i), &sz);
                xfer->iso_packet_desc[i].actual_length = (unsigned int)sz;
            }
        }
    }
}

void replay_req_complete_data(struct libusb_transfer *xfer)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_add_usb_event(REPLAY_ASYNC_EVENT_USB_DATA,
                             replay_get_xfer_id(xfer), xfer);
    }
}

void replay_req_register_data(struct libusb_transfer *xfer)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        replay_add_usb_event(REPLAY_ASYNC_EVENT_USB_DATA,
                             replay_get_xfer_id(xfer), xfer);
    }
}


void replay_event_usb_data(void *opaque)
{
    struct libusb_transfer *xfer = opaque;

    usb_host_req_complete_data(xfer);
}

void replay_req_complete_iso(struct libusb_transfer *xfer)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_add_usb_event(REPLAY_ASYNC_EVENT_USB_ISO,
                             replay_get_iso_xfer_id(xfer), xfer);
    }
}

void replay_req_register_iso(struct libusb_transfer *xfer)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        USBHostIsoXfer *r = xfer->user_data;
        USBHostDevice  *s = r->ring->host;

        replay_add_usb_event(REPLAY_ASYNC_EVENT_USB_ISO,
                             replay_get_iso_xfer_id(xfer), xfer);
    }
}

void replay_event_usb_iso(void *opaque)
{
    struct libusb_transfer *xfer = opaque;

    usb_host_req_complete_iso(xfer);
}

#endif

bool replay_usb_has_xfers(void)
{
#ifdef CONFIG_USB_LIBUSB
    return usb_host_has_xfers();
#else
    return false;
#endif
}
