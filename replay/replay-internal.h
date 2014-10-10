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

/*! Saves queued events (like instructions and sound). */
void replay_save_instructions(void);
/*! Checks that the next data is corresponding to the desired kind.
    Terminates the program in case of error. */
void validate_data_kind(int kind);

#endif
