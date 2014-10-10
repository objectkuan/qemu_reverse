#include "replay/replay.h"

ReplayMode replay_mode;

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
