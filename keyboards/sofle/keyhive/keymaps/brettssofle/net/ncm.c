// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// USB network config mode: while it is on, the keyboard enumerates as a
// composite device (keyboard, mouse, raw HID, ... + CDC-NCM network adapter)
// under its own product ID, so the computer gets a small Ethernet link to the
// keyboard (192.168.7.1; the computer gets 192.168.7.2 by DHCP, no gateway,
// no DNS). Leaving it re-enumerates as the plain keyboard. Keys and the
// trackpad keep working in both.
//
// NCM is driverless on Linux and on Windows 10 1903+ / 11 (UsbNcm.sys); MS OS
// 2.0 descriptors (compatible ID "WINNCM") make Windows pick that driver for
// the NCM function of this composite device.
//
// The descriptors are the normal QMK ones with the NCM function appended
// (get_usb_descriptor_user), class requests and the data interface's
// alternate setting are handled in usb_requests_hook_user, and the endpoints
// are set up in usb_event_user. Everything else runs in net_task() from the
// main loop, one transfer block at a time, so the matrix scan never waits.

#include "quantum.h"
#include <hal.h>
#include "usb_main.h"
#include "usb_descriptor.h"
#include "net.h"
#include "ntb.h"
#include "stack.h"
#include "diag.h"

#ifndef NET_PRODUCT_ID
#    define NET_PRODUCT_ID (PRODUCT_ID + 1) // its own PID: Windows binds the new function cleanly
#endif
#define NET_FALLBACK_MS 20000   // no address handed out by then: show the fallbacks
#define NET_IDLE_EXIT_MS 600000 // back to the plain keyboard after 10 min without traffic

#define NCM_EP_NOTIFY 13
#define NCM_EP_DATA 14
#define NCM_EP_DATA_SIZE 64
#define NCM_EP_NOTIFY_SIZE 16
#define NCM_IF_COMM TOTAL_INTERFACES
#define NCM_IF_DATA (TOTAL_INTERFACES + 1)
#define NCM_STR_MAC 0x20
#define MS_VENDOR_CODE 0x4E
#define TX_QUEUE 4
#define FRAME_MAX 1514

_Static_assert(NCM_EP_DATA <= USB_MAX_ENDPOINTS, "not enough USB endpoints");

extern const USB_Descriptor_Device_t        DeviceDescriptor;
extern const USB_Descriptor_Configuration_t ConfigurationDescriptor;

/* ---- descriptors ---- */

#define NCM_DESC_LEN 85
static const uint8_t ncm_desc[NCM_DESC_LEN] = {
    // Interface association: CDC (02) NCM (0D)
    8, 0x0B, NCM_IF_COMM, 2, 0x02, 0x0D, 0x00, 0,
    // Communication interface
    9, 0x04, NCM_IF_COMM, 0, 1, 0x02, 0x0D, 0x00, 0,
    5, 0x24, 0x00, 0x10, 0x01,                                // CDC header 1.10
    5, 0x24, 0x06, NCM_IF_COMM, NCM_IF_DATA,                  // union
    13, 0x24, 0x0F, NCM_STR_MAC, 0, 0, 0, 0, 0xEA, 0x05, 0, 0, 0, // Ethernet: MAC string, max segment 1514
    6, 0x24, 0x1A, 0x00, 0x01, 0x00,                          // NCM 1.00
    7, 0x05, 0x80 | NCM_EP_NOTIFY, 0x03, NCM_EP_NOTIFY_SIZE, 0, 16,
    // Data interface: alternate 0 = off, 1 = on with the bulk endpoints
    9, 0x04, NCM_IF_DATA, 0, 0, 0x0A, 0x00, 0x01, 0,
    9, 0x04, NCM_IF_DATA, 1, 2, 0x0A, 0x00, 0x01, 0,
    7, 0x05, 0x80 | NCM_EP_DATA, 0x02, NCM_EP_DATA_SIZE, 0, 0,
    7, 0x05, NCM_EP_DATA, 0x02, NCM_EP_DATA_SIZE, 0, 0,
};

