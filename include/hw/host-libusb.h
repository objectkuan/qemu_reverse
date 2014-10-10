#ifndef HOST_LIBUSB_H
#define HOST_LIBUSB_H

#include <libusb.h>

#include "qemu/notify.h"

#define TYPE_USB_HOST_DEVICE "usb-host"
#define USB_HOST_DEVICE(obj) \
     OBJECT_CHECK(USBHostDevice, (obj), TYPE_USB_HOST_DEVICE)

struct USBAutoFilter {
    uint32_t bus_num;
    uint32_t addr;
    char     *port;
    uint32_t vendor_id;
    uint32_t product_id;
};

typedef struct USBHostDevice USBHostDevice;
typedef struct USBHostRequest USBHostRequest;
typedef struct USBHostIsoXfer USBHostIsoXfer;
typedef struct USBHostIsoRing USBHostIsoRing;
typedef struct USBHostInterface USBHostInterface;

enum USBHostDeviceOptions {
    USB_HOST_OPT_PIPELINE,
};

struct USBHostInterface {
    bool                             detached;
    bool                             claimed;
};

struct USBHostDevice {
    USBDevice parent_obj;

    /* properties */
    struct USBAutoFilter             match;
    int32_t                          bootindex;
    uint32_t                         iso_urb_count;
    uint32_t                         iso_urb_frames;
    uint32_t                         options;
    uint32_t                         loglevel;

    /* state */
    QTAILQ_ENTRY(USBHostDevice)      next;
    int                              seen, errcount;
    int                              bus_num;
    int                              addr;
    char                             port[16];
    bool                             is_open;

    libusb_device                    *dev;
    libusb_device_handle             *dh;
    struct libusb_device_descriptor  ddesc;

    USBHostInterface ifs[USB_MAX_INTERFACES];

    /* callbacks & friends */
    QEMUBH                           *bh_nodev;
    QEMUBH                           *bh_postld;
    Notifier                         exit;

    /* request queues */
    QTAILQ_HEAD(, USBHostRequest)    requests;
    QTAILQ_HEAD(, USBHostIsoRing)    isorings;
};

struct USBHostRequest {
    USBHostDevice                    *host;
    USBPacket                        *p;
    bool                             in;
    struct libusb_transfer           *xfer;
    unsigned char                    *buffer;
    unsigned char                    *cbuf;
    unsigned int                     clen;
    QTAILQ_ENTRY(USBHostRequest)     next;
};

struct USBHostIsoXfer {
    USBHostIsoRing                   *ring;
    struct libusb_transfer           *xfer;
    bool                             copy_complete;
    unsigned int                     packet;
    QTAILQ_ENTRY(USBHostIsoXfer)     next;
};

struct USBHostIsoRing {
    USBHostDevice                    *host;
    USBEndpoint                      *ep;
    QTAILQ_HEAD(, USBHostIsoXfer)    unused;
    QTAILQ_HEAD(, USBHostIsoXfer)    inflight;
    QTAILQ_HEAD(, USBHostIsoXfer)    copy;
    QTAILQ_ENTRY(USBHostIsoRing)     next;
};

void usb_host_req_complete_ctrl(struct libusb_transfer *xfer);
void usb_host_req_complete_data(struct libusb_transfer *xfer);
void usb_host_req_complete_iso(struct libusb_transfer *xfer);
unsigned char *usb_host_get_iso_packet_buffer(USBHostIsoXfer *xfer, int packet);

bool usb_host_has_xfers(void);

#endif
