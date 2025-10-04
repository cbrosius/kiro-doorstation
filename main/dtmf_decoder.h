#ifndef DTMF_DECODER_H
#define DTMF_DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    DTMF_0 = '0',
    DTMF_1 = '1',
    DTMF_2 = '2',
    DTMF_3 = '3',
    DTMF_4 = '4',
    DTMF_5 = '5',
    DTMF_6 = '6',
    DTMF_7 = '7',
    DTMF_8 = '8',
    DTMF_9 = '9',
    DTMF_STAR = '*',
    DTMF_HASH = '#',
    DTMF_A = 'A',
    DTMF_B = 'B',
    DTMF_C = 'C',
    DTMF_D = 'D',
    DTMF_NONE = 0
} dtmf_tone_t;

typedef void (*dtmf_callback_t)(dtmf_tone_t tone);

void dtmf_decoder_init(void);
void dtmf_set_callback(dtmf_callback_t callback);
dtmf_tone_t dtmf_decode_buffer(const int16_t *buffer, size_t length);
void dtmf_process_audio(const int16_t *buffer, size_t length);

#endif