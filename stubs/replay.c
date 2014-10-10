#include "replay/replay.h"
#include "sysemu/sysemu.h"

ReplayMode replay_mode;
int replay_icount;

ReplaySubmode replay_get_play_submode(void)
{
    return 0;
}

void replay_save_clock(unsigned int kind, int64_t clock)
{
}

int64_t replay_read_clock(unsigned int kind)
{
    return 0;
}

int replay_checkpoint(unsigned int checkpoint)
{
    return 0;
}

int runstate_is_running(void)
{
    return 0;
}

int64_t replay_get_icount(void)
{
    return 0;
}

void replay_add_bh_event(void *bh, uint64_t id)
{
}
