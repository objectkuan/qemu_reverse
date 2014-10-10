/*
 * replay-net.c
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
#include "net/net.h"
#include "replay.h"
#include "replay-internal.h"

/* limited by Ethernet frame size */
#define MAX_NET_PACKET_SIZE             1560

/* Network data */
NetClientState **vlan_states = NULL;
size_t vlan_states_count = 0;
size_t vlan_states_capacity = 0;

/* Structure for storing information about the network packet */
typedef struct {
    /* Offset in the replay log file where packet is saved. */
    uint64_t file_offset;
    /* Number of step when packet came. */
    uint64_t step;
} QEMU_PACKED NetPacketInfo;

typedef struct NetPacketQueue {
    /* ID of the packet */
    uint64_t id;
    /* ID of the network client */
    int32_t nc_id;
    size_t size;
    uint8_t buf[MAX_NET_PACKET_SIZE];
    uint64_t offset;
} NetPacketQueue;

/* Network packets count. */
static uint64_t net_packets_count;
/* Capacity of the array for packets parameters. */
static uint64_t net_packets_capacity;
/* Array for storing network packets parameters. */
static NetPacketInfo *net_packets;


void replay_net_init(void)
{
    net_packets_count = 0;
}

void replay_net_read_packets_data(void)
{
    net_packets_count = replay_get_qword();
    net_packets_capacity = net_packets_count;
    if (net_packets_count) {
        net_packets = (NetPacketInfo *)g_malloc(sizeof(NetPacketInfo)
                                                * net_packets_count);
        if (fread(net_packets, sizeof(NetPacketInfo),
                  net_packets_count, replay_file) != net_packets_count) {
            fprintf(stderr, "Internal error in replay_net_read_packets_data\n");
            exit(1);
        }
    }
}

void replay_net_write_packets_data(void)
{
    replay_put_qword(net_packets_count);
    if (net_packets && net_packets_count) {
        fwrite(net_packets, sizeof(NetPacketInfo),
               net_packets_count, replay_file);
    }
}

void replay_add_network_client(NetClientState *nc)
{
    if (vlan_states_count == 0) {
        vlan_states = (NetClientState **)g_malloc(sizeof(*vlan_states));
        vlan_states_count = 0;
        vlan_states_capacity = 1;
    } else if (vlan_states_count == vlan_states_capacity) {
        vlan_states_capacity *= 2;
        vlan_states = (NetClientState **)g_realloc(vlan_states,
                                                   sizeof(*vlan_states)
                                                   * vlan_states_capacity);
    }

    vlan_states[vlan_states_count++] = nc;
}

void replay_net_free(void)
{
    if (vlan_states) {
        g_free(vlan_states);
        vlan_states = NULL;
    }
}

void replay_save_net_packet(struct NetClientState *nc, const uint8_t *buf,
                            size_t size)
{
    if (replay_file) {
        if (net_packets_capacity == net_packets_count) {
            if (net_packets_capacity == 0) {
                net_packets_capacity = 1;
            } else {
                net_packets_capacity *= 2;
            }
            net_packets = (NetPacketInfo *)g_realloc(net_packets,
                                                     net_packets_capacity
                                                     * sizeof(NetPacketInfo));
        }

        /* add packet processing event to the queue */
        NetPacketQueue *p = (NetPacketQueue *)
                                g_malloc0(sizeof(NetPacketQueue));
        p->id = net_packets_count;
        p->size = size;
        if (net_hub_id_for_client(nc, &p->nc_id) < 0) {
            fprintf(stderr, "Replay: Cannot determine net client id\n");
            exit(1);
        }
        memcpy(p->buf, buf, size);
        replay_add_event(REPLAY_ASYNC_EVENT_NETWORK, p);

        ++net_packets_count;
    }
}

static NetClientState *replay_net_find_vlan(NetPacketQueue *packet)
{
    int i;
    for (i = 0 ; i < vlan_states_count ; ++i) {
        int id = 0;
        if (net_hub_id_for_client(vlan_states[i], &id) < 0) {
            fprintf(stderr, "Replay: Cannot determine net client id\n");
            exit(1);
        }
        if (id == packet->nc_id) {
            return vlan_states[i];
        }
    }

    fprintf(stderr, "Replay: please specify -net replay command-line option\n");
    exit(1);

    return NULL;
}

void replay_net_send_packet(void *opaque)
{
    NetPacketQueue *packet = (NetPacketQueue *)opaque;
    NetClientState *vlan_state = replay_net_find_vlan(packet);

    if (replay_mode == REPLAY_MODE_RECORD) {
        net_packets[packet->id].file_offset = packet->offset;
        net_packets[packet->id].step = replay_get_current_step();

        qemu_send_packet(vlan_state, packet->buf, packet->size);
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        qemu_send_packet(vlan_state, packet->buf, packet->size);
    }

    g_free(packet);
}

void replay_net_save_packet(void *opaque)
{
    NetPacketQueue *p = (NetPacketQueue *)opaque;
    p->offset = ftello64(replay_file);
    replay_put_qword(p->id);
    replay_put_dword(p->nc_id);
    replay_put_array(p->buf, p->size);
}

void *replay_net_read_packet(void)
{
    NetPacketQueue *p = g_malloc0(sizeof(NetPacketQueue));;
    p->id = replay_get_qword();
    p->nc_id = replay_get_dword();
    replay_get_array(p->buf, &p->size);
    replay_check_error();

    return p;
}
