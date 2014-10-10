/*
 * replay-internal.c
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

volatile unsigned int replay_data_kind = -1;
volatile unsigned int replay_has_unread_data;

/* File for replay writing */
FILE *replay_file;

void replay_put_byte(unsigned char byte)
{
    if (replay_file) {
        fwrite(&byte, sizeof(byte), 1, replay_file);
    }
}

void replay_put_event(unsigned char event)
{
    replay_put_byte(event);
}


void replay_put_word(uint16_t word)
{
    if (replay_file) {
        fwrite(&word, sizeof(word), 1, replay_file);
    }
}

void replay_put_dword(unsigned int dword)
{
    if (replay_file) {
        fwrite(&dword, sizeof(dword), 1, replay_file);
    }
}

void replay_put_qword(int64_t qword)
{
    if (replay_file) {
        fwrite(&qword, sizeof(qword), 1, replay_file);
    }
}

void replay_put_array(const uint8_t *buf, size_t size)
{
    if (replay_file) {
        fwrite(&size, sizeof(size), 1, replay_file);
        fwrite(buf, 1, size, replay_file);
    }
}

unsigned char replay_get_byte(void)
{
    unsigned char byte;
    if (replay_file) {
        fread(&byte, sizeof(byte), 1, replay_file);
    }
    return byte;
}

uint16_t replay_get_word(void)
{
    uint16_t word;
    if (replay_file) {
        fread(&word, sizeof(word), 1, replay_file);
    }

    return word;
}

unsigned int replay_get_dword(void)
{
    unsigned int dword;
    if (replay_file) {
        fread(&dword, sizeof(dword), 1, replay_file);
    }

    return dword;
}

int64_t replay_get_qword(void)
{
    int64_t qword;
    if (replay_file) {
        fread(&qword, sizeof(qword), 1, replay_file);
    }

    return qword;
}

void replay_get_array(uint8_t *buf, size_t *size)
{
    if (replay_file) {
        fread(size, sizeof(*size), 1, replay_file);
        fread(buf, 1, *size, replay_file);
    }
}

void replay_get_array_alloc(uint8_t **buf, size_t *size)
{
    if (replay_file) {
        fread(size, sizeof(*size), 1, replay_file);
        *buf = g_malloc(*size);
        fread(*buf, 1, *size, replay_file);
    }
}

void replay_check_error(void)
{
    if (replay_file) {
        if (feof(replay_file)) {
            fprintf(stderr, "replay file is over\n");
            exit(1);
        } else if (ferror(replay_file)) {
            fprintf(stderr, "replay file is over or something goes wrong\n");
            exit(1);
        }
    }
}

void replay_fetch_data_kind(void)
{
    if (replay_file) {
        if (!replay_has_unread_data) {
            replay_data_kind = replay_get_byte();
            replay_check_error();
            replay_has_unread_data = 1;
        }
    }
}

/*! Saves cached instructions. */
void replay_save_instructions(void)
{
    if (replay_file && replay_mode == REPLAY_MODE_RECORD) {
        if (first_cpu != NULL && first_cpu->instructions_count > 0) {
            replay_put_event(EVENT_INSTRUCTION);
            replay_put_dword(first_cpu->instructions_count);
            replay_state.current_step += first_cpu->instructions_count;
            first_cpu->instructions_count = 0;
        }
    }
}