#define MSOS_SET_LEN 46
static const uint8_t bos_desc[33] = {
    5, 0x0F, 33, 0, 1,
    // Microsoft OS 2.0 platform capability (UUID D8DD60DF-4589-4CC7-9CD2-659D9E648A9F)
    28, 0x10, 0x05, 0x00, 0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C, 0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
    0x00, 0x00, 0x03, 0x06, MSOS_SET_LEN, 0, MS_VENDOR_CODE, 0,
};
static const uint8_t msos_set[MSOS_SET_LEN] = {
    10, 0, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, MSOS_SET_LEN, 0, // set header, Windows 8.1+
    8, 0, 0x01, 0x00, 0, 0, 36, 0,                               // configuration subset
    8, 0, 0x02, 0x00, NCM_IF_COMM, 0, 28, 0,                     // function subset: the NCM function
    20, 0, 0x03, 0x00, 'W', 'I', 'N', 'N', 'C', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// GET_NTB_PARAMETERS: 16-bit NTBs, up to 2048 bytes each way, 4-byte alignment.
static const uint8_t ntb_params[28] = {
    28, 0, 0x01, 0x00, 0x00, 0x08, 0x00, 0x00, 4, 0, 0, 0, 4, 0, 0, 0,
    0x00, 0x08, 0x00, 0x00, 4, 0, 0, 0, 4, 0, 0, 0,
};

static uint8_t                 config_desc[sizeof(USB_Descriptor_Configuration_t) + NCM_DESC_LEN];
static USB_Descriptor_Device_t device_desc;
static uint8_t                 mac_string[2 + 24];

static void build_descriptors(void) {
    memcpy(config_desc, &ConfigurationDescriptor, sizeof(USB_Descriptor_Configuration_t));
    memcpy(config_desc + sizeof(USB_Descriptor_Configuration_t), ncm_desc, NCM_DESC_LEN);
    config_desc[2] = sizeof(config_desc) & 0xFF;
    config_desc[3] = sizeof(config_desc) >> 8;
    config_desc[4] = TOTAL_INTERFACES + 2;

    memcpy(&device_desc, &DeviceDescriptor, sizeof(device_desc));
    device_desc.USBSpecification = VERSION_BCD(2, 1, 0); // BOS (MS OS 2.0) needs 2.01+
    device_desc.Class            = USB_CSCP_IADDeviceClass;
    device_desc.SubClass         = USB_CSCP_IADDeviceSubclass;
    device_desc.Protocol         = USB_CSCP_IADDeviceProtocol;
    device_desc.ProductID        = NET_PRODUCT_ID;

    static const char hex[] = "0123456789ABCDEF";
    mac_string[0]           = sizeof(mac_string);
    mac_string[1]           = DTYPE_String;
    for (uint8_t i = 0; i < 6; i++) {
        mac_string[2 + i * 4]     = hex[net_host_mac[i] >> 4];
        mac_string[2 + i * 4 + 1] = 0;
        mac_string[2 + i * 4 + 2] = hex[net_host_mac[i] & 15];
        mac_string[2 + i * 4 + 3] = 0;
    }
}

/* ---- state ---- */

static bool              net_on; // the NCM function is in the descriptors
static uint8_t           state;
static uint32_t          enter_time;
static volatile bool     data_on; // data interface alternate setting 1
static volatile bool     rx_full, tx_busy, notify_busy;
static volatile uint16_t rx_len;
static uint8_t           notify_step; // 1 = speed, 2 = connection, 0 = done
static uint16_t          seq;
static uint8_t           rx_buf[NTB_MAX_SIZE] __attribute__((aligned(4)));
static uint8_t           tx_buf[NTB_MAX_SIZE] __attribute__((aligned(4)));
static uint8_t           notify_buf[16] __attribute__((aligned(4)));
static uint8_t           txq[TX_QUEUE][FRAME_MAX];
static uint16_t          txq_len[TX_QUEUE];
static uint8_t           txq_head, txq_count;
static uint8_t           exit_why;
static uint32_t          exit_time;

uint32_t net_millis(void) {
    return timer_read32();
}

/* ---- endpoints (callbacks run in the USB interrupt) ---- */

static void notify_in_cb(USBDriver *usbp, usbep_t ep) {
    notify_busy = false;
}

static void data_in_cb(USBDriver *usbp, usbep_t ep) {
    tx_busy = false;
}

static void data_out_cb(USBDriver *usbp, usbep_t ep) {
    rx_len  = usbGetReceiveTransactionSizeX(usbp, ep);
    rx_full = true; // handed to net_task(); receiving restarts once it's parsed
}

static USBInEndpointState        notify_in_state, data_in_state;
static USBOutEndpointState       data_out_state;
static const USBEndpointConfig   notify_ep = {
    .ep_mode    = USB_EP_MODE_TYPE_INTR,
    .in_cb      = notify_in_cb,
    .in_maxsize = NCM_EP_NOTIFY_SIZE,
    .in_state   = &notify_in_state,
};
static const USBEndpointConfig data_ep = {
    .ep_mode     = USB_EP_MODE_TYPE_BULK,
    .in_cb       = data_in_cb,
    .out_cb      = data_out_cb,
    .in_maxsize  = NCM_EP_DATA_SIZE,
    .out_maxsize = NCM_EP_DATA_SIZE,
    .in_state    = &data_in_state,
    .out_state   = &data_out_state,
};

static void link_reset(void) {
    data_on     = false;
    rx_full     = false;
    tx_busy     = false;
    notify_busy = false;
    notify_step = 0;
    txq_count   = 0;
}

void usb_event_user(USBDriver *usbp, usbevent_t event) {
    if (!net_on) {
        return;
    }
    switch (event) {
        case USB_EVENT_CONFIGURED:
            osalSysLockFromISR();
            usbInitEndpointI(usbp, NCM_EP_NOTIFY, &notify_ep);
            usbInitEndpointI(usbp, NCM_EP_DATA, &data_ep);
            osalSysUnlockFromISR();
            link_reset();
            break;
        case USB_EVENT_RESET:
        case USB_EVENT_UNCONFIGURED:
        case USB_EVENT_SUSPEND:
            link_reset();
            break;
        default:
            break;
    }
}

static void set_data_interface(USBDriver *usbp, uint8_t alt) {
    link_reset();
    if (alt == 1) {
        data_on = true;
        osalSysLockFromISR();
        if (!usbGetReceiveStatusI(usbp, NCM_EP_DATA)) {
            usbStartReceiveI(usbp, NCM_EP_DATA, rx_buf, NTB_MAX_SIZE);
        }
        osalSysUnlockFromISR();
        notify_step = 1; // tell the computer the link is up
    }
}

/* ---- control requests (USB interrupt) ---- */

bool usb_requests_hook_user(USBDriver *usbp) {
    if (!net_on) {
        return false;
    }
    static uint8_t ctrl[64] __attribute__((aligned(4)));
    const uint8_t *s      = usbp->setup;
    uint8_t        type   = s[0], req = s[1];
    uint16_t       value  = s[2] | s[3] << 8;
    uint16_t       index  = s[4] | s[5] << 8;
    uint16_t       length = s[6] | s[7] << 8;

    if (type == 0xC0 && req == MS_VENDOR_CODE && index == 7) {
        usbSetupTransfer(usbp, (uint8_t *)msos_set, MIN(length, MSOS_SET_LEN), NULL);
        return true;
    }
    uint8_t iface = index & 0xFF;
    if ((type & USB_RTYPE_RECIPIENT_MASK) != USB_RTYPE_RECIPIENT_INTERFACE || (iface != NCM_IF_COMM && iface != NCM_IF_DATA)) {
        return false;
    }
    if ((type & USB_RTYPE_TYPE_MASK) == USB_RTYPE_TYPE_STD) {
        if (req == USB_REQ_SET_INTERFACE) {
            if (iface == NCM_IF_DATA && value <= 1) {
                set_data_interface(usbp, value);
            } else if (value != 0) {
                return false;
            }
            usbSetupTransfer(usbp, NULL, 0, NULL);
            return true;
        }
        if (req == USB_REQ_GET_INTERFACE) {
            ctrl[0] = iface == NCM_IF_DATA && data_on ? 1 : 0;
            usbSetupTransfer(usbp, ctrl, 1, NULL);
            return true;
        }
        return false;
    }
    if ((type & USB_RTYPE_TYPE_MASK) != USB_RTYPE_TYPE_CLASS || iface != NCM_IF_COMM) {
        return false;
    }
    switch (req) {
        case 0x80: // GET_NTB_PARAMETERS
            usbSetupTransfer(usbp, (uint8_t *)ntb_params, MIN(length, sizeof(ntb_params)), NULL);
            return true;
        case 0x83: // GET_NTB_FORMAT: 16-bit
            ctrl[0] = ctrl[1] = 0;
            usbSetupTransfer(usbp, ctrl, MIN(length, 2), NULL);
            return true;
        case 0x85: // GET_NTB_INPUT_SIZE
            ctrl[0] = NTB_MAX_SIZE & 0xFF;
            ctrl[1] = NTB_MAX_SIZE >> 8;
            ctrl[2] = ctrl[3] = 0;
            usbSetupTransfer(usbp, ctrl, MIN(length, 4), NULL);
            return true;
        case 0x40: // SET_ETHERNET_MULTICAST_FILTERS
        case 0x41: // SET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER
        case 0x43: // SET_ETHERNET_PACKET_FILTER
        case 0x84: // SET_NTB_FORMAT
        case 0x86: // SET_NTB_INPUT_SIZE (our blocks are always smaller)
        case 0x87: // SET_MAX_DATAGRAM_SIZE
        case 0x8A: // SET_CRC_MODE
            usbSetupTransfer(usbp, length ? ctrl : NULL, MIN(length, sizeof(ctrl)), NULL);
            return true;
        default:
            return false;
    }
}

uint16_t get_usb_descriptor_user(const uint16_t wValue, const uint16_t wIndex, const uint16_t wLength, const void **const address) {
    if (!net_on) {
        return 0;
    }
    switch (wValue >> 8) {
        case DTYPE_Device:
            *address = &device_desc;
            return sizeof(device_desc);
        case DTYPE_Configuration:
            *address = config_desc;
            return sizeof(config_desc);
        case 0x0F: // BOS
            *address = bos_desc;
            return sizeof(bos_desc);
        case DTYPE_String:
            if ((wValue & 0xFF) == NCM_STR_MAC) {
                *address = mac_string;
                return sizeof(mac_string);
            }
            return 0;
        default:
            return 0;
    }
}

/* ---- main loop side ---- */

static bool usb_ready(void) {
    return USB_DRIVER.state == USB_ACTIVE && data_on;
}

static void flush_tx(void) {
    if (tx_busy || !txq_count || !usb_ready()) {
        return;
    }
    uint16_t size = ntb_build(tx_buf, txq[txq_head], txq_len[txq_head], seq++);
    txq_head      = (txq_head + 1) % TX_QUEUE;
    txq_count--;
    tx_busy = true;
    chSysLock();
    usbStartTransmitI(&USB_DRIVER, NCM_EP_DATA, tx_buf, size);
    chSysUnlock();
}

static void ncm_tx(const uint8_t *frame, uint16_t len) {
    if (!data_on || len > FRAME_MAX || txq_count == TX_QUEUE) {
        return; // dropped: the computer's TCP/ARP retry
    }
    uint8_t slot = (txq_head + txq_count) % TX_QUEUE;
    memcpy(txq[slot], frame, len);
    txq_len[slot] = len;
    txq_count++;
    flush_tx();
}

static void send_notification(void) {
    if (!notify_step || notify_busy || !usb_ready()) {
        return;
    }
    uint8_t n;
    memset(notify_buf, 0, sizeof(notify_buf));
    notify_buf[0] = 0xA1;
    notify_buf[4] = NCM_IF_COMM;
    if (notify_step == 1) {
        // CONNECTION_SPEED_CHANGE: 12 Mbit/s both ways
        notify_buf[1] = 0x2A;
        notify_buf[6] = 8;
        for (uint8_t i = 0; i < 2; i++) {
            notify_buf[8 + i * 4]  = 12000000 & 0xFF;
            notify_buf[9 + i * 4]  = (12000000 >> 8) & 0xFF;
            notify_buf[10 + i * 4] = (12000000 >> 16) & 0xFF;
            notify_buf[11 + i * 4] = 12000000 >> 24;
        }
        n           = 16;
        notify_step = 2;
    } else {
        // NETWORK_CONNECTION: connected
        notify_buf[1] = 0x00;
        notify_buf[2] = 1;
        n             = 8;
        notify_step   = 0;
    }
    notify_busy = true;
    chSysLock();
    usbStartTransmitI(&USB_DRIVER, NCM_EP_NOTIFY, notify_buf, n);
    chSysUnlock();
}

static void stack_start(void) {
    NET_CRUMB(CRUMB_NET_START);
    netstack_start(ncm_tx);
    NET_CRUMB(CRUMB_LOOP);
}

static void stack_stop(void) {
    NET_CRUMB(CRUMB_NET_START);
    netstack_stop();
    NET_CRUMB(CRUMB_LOOP);
}

void net_mode_enter(void) {
    if (net_on) {
        return;
    }
    build_descriptors();
    link_reset();
    clear_keyboard();
    net_on     = true;
    state      = NET_USB;
    enter_time = timer_read32();
    diag_on_net_stack(stack_start);
    restart_usb_driver(&USB_DRIVER); // re-enumerate with the network function
}

void net_mode_exit(uint8_t why) {
    if (!net_on) {
        return;
    }
    exit_why  = why;
    exit_time = timer_read32();
    diag_on_net_stack(stack_stop);
    net_on = false;
    link_reset();
    state = NET_OFF;
    clear_keyboard();
    restart_usb_driver(&USB_DRIVER); // back to the plain keyboard
}

uint8_t net_last_exit(uint32_t *ms_ago) {
    *ms_ago = exit_why ? timer_elapsed32(exit_time) : 0;
    return exit_why;
}

bool net_mode_active(void) {
    return net_on;
}

uint8_t net_state(void) {
    return state;
}

uint16_t net_mode_seconds(void) {
    uint32_t s = net_on ? timer_elapsed32(enter_time) / 1000 : 0;
    return s > 9999 ? 9999 : s;
}

bool net_failed(void) {
    return net_on && state != NET_READY && timer_elapsed32(enter_time) > NET_FALLBACK_MS;
}

// Runs on the network code's own stack (diag.c): lwIP, the HTTP server and
// the API (which runs VIA / Vial commands) go too deep for the main loop's.
static void net_task_body(void) {
    if (rx_full) {
        // One transfer block per call; lwIP answers synchronously.
        NET_CRUMB(CRUMB_NET_RX);
        ntb_parse(rx_buf, rx_len, netstack_input);
        rx_full = false;
        chSysLock();
        if (data_on && !usbGetReceiveStatusI(&USB_DRIVER, NCM_EP_DATA)) {
            usbStartReceiveI(&USB_DRIVER, NCM_EP_DATA, rx_buf, NTB_MAX_SIZE);
        }
        chSysUnlock();
    }
    NET_CRUMB(CRUMB_NET_TIMER);
    netstack_poll();
    NET_CRUMB(CRUMB_NET_TX);
    send_notification();
    flush_tx();
    NET_CRUMB(CRUMB_LOOP);
}

void net_task(void) {
    if (!net_on) {
        return;
    }
    diag_on_net_stack(net_task_body);
    state = !data_on ? NET_USB : netstack_leases() ? NET_READY : NET_LINK;
    if (state == NET_READY && netstack_idle_ms() > NET_IDLE_EXIT_MS) {
        net_mode_exit(NET_EXIT_IDLE);
    }
}
