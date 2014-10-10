/*
 * net-replay.c
 *
 * Copyright (c) 2010-2014 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "net/net.h"
#include "clients.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "replay/replay.h"

typedef struct NetReplayState {
    NetClientState nc;
} NetReplayState;

static ssize_t net_replay_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    return size;
}

static void net_replay_cleanup(NetClientState *nc)
{
}

static NetClientInfo net_replay_info = {
    .type = NET_CLIENT_OPTIONS_KIND_REPLAY,
    .size = sizeof(NetReplayState),
    .receive = net_replay_receive,
    .cleanup = net_replay_cleanup,
};

static int net_replay_init(NetClientState *vlan, const char *device,
                         const char *name)
{
    NetClientState *nc;

    nc = qemu_new_net_client(&net_replay_info, vlan, device, name);

    snprintf(nc->info_str, sizeof(nc->info_str), "replayer");

    if (replay_mode == REPLAY_MODE_RECORD) {
        fprintf(stderr, "-net replay is not permitted in record mode\n");
        exit(1);
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        replay_add_network_client(nc);
    } else {
        fprintf(stderr, "-net replay is not permitted without replay\n");
        exit(1);
    }

    return 0;
}

int net_init_replay(const NetClientOptions *opts, const char *name,
                    NetClientState *peer)
{
    assert(peer);
    assert(opts->kind == NET_CLIENT_OPTIONS_KIND_REPLAY);

    return net_replay_init(peer, "replay", name);
}
