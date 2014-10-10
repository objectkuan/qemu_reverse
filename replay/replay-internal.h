#ifndef REPLAY_INTERNAL_H
#define REPLAY_INTERNAL_H

/*
 * replay-internal.h
 *
 * Copyright (c) 2010-2014 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <stdio.h>
#include "sysemu/sysemu.h"

/* internal data for savevm */
#define EVENT_END_STARTUP           0
/* for time_t event */
#define EVENT_TIME_T                1
/* for tm event */
#define EVENT_TM                    2
/* for outgoing sound event */
#define EVENT_SOUND_OUT             7
/* for incoming sound event */
#define EVENT_SOUND_IN              8
/* for software interrupt */
#define EVENT_INTERRUPT             15
/* for shutdown request */
#define EVENT_SHUTDOWN              20
/* for save VM event */
#define EVENT_SAVE_VM_BEGIN         21
/* for save VM event */
#define EVENT_SAVE_VM_END           22
/* for emulated exceptions */
#define EVENT_EXCEPTION             23
/* for async events */
#define EVENT_ASYNC                 24
#define EVENT_ASYNC_OPT             25
/* for instruction event */
#define EVENT_INSTRUCTION           32
/* for clock read/writes */
#define EVENT_CLOCK                 64
/* some of grteater codes are reserved for clocks */

/* for checkpoint event */
#define EVENT_CHECKPOINT            96
/* end of log event */
#define EVENT_END                   127

/* Asynchronous events IDs */

#define REPLAY_ASYNC_EVENT_BH          0
#define REPLAY_ASYNC_EVENT_THREAD      1
#define REPLAY_ASYNC_EVENT_INPUT       2
#define REPLAY_ASYNC_EVENT_INPUT_SYNC  3
#define REPLAY_ASYNC_EVENT_NETWORK     4
#define REPLAY_ASYNC_COUNT             5

typedef struct ReplayState {
    /*! Cached clock values. */
    int64_t cached_clock[REPLAY_CLOCK_COUNT];
    /*! Nonzero, when next instruction is repeated one and was already
        processed. */
    int skipping_instruction;
    /*! Current step - number of processed instructions and timer events. */
    uint64_t current_step;
    /*! Temporary data for saving/loading replay file position. */
    uint64_t file_offset;
} ReplayState;
extern ReplayState replay_state;

/*! Information about saved VM state */
struct SavedStateInfo {
    /* Offset in the replay log file where state is saved. */
    uint64_t file_offset;
    /* Step number, corresponding to the saved state. */
    uint64_t step;
};
/*! Reference to the saved state */
typedef struct SavedStateInfo SavedStateInfo;

extern volatile unsigned int replay_data_kind;
extern volatile unsigned int replay_has_unread_data;

/* File for replay writing */
extern FILE *replay_file;

void replay_put_byte(unsigned char byte);
void replay_put_event(unsigned char event);
void replay_put_word(uint16_t word);
void replay_put_dword(unsigned int dword);
void replay_put_qword(int64_t qword);
void replay_put_array(const uint8_t *buf, size_t size);

unsigned char replay_get_byte(void);
uint16_t replay_get_word(void);
unsigned int replay_get_dword(void);
int64_t replay_get_qword(void);
void replay_get_array(uint8_t *buf, size_t *size);
void replay_get_array_alloc(uint8_t **buf, size_t *size);

/*! Checks error status of the file. */
void replay_check_error(void);

/*! Reads data type from the file and stores it in the
    replay_data_kind variable. */
void replay_fetch_data_kind(void);
/*! Checks that the next data is corresponding to the desired kind.
    Terminates the program in case of error. */
void validate_data_kind(int kind);

/*! Saves queued events (like instructions and sound). */
void replay_save_instructions(void);

/*! Skips async events until some sync event will be found. */
bool skip_async_events(int stop_event);
/*! Skips async events invocations from the input,
    until required data kind is found. If the requested data is not found
    reports an error and stops the execution. */
void skip_async_events_until(unsigned int kind);

/*! Reads next clock value from the file.
    If clock kind read from the file is different from the parameter,
    the value is not used.
    If the parameter is -1, the clock value is read to the cache anyway. */
void replay_read_next_clock(unsigned int kind);

/* Asynchronous events queue */

/*! Initializes events' processing internals */
void replay_init_events(void);
/*! Clears internal data structures for events handling */
void replay_finish_events(void);
/*! Enables storing events in the queue */
void replay_enable_events(void);
/*! Flushes events queue */
void replay_flush_events(void);
/*! Clears events list before loading new VM state */
void replay_clear_events(void);
/*! Returns true if there are any unsaved events in the queue */
bool replay_has_events(void);
/*! Saves events from queue into the file */
void replay_save_events(int opt);
/*! Read events from the file into the input queue */
void replay_read_events(int opt);
/*! Adds specified async event to the queue */
void replay_add_event(int event_id, void *opaque);

/* Input events */

/*! Saves input event to the log */
void replay_save_input_event(InputEvent *evt);
/*! Reads input event from the log */
InputEvent *replay_read_input_event(void);

/* Network events */

/*! Initializes network data structures. */
void replay_net_init(void);
/*! Cleans up network data structures. */
void replay_net_free(void);
/*! Reads packets offsets array from the log. */
void replay_net_read_packets_data(void);
/*! Writes packets offsets array to the log. */
void replay_net_write_packets_data(void);
/*! Saves network packet into the log. */
void replay_net_save_packet(void *opaque);
/*! Reads network packet from the log. */
void *replay_net_read_packet(void);
/*! Called to send packet that was read or received from external input
    to the net queue. */
void replay_net_send_packet(void *opaque);

/* Sound events */

/*! Returns true, when there are any pending sound events. */
bool replay_has_sound_events(void);
void replay_save_sound_out(void);
void replay_save_sound_in(void);
void replay_read_sound_out(void);
void replay_read_sound_in(void);
void replay_sound_flush_queue(void);

#endif
